#!/usr/bin/env python3
"""Capture the NUCLEO-WL55JC ST-LINK VCP log for a fixed time.

usage: capture_vcp.py <port> <seconds> <outfile> [baud]

Writes the raw text (CRLF normalised) to <outfile>; each line is prefixed with the
host time offset in seconds so device time and host time can be correlated.
Only opens the port given on the command line (the lab rule: /dev/ttyACM2 only).
"""
import sys
import time

import serial  # pyserial


def main() -> int:
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    port, seconds, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
    baud = int(sys.argv[4]) if len(sys.argv) > 4 else 115200
    t0 = time.monotonic()
    buf = b""
    with serial.Serial(port, baud, timeout=0.2) as ser, open(out, "w", encoding="utf-8", errors="replace") as f:
        while time.monotonic() - t0 < seconds:
            chunk = ser.read(4096)
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.rstrip(b"\r").decode("utf-8", errors="replace")
                f.write(f"[host +{time.monotonic() - t0:7.3f}] {text}\n")
                f.flush()
        if buf:
            f.write(f"[host +{time.monotonic() - t0:7.3f}] {buf.decode('utf-8', errors='replace')}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
