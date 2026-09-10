from __future__ import annotations

import shutil
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import pytest
from jinja2 import Environment, FileSystemLoader

from ..conftest import RouteRule

_TEMPLATES_DIR = Path(__file__).parent / "templates"
_PCAP_PATH = Path(__file__).parent / "g711a.pcap"


@dataclass(frozen=True)
class Ports:
    local_ip: str = "127.0.0.1"
    sbc_port: int = 5060
    callee_port: int = 5061
    caller_port: int = 5062
    callee_media_port: int = 6006
    caller_media_port: int = 6002


@pytest.fixture(scope="session")
def ports() -> Ports:
    return Ports()


@pytest.fixture(scope="session")
def sip_port(ports: Ports) -> int:
    return ports.sbc_port


@pytest.fixture(scope="session")
def initial_routes(ports: Ports) -> dict[int, RouteRule]:
    # SIPp's [service] macro defaults to the literal "service", so this is
    # the exact request-URI the caller scenario will send.
    return {
        2: RouteRule(
            uri=f"sip:service@{ports.local_ip}:{ports.sbc_port}",
            sip_address=ports.local_ip,
            port=ports.callee_port,
        ),
    }


@pytest.fixture(scope="session")
def sipp_binary() -> Path:
    resolved = shutil.which("sipp")
    if resolved is None:
        pytest.skip("sipp is not installed or not on PATH")

    sipp_path = Path(resolved).resolve()
    caps = subprocess.run(["getcap", str(sipp_path)], capture_output=True, text=True, check=False).stdout
    if "cap_net_raw" not in caps:
        pytest.skip(
            "sipp needs CAP_NET_RAW to replay pcap audio; run once: "
            f"sudo setcap cap_net_raw+ep {sipp_path}"
        )
    return sipp_path


@pytest.fixture(scope="session")
def _jinja_env() -> Environment:
    return Environment(loader=FileSystemLoader(_TEMPLATES_DIR), keep_trailing_newline=True)


@pytest.fixture
def render_scenario(_jinja_env: Environment, tmp_path: Path) -> Callable[..., Path]:
    def _render(template_name: str, **params: object) -> Path:
        rendered = _jinja_env.get_template(template_name).render(pcap_path=_PCAP_PATH, **params)
        out_path = tmp_path / template_name.removesuffix(".j2")
        out_path.write_text(rendered)
        return out_path

    return _render


def _run_sipp(sipp_binary: Path, scenario: Path, target: str | None, local_ip: str, local_port: int, media_port: int):
    args = [str(sipp_binary), "-sf", str(scenario)]
    if target is not None:
        args.append(target)
    args += ["-i", local_ip, "-p", str(local_port), "-mp", str(media_port), "-m", "1", "-nostdin"]
    return subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def _stop(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


@pytest.fixture
def run_sipp_pair(sipp_binary: Path, sbc_engine, ports: Ports) -> Callable[[Path, Path], None]:
    def _run(caller_scenario: Path, callee_scenario: Path) -> None:
        callee = _run_sipp(
            sipp_binary, callee_scenario, None, ports.local_ip, ports.callee_port, ports.callee_media_port
        )
        # Give the callee time to bind before the caller dials it; matches
        # the previous run_test.py orchestration.
        time.sleep(1)
        if callee.poll() is not None:
            output, _ = callee.communicate()
            pytest.fail(f"callee sipp exited early ({callee.returncode}):\n{output}")

        caller = _run_sipp(
            sipp_binary,
            caller_scenario,
            f"{ports.local_ip}:{ports.sbc_port}",
            ports.local_ip,
            ports.caller_port,
            ports.caller_media_port,
        )
        try:
            caller_output, _ = caller.communicate(timeout=60)
            callee_output, _ = callee.communicate(timeout=60)
        finally:
            _stop(caller)
            _stop(callee)

        if caller.returncode != 0:
            pytest.fail(f"caller sipp failed ({caller.returncode}):\n{caller_output}")
        if callee.returncode != 0:
            pytest.fail(f"callee sipp failed ({callee.returncode}):\n{callee_output}")

    return _run
