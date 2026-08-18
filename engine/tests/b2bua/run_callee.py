#!/usr/bin/env python3
import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import TypedDict, cast


class Config(TypedDict, total=False):
    local_ip: str
    callee_port: str
    callee_media_port: str
    call_loop: bool


def main() -> int:
    engine_dir = Path(__file__).resolve().parents[2]
    os.chdir(engine_dir)

    with (engine_dir / "tests/b2bua/config.json").open(encoding="utf-8") as file:
        config = cast(Config, json.load(file))

    local_ip = config.get("local_ip", "127.0.0.1")
    callee_port = config.get("callee_port", "5061")
    callee_media_port = config.get("callee_media_port", "6004")
    loop = config.get("call_loop", False)
    scenario = "tests/b2bua/callee_loop.xml" if loop else "tests/b2bua/callee.xml"
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

    if loop:
        print("\n*** SIPp is now running in the foreground. ***")
        print("*** Press Ctrl+C to exit and clean up... ***\n")

        callee_process = subprocess.Popen(callee_args)
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            print("\nCtrl+C detected! Cleaning up SIPp process...")
            callee_process.send_signal(signal.SIGINT)
            callee_process.wait()
            print("Cleanup complete.")
            return 0

    print("*** Waits for the caller's BYE, then exits on its own. ***")
    try:
        return subprocess.run(callee_args, check=False).returncode
    except KeyboardInterrupt:
        return 1


if __name__ == "__main__":
    sys.exit(main())
