# B2BUA SIPp Tests

Integration tests for the SBC's B2BUA functionality using SIPp.


## Configuration

Edit `config.json` to configure the scenarios:

- `local_ip`: IP for SIPp to bind to.
- `sbc_port`: Port of the SBC to send calls to.
- `callee_port`: Port the Callee listens on.
- `caller_port`: Port the Caller uses.
- `callee_media_port`: Base RTP port used by the Callee.
- `caller_media_port`: Base RTP port used by the Caller.

SIPp reserves two RTP sockets for each configured media base port: the base
port for audio and `base + 2` for video, even though these scenarios advertise
only audio. Keep the Caller and Callee base ports at least four ports apart to
avoid a bind collision. For example, the values in `config-example.json`
reserve `6002`/`6004` for the Caller and `6006`/`6008` for the Callee. A
collision is reported by SIPp as `Unable to bind video RTP socket`.

RTP (using `g711a.pcap`) is continuously streamed independently by both Caller and Callee at realistic G.711 packet pacing (~30ms per packet).

### SIPp raw-socket permission

The scenarios use SIPp's `play_pcap_audio` action. Replaying packets from a
PCAP requires permission to create raw network sockets. Grant only that
capability to the installed SIPp executable once:

```bash
SIPP_BIN="$(readlink -f "$(command -v sipp)")"
sudo setcap cap_net_raw+ep "$SIPP_BIN"
getcap "$SIPP_BIN"
```

The final command should report `cap_net_raw=ep`. Without this capability,
SIPp fails when media playback begins with `Can't create raw IPv4 socket`.
The `just test-b2bua` recipe checks this prerequisite before starting either
SIPp process and prints the setup command when it is missing. Running the
entire test as root is unnecessary.

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

### Loop mode: long-lived call, manual teardown

In this mode, audio loops continuously and neither scenario sends `BYE`
automatically.

1. Start the SBC engine.
2. From the engine directory, start the test:
   ```bash
   just test-b2bua --loop
   ```
3. Press `Ctrl+C` to stop both SIPp processes.
