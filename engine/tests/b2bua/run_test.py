#!/usr/bin/env python3
import signal
import subprocess
import sys
import time
from pathlib import Path


def stop_process(process: subprocess.Popen[bytes] | None) -> None:
    if process is None or process.poll() is not None:
        return

    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def main() -> int:
    engine_dir = Path(__file__).resolve().parents[2]
    forwarded_args = sys.argv[1:]
    callee: subprocess.Popen[bytes] | None = None
    caller: subprocess.Popen[bytes] | None = None
    previous_sigterm = signal.getsignal(signal.SIGTERM)

    def handle_sigterm(_signum: int, _frame: object) -> None:
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, handle_sigterm)
    try:
        callee = subprocess.Popen(
            [sys.executable, "tests/b2bua/run_callee.py", *forwarded_args],
            cwd=engine_dir,
        )

        # Give SIPp time to bind, but do not start the caller if the callee
        # already failed (for example, because its address is in use).
        time.sleep(1)
        if callee.poll() is not None:
            return callee.returncode

        caller = subprocess.Popen(
            [sys.executable, "tests/b2bua/run_caller.py", *forwarded_args],
            cwd=engine_dir,
        )
        caller_status = caller.wait()
        if caller_status != 0:
            return caller_status

        return callee.wait()
    except KeyboardInterrupt:
        print("\nInterrupted. Cleaning up B2BUA test processes...")
        return 130
    finally:
        signal.signal(signal.SIGTERM, previous_sigterm)
        stop_process(caller)
        stop_process(callee)


if __name__ == "__main__":
    sys.exit(main())
