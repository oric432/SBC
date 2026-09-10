"""Shared fixtures for engine integration test suites.

Anything here is generic to "start a real SbcEngine and talk to it" -
suite-specific behavior (SIP scenarios, route content, ...) belongs in that
suite's own conftest.py.
"""

from __future__ import annotations

import json
import logging
import re
import subprocess
import threading
import time
from dataclasses import dataclass, field
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

import pytest

_log = logging.getLogger("integration")

# The engine retries its route fetch forever on connection failure rather than
# crashing (see RoutesManager::fetch_routes_snapshot), so a missing/late stub
# route hangs the fixture instead of failing fast - this log line only
# appears once the fetch actually succeeds, right before the SIP transport
# binds, so it is the one reliable "engine is ready" signal available.
_ROUTES_LOADED_MARKER = "loaded routing table"
_ERROR_LOG_PATTERN = re.compile(r"\]\s*\[(error|critical)\]\s*\[")


@dataclass(frozen=True)
class RouteRule:
    uri: str
    sip_address: str
    port: int
    codec: str | None = None


class RoutesStubServer:
    """Stands in for the control-plane's `GET /api/b2bua/routes` endpoint.

    Serves whatever snapshot was last set via `set_routes`, matching the
    `ApiResponse<SipRouteSnapshot>` JSON contract the engine's
    RoutesManager parses (see src/protocols/{Api,SipRoutes}.hpp).
    """

    def __init__(self) -> None:
        self._routes: dict[int, RouteRule] = {}
        self._lock = threading.Lock()
        stub = self

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self) -> None:  # noqa: N802 (BaseHTTPRequestHandler API)
                if self.path != "/api/b2bua/routes":
                    self.send_response(404)
                    self.end_headers()
                    return

                with stub._lock:
                    routes = stub._routes

                payload = {
                    "success": True,
                    "data": {
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
                }
                body = json.dumps(payload).encode()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, format: str, *args: object) -> None:  # noqa: A002
                pass

        self._httpd = HTTPServer(("127.0.0.1", 0), Handler)
        self._thread = threading.Thread(target=self._httpd.serve_forever, daemon=True)
        self._thread.start()

    @property
    def url(self) -> str:
        host, port = self._httpd.server_address
        return f"http://{host}:{port}"

    def set_routes(self, routes: dict[int, RouteRule]) -> None:
        with self._lock:
            self._routes = dict(routes)

    def stop(self) -> None:
        self._httpd.shutdown()
        self._httpd.server_close()
        self._thread.join(timeout=5)


@dataclass
class EngineHandle:
    process: subprocess.Popen[str]
    log_lines: list[str] = field(default_factory=list)

    def has_error_logs(self) -> bool:
        return any(_ERROR_LOG_PATTERN.search(line) for line in self.log_lines)

    def log_tail(self, count: int = 40) -> str:
        return "\n".join(self.log_lines[-count:])


def _drain_stdout(process: subprocess.Popen[str], sink: list[str]) -> None:
    assert process.stdout is not None
    for line in process.stdout:
        sink.append(line.rstrip("\n"))


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
def sbc_engine(tmp_path_factory, engine_binary, routes_stub_server, sip_port, initial_routes):
    routes_stub_server.set_routes(initial_routes)

    work_dir = tmp_path_factory.mktemp("sbc_engine")
    (work_dir / "settings.toml").write_text(
        f'[sip]\naddress = "127.0.0.1"\nport = {sip_port}\n\n'
        f'[control_plane]\nhttp_url = "{routes_stub_server.url}"\n'
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
    reader = threading.Thread(target=_drain_stdout, args=(process, handle.log_lines), daemon=True)
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
