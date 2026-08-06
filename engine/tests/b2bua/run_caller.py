#!/usr/bin/env python3
import argparse
import subprocess
import time
import sys
import os
import signal
import json

# Change to the root of the project so paths work correctly
script_dir = os.path.dirname(os.path.abspath(__file__))
engine_dir = os.path.abspath(os.path.join(script_dir, "..", ".."))
os.chdir(engine_dir)

parser = argparse.ArgumentParser(description="SIPp caller for B2BUA tests")
parser.add_argument(
    "--loop", action="store_true",
    help="Hold the call up indefinitely (Ctrl+C to hang up), instead of the "
         "default: hold for ~10s then send BYE and exit on its own.",
)
args = parser.parse_args()

with open("tests/b2bua/config.json", "r") as f:
    config = json.load(f)

local_ip = config.get("local_ip", "127.0.0.1")
sbc_port = str(config.get("sbc_port", "5060"))
caller_port = str(config.get("caller_port", "5062"))
caller_media_port = str(config.get("caller_media_port", "6002"))
scenario = "tests/b2bua/caller_loop.xml" if args.loop else "tests/b2bua/caller.xml"
caller_args = ["sipp", "-sf", scenario, f"{local_ip}:{sbc_port}", "-i", local_ip, "-p", caller_port, "-mp", caller_media_port, "-m", "1"]

print(f"Running SIPp Caller (sending to {sbc_port})...")
print(f"--> Local SIP URI: sip:sipp@{local_ip}:{caller_port}")
print(f"--> Dialing SIP URI: sip:service@{local_ip}:{sbc_port}")

if args.loop:
    print("\n*** SIPp is now running in the foreground. ***")
    print("*** It will show a UI attempting to place the call. ***")
    print("*** Press Ctrl+C to exit and clean up... ***\n")

    caller_process = subprocess.Popen(caller_args)
    try:
        # Wait indefinitely until user hits Ctrl+C
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nCtrl+C detected! Cleaning up SIPp process...")
        caller_process.send_signal(signal.SIGINT)
        caller_process.wait()
        print("Cleanup complete.")
        sys.exit(0)
else:
    print("*** Call holds for ~10s then caller sends BYE. No Ctrl+C needed. ***")
    try:
        result = subprocess.run(caller_args)
        sys.exit(result.returncode)
    except KeyboardInterrupt:
        sys.exit(1)
