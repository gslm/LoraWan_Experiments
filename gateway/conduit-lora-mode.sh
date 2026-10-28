#!/bin/sh
# Switch the Conduit's LoRa mode between its built-in network server and packet
# forwarder (Semtech UDP to an external network server, e.g. our ChirpStack).
# Runs ON the gateway. "forwarder" and "server" save the config and REBOOT the gateway.
# Target: MultiTech Conduit MTCDT-210A, AEP 1.7.4 (uses curl + python2, both on the gateway).
#
# Usage, from the workstation (nothing to copy, the script is piped over SSH):
#   ssh admin@<gw> 'sh -s status'    < conduit-lora-mode.sh   show current mode (read-only)
#   ssh admin@<gw> 'sh -s forwarder' < conduit-lora-mode.sh   packet forwarder to NS_ADDR
#   ssh admin@<gw> 'sh -s server'    < conduit-lora-mode.sh   back to built-in network server (undo)
#
# Settings for "forwarder"; override as env vars: ssh admin@<gw> 'NS_ADDR=1.2.3.4 sh -s forwarder' < ...
NS_ADDR=${NS_ADDR:-192.168.28.86}   # network server that receives the packets
UP_PORT=${UP_PORT:-1700}            # Semtech UDP port, uplink
DOWN_PORT=${DOWN_PORT:-1700}        # Semtech UDP port, downlink
SUB_BAND=${SUB_BAND:-2}             # AU915/US915 sub-band, 1-based: 2 = channels 8-15 + 65
GW_USER=${GW_USER:-admin}
GW_PASS=${GW_PASS:-admin}
API=https://127.0.0.1/api
export NS_ADDR UP_PORT DOWN_PORT SUB_BAND

fail() {
    echo "ABORT: $1 (nothing saved; staged changes are dropped on the next reboot)"
    exit 1
}

# PUT a JSON file to an API path; succeed only on code 200.
put() {
    curl -sk -X PUT -H 'Expect:' -H 'Content-Type: application/json' \
        --data @"$2" "$API/$1?token=$TOKEN" | grep -q '"code" : 200'
}

status() {
    python -c "
import json
l = json.load(open('/var/config/db.json'))['loraNetwork']
p = l['packetForwarder']
print('Saved mode:       %s' % ('PACKET FORWARDER' if l['lora']['packetForwarderMode'] else 'NETWORK SERVER'))
print('Forwarder config: server=%s up=%s down=%s sub-band=%s' % (p['serverAddress'], p['upstreamPort'], p['downstreamPort'], p['frequencySubBand']))
"
    python -c "
import json, re
d = json.loads(re.sub(r'//.*', '', open('/var/run/lora/1/global_conf.json').read()))
g, sx = d['gateway_conf'], d['SX1301_conf']
print('Running:          sends to %s:%s/%s, radios at %.1f and %.1f MHz' % (g['server_address'], g['serv_port_up'], g['serv_port_down'], sx['radio_0']['freq'] / 1e6, sx['radio_1']['freq'] / 1e6))
"
    if ps axo args | grep -q '^/opt/lora/lora-network-server'; then
        echo "Built-in network server: running"
    else
        echo "Built-in network server: not running"
    fi
    if netstat -uln | grep -q ':67 '; then
        echo "WARNING: something is listening on UDP 67 (DHCP server)"
    fi
}

set_mode() {
    TOKEN=$(curl -sk "$API/login?username=$GW_USER&password=$GW_PASS" |
        python -c "import json,sys;print(json.load(sys.stdin)['result']['token'])") || fail "login failed"
    cp /var/config/db.json "/var/config/db.json.bak-lora-$(date +%Y%m%d-%H%M%S)" || fail "could not back up db.json"

    # 1. Forwarder settings (only used in packet forwarder mode).
    if [ "$1" = "forwarder" ]; then
        curl -sk "$API/loraNetwork/packetForwarder?token=$TOKEN" |
            python -c "import json,os,sys;r=json.load(sys.stdin)['result'];r.update(serverAddress=os.environ['NS_ADDR'],upstreamPort=int(os.environ['UP_PORT']),downstreamPort=int(os.environ['DOWN_PORT']),frequencySubBand=int(os.environ['SUB_BAND']));json.dump(r,open('/tmp/pf.json','w'))" ||
            fail "could not read forwarder settings"
        put loraNetwork/packetForwarder /tmp/pf.json || fail "could not write forwarder settings"
    fi

    # 2. Mode switch.
    PF=$([ "$1" = "forwarder" ] && echo True || echo False)
    curl -sk "$API/loraNetwork/lora?token=$TOKEN" |
        python -c "import json,sys;r=json.load(sys.stdin)['result'];r['packetForwarderMode']=$PF;json.dump(r,open('/tmp/lora.json','w'))" ||
        fail "could not read LoRa settings"
    put loraNetwork/lora /tmp/lora.json || fail "could not write LoRa mode"

    # 3. Verify before saving.
    curl -sk "$API/loraNetwork/lora?token=$TOKEN" |
        python -c "import json,sys;sys.exit(json.load(sys.stdin)['result']['packetForwarderMode'] is not $PF)" ||
        fail "LoRa mode read-back does not match"
    if [ "$1" = "forwarder" ]; then
        curl -sk "$API/loraNetwork/packetForwarder?token=$TOKEN" |
            python -c "import json,os,sys;r=json.load(sys.stdin)['result'];sys.exit(not (r['serverAddress']==os.environ['NS_ADDR'] and r['upstreamPort']==int(os.environ['UP_PORT']) and r['downstreamPort']==int(os.environ['DOWN_PORT']) and r['frequencySubBand']==int(os.environ['SUB_BAND'])))" ||
            fail "forwarder settings read-back does not match"
        echo "Verified: packet forwarder to $NS_ADDR:$UP_PORT/$DOWN_PORT, sub-band $SUB_BAND"
    else
        echo "Verified: built-in network server"
    fi

    # 4. Save and reboot (AEP 1.7.4 has no save_apply; the POST needs a body, even empty).
    curl -sk -X POST -d '' "$API/command/save_restart?token=$TOKEN" | grep -q '"code" : 200' ||
        fail "save_restart failed"
    echo "Saved. Gateway is rebooting (2-3 min). Then run: sh -s status"
}

case "$1" in
    status) status ;;
    forwarder|server) set_mode "$1" ;;
    *) echo "usage: sh -s status|forwarder|server  (see header)"; exit 1 ;;
esac
