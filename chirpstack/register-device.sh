#!/bin/sh
# Register our NUCLEO-WL55JC board (OTAA) in the local ChirpStack, or remove it again.
# Runs on the workstation; uses provision.py (ChirpStack REST API, localhost:8090).
# The device goes into application "adr-lab" with device profile "ST-WL55-AU915-ADR"
# (LoRaWAN 1.0.4, RP002-1.0.3, AU915 sub-band 2, default ADR), both made by provision.py.
#
# Usage:
#   ./register-device.sh status    list the devices ChirpStack knows (read-only)
#   ./register-device.sh add       register the board (safe to re-run)
#   ./register-device.sh remove    delete it from ChirpStack (undo)
#   ./register-device.sh reset     remove + add, which clears ChirpStack's DevNonce history.
#                                  Run it before resetting the board: our firmware restarts
#                                  DevNonce at 1 on every reset.
#
# Settings (the board prints them at boot); override as env vars, e.g. DEV_EUI=... ./register-device.sh add
DEV_EUI=${DEV_EUI:-0080e1150530f717}                 # derived from the chip's unique ID
JOIN_EUI=${JOIN_EUI:-0101010101010101}               # ST example default (se-identity.h)
APP_KEY=${APP_KEY:-2b7e151628aed2a6abf7158809cf4f3c} # ST's public test key (se-identity.h)
DEV_NAME=${DEV_NAME:-nucleo-wl55}

cd "$(dirname "$0")" || exit 1

add() {
    ./provision.py --dev-eui "$DEV_EUI" --join-eui "$JOIN_EUI" --app-key "$APP_KEY" --device-name "$DEV_NAME"
}

remove() {
    ./provision.py --delete --dev-eui "$DEV_EUI"
}

case "$1" in
    status) ./provision.py --show | sed -n '/^devices in application/,$p' ;;
    add)    add ;;
    remove) remove ;;
    reset)  remove && add ;;
    *)      echo "usage: ./register-device.sh status|add|remove|reset  (see header)"; exit 1 ;;
esac
