#!/usr/bin/env python3
import argparse
import json
import os
import signal
import subprocess
import sys
from pathlib import Path
from typing import TypedDict, cast


class Config(TypedDict, total=False):
    local_ip: str
    callee_port: str
    callee_media_port: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run the B2BUA SIPp callee")
    parser.add_argument(
        "--loop",
        action="store_true",
        help="keep the call and RTP playback running until interrupted",
    )
    return parser.parse_args()


def run_sipp(command: list[str]) -> int:
    process = subprocess.Popen(command)
    previous_sigterm = signal.getsignal(signal.SIGTERM)

    def handle_sigterm(_signum: int, _frame: object) -> None:
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, handle_sigterm)
    try:
        return process.wait()
    except KeyboardInterrupt:
        print("\nInterrupted. Cleaning up SIPp process...")
        return 130
    finally:
        signal.signal(signal.SIGTERM, previous_sigterm)
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        print("Cleanup complete.")


def main() -> int:
    args = parse_args()
    engine_dir = Path(__file__).resolve().parents[2]
    os.chdir(engine_dir)

    with (engine_dir / "tests/b2bua/config.json").open(encoding="utf-8") as file:
        config = cast(Config, json.load(file))

    local_ip = config.get("local_ip", "127.0.0.1")
    callee_port = config.get("callee_port", "5061")
    callee_media_port = config.get("callee_media_port", "6004")
    scenario = "tests/b2bua/callee_loop.xml" if args.loop else "tests/b2bua/callee.xml"
    callee_args = [
        "sipp",
        "-sf",
        scenario,
        "-i",
        local_ip,
        "-p",
        callee_port,
        "-mp",
        callee_media_port,
        "-m",
        "1",
        "-nostdin",
    ]

    print(f"Starting SIPp Callee (listening on {callee_port})...")
    print(f"--> Local SIP URI: sip:sipp@{local_ip}:{callee_port}")
    print(f"--> Target SIP URI for routes: sip:service@{local_ip}:{callee_port}")

    if args.loop:
        print("\n*** SIPp is now running in the foreground. ***")
        print("*** Press Ctrl+C to exit and clean up... ***\n")
    else:
        print("*** Waits for the caller's BYE, then exits on its own. ***")

    return run_sipp(callee_args)


if __name__ == "__main__":
    sys.exit(main())
