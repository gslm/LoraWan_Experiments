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

## conduit-lora-mode.sh

Switches the gateway's LoRa mode:

- **server**: the built-in MultiTech network server handles joins and ADR (factory setup).
- **forwarder**: the gateway only relays packets (Semtech UDP) to an external network
  server, by default our ChirpStack on the workstation.

It runs from the workstation over SSH; nothing is copied to the gateway. `status` is
read-only. `forwarder` and `server` back up `db.json`, verify the change, then save and
reboot the gateway (2-3 min).

```bash
# workstation
cd /media/guilhermes/ssd/LoraWan_Experiments/gateway
ssh admin@192.168.28.28 'sh -s status' < conduit-lora-mode.sh
```

Switch to packet forwarder (defaults: `192.168.28.86`, ports 1700/1700, sub-band 2):

```bash
# workstation
cd /media/guilhermes/ssd/LoraWan_Experiments/gateway
ssh admin@192.168.28.28 'sh -s forwarder' < conduit-lora-mode.sh
```

Other values are passed as variables, e.g. `'NS_ADDR=192.168.28.50 sh -s forwarder'`
(all settings are listed in the script header).

Undo, back to the built-in network server:

```bash
# workstation
cd /media/guilhermes/ssd/LoraWan_Experiments/gateway
ssh admin@192.168.28.28 'sh -s server' < conduit-lora-mode.sh
```

Notes:

- `server` only flips the mode back; the forwarder settings stay, unused.
- Sub-band 2 = radios at 917.2 and 918.0 MHz = channels 8-15 + 65, the same channels as
  ChirpStack's `au915_1` (ChirpStack counts from 0, MultiTech from 1).
- After every switch, run `status` and check the radio frequencies actually in use.
- The gateway IP comes from DHCP and may change.

## watch-board.py

Read-only live view of what the gateway receives from our board: one line per uplink
(frame counter, frequency, channel, data rate, RSSI, SNR, ADR bits), plus the last ADR
command (LinkADRReq) the network server sent. Works in **built-in network server** mode
only; in packet forwarder mode, watch ChirpStack instead.

```bash
# workstation
cd /media/guilhermes/ssd/LoraWan_Experiments/gateway
scp watch-board.py admin@192.168.28.28:/tmp/
```

```sh
# gateway (SSH terminal)
cd /tmp
python /tmp/watch-board.py
```

Ctrl-C stops it. `/tmp` is wiped on reboot, so copy it again after one. Another device:
`python /tmp/watch-board.py <dev-eui>` (format `00-80-e1-15-05-30-f7-17`). To check the board
side, compare the frame counter (FCnt) with the `[UL]` lines on its serial trace.
