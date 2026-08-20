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
    sbc_port: str
    caller_port: str
    caller_media_port: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run the B2BUA SIPp caller")
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
    sbc_port = config.get("sbc_port", "5060")
    caller_port = config.get("caller_port", "5062")
    caller_media_port = config.get("caller_media_port", "6002")
    scenario = "tests/b2bua/caller_loop.xml" if args.loop else "tests/b2bua/caller.xml"
    caller_args = [
        "sipp",
        "-sf",
        scenario,
        f"{local_ip}:{sbc_port}",
        "-i",
        local_ip,
        "-p",
        caller_port,
        "-mp",
        caller_media_port,
        "-m",
        "1",
        "-nostdin",
    ]

    print(f"Running SIPp Caller (sending to {sbc_port})...")
    print(f"--> Local SIP URI: sip:sipp@{local_ip}:{caller_port}")
    print(f"--> Dialing SIP URI: sip:service@{local_ip}:{sbc_port}")

    if args.loop:
        print("\n*** SIPp is now running in the foreground. ***")
        print("*** It will show a UI attempting to place the call. ***")
        print("*** Press Ctrl+C to exit and clean up... ***\n")
    else:
        print("*** Call holds for ~10s then caller sends BYE. No Ctrl+C needed. ***")

    return run_sipp(caller_args)


if __name__ == "__main__":
    sys.exit(main())
