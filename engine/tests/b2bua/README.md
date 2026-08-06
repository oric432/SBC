# B2BUA SIPp Tests

Integration tests for the SBC's B2BUA functionality using SIPp.


## Configuration

Edit `config.json` to change the test IPs and ports:
- `local_ip`: IP for SIPp to bind to.
- `sbc_port`: Port of the SBC to send calls to.
- `callee_port`: Port the Callee listens on.
- `caller_port`: Port the Caller uses.

RTP (using `g711a.pcap`) is continuously streamed independently by both Caller and Callee at realistic G.711 packet pacing (~30ms per packet).

## How to Run

### Default: short call with caller-initiated BYE

The caller holds the call up for ~10s and then hangs up itself by sending
`BYE`. Both scripts exit on their own once the scenario completes, so this
is useful for verifying the SBC tears down (and frees) the `CallSession`
for the call — check the `call` logger's `trace` output at shutdown, e.g.
with `--log-level trace`.

1. Start the SBC engine. Ensure its route table forwards calls to `callee_port`.
2. Start the Callee:
   ```bash
   python3 run_callee.py
   ```
3. Start the Caller:
   ```bash
   python3 run_caller.py
   ```
4. Both processes exit on their own after the BYE/200 OK exchange.

### `--loop`: long-lived call, manual teardown

Pass `--loop` to either script to hold the call up indefinitely instead
(the original behavior): audio loops continuously and nothing sends `BYE`
on its own.

1. Start the SBC engine.
2. Start the Callee:
   ```bash
   python3 run_callee.py --loop
   ```
3. Start the Caller:
   ```bash
   python3 run_caller.py --loop
   ```
4. Press `Ctrl+C` on each to stop.
