# B2BUA SIPp Tests

Integration tests for the SBC's B2BUA functionality using SIPp.

## Layout

```
tests/b2bua/
├── config.json          # shared IPs/ports for all scenarios
├── g711a.pcap           # shared RTP payload for all scenarios
├── long_call/           # call held up indefinitely, manual teardown
│   ├── caller.xml
│   ├── callee.xml
│   ├── run_caller.py
│   └── run_callee.py
└── bye_after_10s/       # call held ~10s, caller sends BYE, scripts exit on their own
    ├── caller.xml
    ├── callee.xml
    ├── run_caller.py
    └── run_callee.py
```

Each scenario folder is self-contained (its own `caller.xml`/`callee.xml`
scenario pair + `run_caller.py`/`run_callee.py` runners); `config.json` and
`g711a.pcap` at the top level are shared by all of them.

## Configuration

Edit `config.json` to change the test IPs and ports:
- `local_ip`: IP for SIPp to bind to.
- `sbc_port`: Port of the SBC to send calls to.
- `callee_port`: Port the Callee listens on.
- `caller_port`: Port the Caller uses.

RTP (using `g711a.pcap`) is continuously streamed independently by both Caller and Callee at realistic G.711 packet pacing (~30ms per packet).

## How to Run

### long_call — long-lived call, manual teardown

1. Start the SBC engine. Ensure its route table forwards calls to `callee_port`.
2. Start the Callee (listens for calls):
   ```bash
   python3 long_call/run_callee.py
   ```
3. Start the Caller (initiates the call):
   ```bash
   python3 long_call/run_caller.py
   ```
4. Press `Ctrl+C` to stop the scripts.

### bye_after_10s — short call with caller-initiated BYE

Same scenario, but the call is not held indefinitely: the caller holds it up
for ~10s and then hangs up itself by sending `BYE`. Both scripts exit on
their own once the scenario completes, so this is useful for verifying the
SBC tears down (and frees) the `CallSession` for the call — check the
`call` logger's `trace` output at shutdown, e.g. with `--log-level trace`.

1. Start the SBC engine.
2. Start the Callee:
   ```bash
   python3 bye_after_10s/run_callee.py
   ```
3. Start the Caller:
   ```bash
   python3 bye_after_10s/run_caller.py
   ```
4. Both processes exit on their own after the BYE/200 OK exchange.
