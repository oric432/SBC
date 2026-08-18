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
    sbc_port: str
    caller_port: str
    caller_media_port: str
    call_loop: bool


def main() -> int:
    engine_dir = Path(__file__).resolve().parents[2]
    os.chdir(engine_dir)

    with (engine_dir / "tests/b2bua/config.json").open(encoding="utf-8") as file:
        config = cast(Config, json.load(file))

    local_ip = config.get("local_ip", "127.0.0.1")
    sbc_port = config.get("sbc_port", "5060")
    caller_port = config.get("caller_port", "5062")
    caller_media_port = config.get("caller_media_port", "6002")
    loop = config.get("call_loop", False)
    scenario = "tests/b2bua/caller_loop.xml" if loop else "tests/b2bua/caller.xml"
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

    if loop:
        print("\n*** SIPp is now running in the foreground. ***")
        print("*** It will show a UI attempting to place the call. ***")
        print("*** Press Ctrl+C to exit and clean up... ***\n")

        caller_process = subprocess.Popen(caller_args)
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            print("\nCtrl+C detected! Cleaning up SIPp process...")
            caller_process.send_signal(signal.SIGINT)
            caller_process.wait()
            print("Cleanup complete.")
            return 0

    print("*** Call holds for ~10s then caller sends BYE. No Ctrl+C needed. ***")
    try:
        return subprocess.run(caller_args, check=False).returncode
    except KeyboardInterrupt:
        return 1


if __name__ == "__main__":
    sys.exit(main())
