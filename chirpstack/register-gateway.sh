#!/bin/sh
# Register our gateway in the local ChirpStack, or remove it again.
# Runs on the workstation; uses provision.py (ChirpStack REST API, localhost:8090).
#
# Usage:
#   ./register-gateway.sh status    list the gateways ChirpStack knows (read-only)
#   ./register-gateway.sh add       register the gateway (safe to re-run)
#   ./register-gateway.sh remove    delete it from ChirpStack (undo)
#
# Settings; override as env vars, e.g. GW_EUI=0080000000001234 ./register-gateway.sh add
GW_EUI=${GW_EUI:-00800000a00006dd}   # must match the EUI the gateway's packet forwarder sends
GW_NAME=${GW_NAME:-conduit-bench}

cd "$(dirname "$0")" || exit 1

case "$1" in
    status) ./provision.py --show | sed -n '/^gateways/,/^$/p' ;;
    add)    ./provision.py --gateway-eui "$GW_EUI" --gateway-name "$GW_NAME" ;;
    remove) ./provision.py --delete --gateway-eui "$GW_EUI" ;;
    *)      echo "usage: ./register-gateway.sh status|add|remove  (see header)"; exit 1 ;;
esac
