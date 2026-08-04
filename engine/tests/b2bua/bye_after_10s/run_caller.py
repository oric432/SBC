#!/usr/bin/env python3
import subprocess
import sys
import os
import json

# Change to the root of the project so paths work correctly
script_dir = os.path.dirname(os.path.abspath(__file__))
engine_dir = os.path.abspath(os.path.join(script_dir, "..", "..", ".."))
os.chdir(engine_dir)

with open("tests/b2bua/config.json", "r") as f:
    config = json.load(f)

local_ip = config.get("local_ip", "127.0.0.1")
sbc_port = str(config.get("sbc_port", "5060"))
caller_port = str(config.get("caller_port", "5062"))
caller_media_port = str(config.get("caller_media_port", "6002"))
caller_args = [
    "sipp", "-sf", "tests/b2bua/bye_after_10s/caller.xml", f"{local_ip}:{sbc_port}",
    "-i", local_ip, "-p", caller_port, "-mp", caller_media_port,
    "-m", "1",
]

print(f"Running SIPp Caller (sending to {sbc_port})...")
print(f"--> Local SIP URI: sip:sipp@{local_ip}:{caller_port}")
print(f"--> Dialing SIP URI: sip:service@{local_ip}:{sbc_port}")
print("*** Call holds for ~10s then caller sends BYE. No Ctrl+C needed. ***")

result = subprocess.run(caller_args)
sys.exit(result.returncode)
