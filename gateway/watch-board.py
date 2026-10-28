#!/usr/bin/env python
"""Show, live, every uplink the gateway receives from our board, and the ADR
command the built-in network server last sent it. Read-only.

Runs ON the gateway (MultiTech Conduit, AEP 1.7.4, python 2.7), in built-in
network server mode only. In packet forwarder mode, watch ChirpStack instead.

  scp watch-board.py admin@192.168.28.28:/tmp/     (workstation; /tmp is wiped on reboot)
  python /tmp/watch-board.py                        (gateway SSH terminal; Ctrl-C to stop)
  python /tmp/watch-board.py <dev-eui>              (another device, e.g. 00-80-e1-15-05-30-f7-17)
"""
import json
import sys
import time
import urllib2

DEV_EUI = sys.argv[1].lower() if len(sys.argv) > 1 else "00-80-e1-15-05-30-f7-17"
API = "http://127.0.0.1/api/lora/packets/up"  # local API, no login needed on the gateway itself
LOG = "/var/log/messages"                      # network server syslog, holds the MAC commands
POLL_S = 5
SILENT_WARN_S = 60                             # the board sends every 15 s

# AU915 uplink data rates
DATA_RATES = {"SF12BW125": 0, "SF11BW125": 1, "SF10BW125": 2, "SF9BW125": 3,
              "SF8BW125": 4, "SF7BW125": 5, "SF8BW500": 6}


def latest_uplink():
    """Newest uplink from DEV_EUI (the API lists the last ~100, newest first)."""
    packets = json.load(urllib2.urlopen(API, timeout=5))["result"]
    for p in packets:
        if p["deveui"] == DEV_EUI:
            return p
    return None


def last_mac_command():
    """Hex of the last MAC command the network server sent DEV_EUI, from syslog."""
    tag = "ED:%s|MAC-COMMAND|" % DEV_EUI
    f = open(LOG)
    f.seek(0, 2)
    f.seek(max(0, f.tell() - 200000))
    last = None
    for line in f:
        if tag in line:
            last = line.split(tag, 1)[1].strip()
    f.close()
    return last


def decode(cmd_hex):
    """Decode LinkADRReq commands (CID 0x03); anything else is shown raw."""
    b = bytearray.fromhex(cmd_hex)
    parts = []
    while len(b) >= 5 and b[0] == 0x03:
        dr, power = b[1] >> 4, b[1] & 0x0F
        eirp = "keep" if power == 0x0F else "EIRP %d dBm" % (30 - 2 * power)
        parts.append("LinkADRReq DR%d, TXPower idx %d (%s), ChMask 0x%04X cntl %d, NbTrans %d"
                     % (dr, power, eirp, b[2] | b[3] << 8, (b[4] >> 4) & 7, b[4] & 0x0F))
        b = b[5:]
    if b:
        parts.append("raw " + "".join("%02x" % x for x in b))
    return "; ".join(parts)


def uplink_line(p):
    freq = p["frequency"]
    dr = DATA_RATES.get(p["datarate"], -1)
    # LoRaWAN channel number (as in the board trace), from the AU915 channel grid
    ch = int(round((freq - 915.2) / 0.2)) if dr < 6 else 64 + int(round((freq - 915.9) / 1.6))
    fctrl = int(p["control"], 16)
    return "%s  %5d  %7.1f  %3d  DR%d %-9s  %4d  %5.1f   %d    %d" % (
        p["created_at"][11:19], int(p["counter"], 16), freq, ch, dr, p["datarate"],
        p["rssi"], p["snr"], fctrl >> 7 & 1, fctrl >> 6 & 1)


def main():
    print("Watching %s on this gateway (Ctrl-C to stop)" % DEV_EUI)
    print("UTC time   FCnt  MHz      ch   DR            RSSI  SNR   ADR  ADRACKReq")
    last_counter = last_cmd = None
    last_new = time.time()
    warned = False
    while True:
        try:
            p = latest_uplink()
            if p and p["counter"] != last_counter:
                print(uplink_line(p))
                last_counter, last_new, warned = p["counter"], time.time(), False
            elif time.time() - last_new > SILENT_WARN_S and not warned:
                print("-- no new uplink from the board for %d s" % SILENT_WARN_S)
                warned = True
            cmd = last_mac_command()
            if cmd and cmd != last_cmd:
                print("   ADR from network server: " + decode(cmd))
                last_cmd = cmd
        except (IOError, ValueError, KeyError) as e:
            print("-- read error: %s" % e)
        sys.stdout.flush()
        time.sleep(POLL_S)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("")
