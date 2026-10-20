"""Shared fixtures for engine integration test suites.

Anything here is generic to "start a real SbcEngine and talk to it" -
suite-specific behavior (SIP scenarios, route content, ...) belongs in that
suite's own conftest.py.
"""

from __future__ import annotations

import base64
import hashlib
import json
import logging
import re
import socket
import socketserver
import struct
import subprocess
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

import pytest

_log = logging.getLogger("integration")

# The engine retries its websocket connection forever on connection failure
# rather than crashing (see ControlPlaneClient), so a missing/late stub
# route hangs the fixture instead of failing fast - this log line only
# appears once the connection actually succeeds, right before the SIP
# transport binds, so it is the one reliable "engine is ready" signal
# available.
_ROUTES_LOADED_MARKER = "applied routing table"
_ERROR_LOG_PATTERN = re.compile(r"\]\s*\[(error|critical)\]\s*\[")

# RFC 6455 5.2.2: the fixed GUID XORed into the handshake's accept key.
_WS_HANDSHAKE_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


@dataclass(frozen=True)
class RouteRule:
    uri: str
    sip_address: str
    port: int
    codec: str | None = None


@dataclass(frozen=True)
class SipUserFixture:
    """A REGISTER test credential. ha1 is computed the same way the real
    control plane does (sipUsersController.computeHa1) so the stub is a
    faithful stand-in, not a shortcut around the engine's actual digest path.
    """

    username: str
    realm: str
    password: str
    enabled: bool = True

    @property
    def ha1(self) -> str:
        return hashlib.md5(f"{self.username}:{self.realm}:{self.password}".encode()).hexdigest()


def _ws_accept_key(client_key: str) -> str:
    digest = hashlib.sha1((client_key + _WS_HANDSHAKE_GUID).encode()).digest()
    return base64.b64encode(digest).decode()


def _ws_text_frame(payload: bytes) -> bytes:
    """A minimal, unmasked RFC 6455 text frame (server->client frames are
    never masked). No fragmentation: every message here is small enough to
    fit in one frame."""
    return _ws_frame(payload, opcode=0x1)


def _ws_frame(payload: bytes, opcode: int) -> bytes:
    header = bytearray([0x80 | opcode])
    length = len(payload)
    if length < 126:
        header.append(length)
    elif length < 65536:
        header.append(126)
        header += struct.pack(">H", length)
    else:
        header.append(127)
        header += struct.pack(">Q", length)
    return bytes(header) + payload


class RoutesStubServer:
    """Stands in for the control plane's engine-facing websocket channel
    (`/ws/engine`, see control-plane/backend/src/ws/engineChannel.ts).

    Performs the RFC 6455 handshake by hand (no extra Python dependency for
    what's otherwise a small protocol) and
    sends whatever snapshot was last set via `set_routes` as that connection's
    first message, matching the `WsEnvelope{type: "snapshot", routes_snapshot}`
    contract ControlPlaneClient parses (see src/protocols/ControlPlaneWs.hpp).
    Nothing in this suite mutates routes after the engine connects. The stub
    acknowledges engine events but does not push another control-plane event.
    """

    def __init__(self) -> None:
        self._routes: dict[int, RouteRule] = {}
        self._users: list[SipUserFixture] = []
        self._lock = threading.Lock()
        stub = self

        class Handler(socketserver.BaseRequestHandler):
            def _recv_exact(self, size: int) -> bytes | None:
                data = bytearray()
                while len(data) < size:
                    chunk = self.request.recv(size - len(data))
                    if not chunk:
                        return None
                    data.extend(chunk)
                return bytes(data)

            def _read_ws_frame(self) -> tuple[int, bytes] | None:
                header = self._recv_exact(2)
                if header is None:
                    return None

                opcode = header[0] & 0x0F
                masked = bool(header[1] & 0x80)
                payload_size = header[1] & 0x7F
                if payload_size == 126:
                    extended_size = self._recv_exact(2)
                    if extended_size is None:
                        return None
                    payload_size = int.from_bytes(extended_size, "big")
                elif payload_size == 127:
                    extended_size = self._recv_exact(8)
                    if extended_size is None:
                        return None
                    payload_size = int.from_bytes(extended_size, "big")

                mask = self._recv_exact(4) if masked else None
                if mask is None:
                    return None

                payload = self._recv_exact(payload_size)
                if payload is None:
                    return None
                return opcode, bytes(
                    byte ^ mask[index % len(mask)] for index, byte in enumerate(payload)
                )

            def handle(self) -> None:
                request = self._read_http_request()
                if request is None:
                    return

                key_match = re.search(rb"Sec-WebSocket-Key:\s*(\S+)", request, re.IGNORECASE)
                if key_match is None:
                    return
                accept = _ws_accept_key(key_match.group(1).decode())

                response = (
                    "HTTP/1.1 101 Switching Protocols\r\n"
                    "Upgrade: websocket\r\n"
                    "Connection: Upgrade\r\n"
                    f"Sec-WebSocket-Accept: {accept}\r\n"
                    "\r\n"
                )
                self.request.sendall(response.encode())

                with stub._lock:
                    routes = stub._routes
                    users = stub._users
                envelope = {
                    "type": "snapshot",
                    "routes_snapshot": {
                        "table_id": "pytest-stub",
                        "version": 1,
                        "routes": {
                            str(priority): {
                                "uri": rule.uri,
                                "sip_address": rule.sip_address,
                                "port": rule.port,
                                "codec": rule.codec,
                            }
                            for priority, rule in routes.items()
                        },
                    },
                    "users_snapshot": {
                        "users": [
                            {
                                "username": user.username,
                                "realm": user.realm,
                                "ha1": user.ha1,
                                "enabled": user.enabled,
                            }
                            for user in users
                        ],
                    },
                }
                self.request.sendall(_ws_text_frame(json.dumps(envelope).encode()))

                self.request.settimeout(None)
                try:
                    while frame := self._read_ws_frame():
                        opcode, payload = frame
                        if opcode == 0x8:
                            self.request.sendall(_ws_frame(payload, opcode=0x8))
                            break
                        if opcode == 0x9:
                            self.request.sendall(_ws_frame(payload, opcode=0xA))
                            continue
                        if opcode != 0x1:
                            continue

                        try:
                            envelope = json.loads(payload)
                        except (json.JSONDecodeError, UnicodeDecodeError):
                            continue

                        message_type = envelope.get("type")
                        if message_type not in {
                            "registration",
                            "call_started",
                            "call_updated",
                            "call_terminated",
                        } or not envelope.get(message_type):
                            continue

                        ack = json.dumps(
                            {"type": "ack", "success": True}, separators=(",", ":")
                        ).encode()
                        self.request.sendall(_ws_text_frame(ack))
                except OSError:
                    pass

            def _read_http_request(self) -> bytes | None:
                data = b""
                self.request.settimeout(5)
                try:
                    while b"\r\n\r\n" not in data:
                        chunk = self.request.recv(4096)
                        if not chunk:
                            return None
                        data += chunk
                except socket.timeout:
                    return None
                return data

        class Server(socketserver.ThreadingMixIn, socketserver.TCPServer):
            daemon_threads = True
            allow_reuse_address = True

        self._server = Server(("127.0.0.1", 0), Handler)
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)
        self._thread.start()

    @property
    def url(self) -> str:
        host, port = self._server.server_address
        return f"ws://{host}:{port}/ws/engine"

    def set_routes(self, routes: dict[int, RouteRule]) -> None:
        with self._lock:
            self._routes = dict(routes)

    def set_users(self, users: list[SipUserFixture]) -> None:
        with self._lock:
            self._users = list(users)

    def stop(self) -> None:
        self._server.shutdown()
        self._server.server_close()
        self._thread.join(timeout=5)


@dataclass
class EngineHandle:
    # None under --live-engine, where this suite doesn't own the process.
    process: subprocess.Popen[str] | None
    log_lines: list[str] = field(default_factory=list)
    log_start: int = 0

    def has_error_logs(self) -> bool:
        return any(_ERROR_LOG_PATTERN.search(line) for line in self.log_lines[self.log_start :])

    def log_tail(self, count: int = 40) -> str:
        return "\n".join(self.log_lines[max(self.log_start, len(self.log_lines) - count) :])


def _drain_stdout(process: subprocess.Popen[str], sink: list[str], show_logs: bool) -> None:
    assert process.stdout is not None
    for line in process.stdout:
        log_line = line.rstrip("\n")
        sink.append(log_line)
        if show_logs:
            _log.info("[engine] %s", log_line)


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption(
        "--show-engine-logs",
        action="store_true",
        default=False,
        help="Show the spawned SbcEngine's logs live while retaining them for test assertions.",
    )
    parser.addoption(
        "--live-engine",
        action="store_true",
        default=False,
        help=(
            "Assume a real SbcEngine and Backend are already running "
            "externally (e.g. in another terminal, so you can watch their "
            "own logs) instead of having this suite spawn and own an "
            "engine process. `sip_port` must match that engine's actual "
            "SIP port, and its routes/users must already be configured "
            "through the real Backend -- the per-test `initial_routes` / "
            "`sip_users` seeding has no effect in this mode. Log-based "
            "assertions (`has_error_logs`/`log_tail`) trivially pass since "
            "there is nothing captured to check."
        ),
    )


@pytest.fixture(scope="session")
def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


@pytest.fixture(scope="session")
def engine_binary(repo_root: Path) -> Path:
    binary = repo_root / "build" / "SbcEngine"
    if not binary.is_file():
        pytest.fail(f"SbcEngine binary not found at {binary}; run `just build-dev` first")
    return binary


@pytest.fixture(scope="session")
def routes_stub_server():
    server = RoutesStubServer()
    yield server
    server.stop()


@pytest.fixture(scope="session")
def sip_port() -> int:
    return 5060


@pytest.fixture(scope="session")
def initial_routes() -> dict[int, RouteRule]:
    return {}


@pytest.fixture(scope="session")
def sip_users() -> list[SipUserFixture]:
    return []


@pytest.fixture(scope="session")
def sbc_engine(request, tmp_path_factory, routes_stub_server, sip_port, initial_routes, sip_users):
    settings_overrides = getattr(request, "param", {})
    if request.config.getoption("--live-engine"):
        if settings_overrides:
            pytest.skip("settings overrides require an owned engine with controlled settings and captured logs")
        _log.info(
            "--live-engine set: assuming SbcEngine + Backend are already running "
            "on port %d; skipping engine startup and route/user seeding "
            "(check the engine's own terminal for its logs)",
            sip_port,
        )
        yield EngineHandle(process=None)
        return

    engine_binary = request.getfixturevalue("engine_binary")
    routes_stub_server.set_routes(initial_routes)
    routes_stub_server.set_users(sip_users)

    work_dir = tmp_path_factory.mktemp("sbc_engine")
    settings = {
        "sip": {"address": "127.0.0.1", "port": sip_port},
        "control_plane": {"ws_url": routes_stub_server.url},
        "registrar": {"min_expires_s": 60, "max_expires_s": 120},
    }
    for section, overrides in settings_overrides.items():
        settings.setdefault(section, {}).update(overrides)
    (work_dir / "settings.toml").write_text(
        "\n".join(
            f"[{section}]\n" + "".join(f"{key} = {json.dumps(value, ensure_ascii=False)}\n" for key, value in values.items())
            for section, values in settings.items()
        )
    )

    _log.info("starting SbcEngine...")
    process = subprocess.Popen(
        [str(engine_binary)],
        cwd=work_dir,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    handle = EngineHandle(process=process)
    show_engine_logs = request.config.getoption("--show-engine-logs")
    reader = threading.Thread(
        target=_drain_stdout,
        args=(process, handle.log_lines, show_engine_logs),
        daemon=True,
    )
    reader.start()

    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        if process.poll() is not None:
            pytest.fail(f"SbcEngine exited early ({process.returncode}):\n{handle.log_tail()}")
        if any(_ROUTES_LOADED_MARKER in line for line in handle.log_lines):
            break
        time.sleep(0.1)
    else:
        process.terminate()
        pytest.fail(f"SbcEngine did not become ready within 15s:\n{handle.log_tail()}")

    # Route load and the SIP transport bind both happen in SbcApp::init(),
    # right after each other - a short buffer avoids racing pjsip's bind.
    time.sleep(0.5)
    _log.info("SbcEngine ready")

    yield handle

    _log.info("stopping SbcEngine...")
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
    reader.join(timeout=5)
