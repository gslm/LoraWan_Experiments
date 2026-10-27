#!/bin/sh
# Set the Conduit's eth0 to DHCP client (LAN) and make sure no DHCP server is enabled.
# Runs ON the gateway (serial console). Saves the config and REBOOTS the gateway.
# Target: MultiTech Conduit MTCDT-210A, AEP 1.7.4 (uses curl + python2, both on the gateway).
#
# Usage: sh conduit-dhcp-client.sh
# Login defaults to admin/admin; override with GW_USER=... GW_PASS=...

GW_USER=${GW_USER:-admin}
GW_PASS=${GW_PASS:-admin}
API=https://127.0.0.1/api

fail() {
    echo "ABORT: $1 (nothing saved)"
    exit 1
}

# PUT a JSON file to an API path; succeed only on code 200.
put() {
    curl -sk -X PUT -H 'Expect:' -H 'Content-Type: application/json' \
        --data @"$2" "$API/$1?token=$TOKEN" | grep -q '"code" : 200'
}

TOKEN=$(curl -sk "$API/login?username=$GW_USER&password=$GW_PASS" |
    python -c "import json,sys;print(json.load(sys.stdin)['result']['token'])") || fail "login failed"

# 1. Disable every DHCP server entry that is enabled.
ON=$(curl -sk "$API/dhcp?token=$TOKEN" |
    python -c "import json,sys;d=json.load(sys.stdin)['result']['dhcps'];print(' '.join(str(i) for i,x in enumerate(d) if x['enabled']))") ||
    fail "could not read DHCP server config"
for i in $ON; do
    echo "Disabling DHCP server entry $i"
    curl -sk "$API/dhcp/dhcps/$i?token=$TOKEN" |
        python -c "import json,sys;r=json.load(sys.stdin)['result'];r['enabled']=False;json.dump(r,open('/tmp/dhcps.json','w'))" ||
        fail "could not read DHCP server entry $i"
    put "dhcp/dhcps/$i" /tmp/dhcps.json || fail "could not disable DHCP server entry $i"
done

# 2. eth0: LAN interface, DHCP client.
N=$(curl -sk "$API/ni?token=$TOKEN" |
    python -c "import json,sys;n=json.load(sys.stdin)['result']['nis'];print([x['name'] for x in n].index('eth0'))") ||
    fail "eth0 not found"
curl -sk "$API/ni/nis/$N?token=$TOKEN" |
    python -c "import json,sys;r=json.load(sys.stdin)['result'];r.pop('wan_exist',None);r['type']='LAN';r['ipv4'].update(mode='DHCP',ip='',mask='',gateway='',dns1='',dns2='');json.dump(r,open('/tmp/eth0_lan_dhcp.json','w'))" ||
    fail "could not read eth0 config"
put "ni/nis/$N" /tmp/eth0_lan_dhcp.json || fail "could not set eth0 to DHCP client"

# 3. Verify before saving: eth0 is LAN + DHCP, no DHCP server enabled.
curl -sk "$API/ni/nis/$N?token=$TOKEN" |
    python -c "import json,sys;r=json.load(sys.stdin)['result'];sys.exit(not (r['type']=='LAN' and r['ipv4']['mode']=='DHCP'))" ||
    fail "eth0 read-back is not LAN + DHCP"
curl -sk "$API/dhcp?token=$TOKEN" |
    python -c "import json,sys;d=json.load(sys.stdin)['result']['dhcps'];sys.exit(any(x['enabled'] for x in d))" ||
    fail "a DHCP server entry is still enabled"
echo "Verified: eth0 = LAN + DHCP client, all DHCP server entries disabled"

# 4. Save and reboot (AEP 1.7.4 has no save_apply; the POST needs a body, even empty).
curl -sk -X POST -d '' "$API/command/save_restart?token=$TOKEN" | grep -q '"code" : 200' ||
    fail "save_restart failed"
echo "Saved. Gateway is rebooting (2-3 min). Then run the check in README.md."
