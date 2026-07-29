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

1. Start the SBC engine. Ensure its route table forwards calls to `callee_port`.
2. Start the Callee (listens for calls):
   ```bash
   python3 run_callee.py
   ```
3. Start the Caller (initiates the call):
   ```bash
   python3 run_caller.py
   ```
4. Press `Ctrl+C` to stop the scripts.
