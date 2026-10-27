# Gateway scripts

Scripts that run on the MultiTech Conduit (MTCDT-210A, AEP 1.7.4).

## conduit-dhcp-client.sh

Puts eth0 on the office LAN as a **DHCP client** and makes sure the gateway's own
DHCP server is **disabled** on every interface. Use it after a reboot that lost the
setting, a factory reset or a firmware update.

It aborts without saving if any step fails. If everything checks out, it saves and
reboots the gateway.

### 1. Copy it to the gateway

From the serial console (`picocom -b 115200 /dev/ttyACM1` on the workstation):

```sh
# gateway serial console
cd /tmp
cat > /tmp/conduit-dhcp-client.sh
```

Paste the script contents, then press Ctrl-D.

### 2. Run it

```sh
# gateway serial console
cd /tmp
sh /tmp/conduit-dhcp-client.sh
```

If the admin login is not `admin/admin`:
`GW_USER=<user> GW_PASS=<password> sh /tmp/conduit-dhcp-client.sh`

### 3. After the reboot (2-3 min), check it

```sh
# gateway serial console
cd /tmp
ifconfig eth0 | grep 'inet addr'
netstat -uln | grep ':67 ' || echo "OK: no DHCP server listening"
```

Expected: an `inet addr` on the 28 network and `OK: no DHCP server listening`.
Anything printed by the `netstat` line means a DHCP server is running: stop and investigate.

### Undo

A config backup was taken before the first change:

```sh
# gateway serial console
cd /var/config
cp /var/config/db.json.bak-20261027 /var/config/db.json
reboot
```
