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
callee_port = str(config.get("callee_port", "5061"))
callee_media_port = str(config.get("callee_media_port", "6004"))
callee_args = [
    "sipp", "-sf", "tests/b2bua/bye_after_10s/callee.xml",
    "-i", local_ip, "-p", callee_port, "-mp", callee_media_port,
    "-m", "1",
]

print(f"Starting SIPp Callee (listening on {callee_port}), waits for caller BYE...")
print(f"--> Local SIP URI: sip:sipp@{local_ip}:{callee_port}")
print(f"--> Target SIP URI for routes: sip:service@{local_ip}:{callee_port}")

result = subprocess.run(callee_args)
sys.exit(result.returncode)
