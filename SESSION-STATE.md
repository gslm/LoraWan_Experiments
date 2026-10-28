# Session state

Last updated: 2026-10-27

## Decisions
- Gateway (MultiTech Conduit, AEP 1.7.4) must only be a DHCP client on the office LAN (192.168.28.0/24), never a DHCP server.
- Operator runs all commands on the gateway serial console. Claude may SSH into the gateway only after it is on the 28 subnet.
- Workspace renamed from SENSE-LoraWan to LoraWan_Experiments (done 2026-10-27).
- GitHub repo: https://github.com/gslm/LoraWan_Experiments (PUBLIC, empty, created by operator).
- Primer exported (names scrubbed) to knowledge-base/lorawan-adr-lab-primer.md; the 2 widgets became text placeholders.
- .gitignore at repo root. SESSION-STATE.md and DIRECTIVES.md are versioned (gateway creds are factory defaults, IP is local only). firmware/backup/ stays on disk, not versioned.
- TEMPORARY: chirpstack/ (upstream chirpstack-docker + our changes) is committed as plain files. Its upstream .git was moved to ai-content/chirpstack-upstream.git. Target later: a submodule plus deploy scripts that assemble the working tree.
- Versioned content must never contain stakeholder names (customer or consultancy). Primer export to knowledge-base/ drops both names and the customer's certification report section.
- Stakeholder images stay unversioned in ai-content/.

## Gateway
- eth0 = LAN + DHCP client, saved and working. IP 192.168.28.28 (DHCP lease, may change), default route 192.168.28.1.
- Verified after reboot (Claude, via SSH): no DHCP server listening on UDP 67.
- SSH from workstation works: `sshpass -p admin ssh admin@192.168.28.28` (operator: `sshp admin@192.168.28.28 admin`).
- AEP 1.7.4 has no save_apply; use command/save_restart (reboots the gateway). The POST needs `-d ''` (else lighttpd 411).
- LoRa mode: still built-in NETWORK SERVER (pkt fwd -> 127.0.0.1:1780/1782, radios 917.2/918.0 MHz = sub-band 2).
- gateway/conduit-lora-mode.sh (status|forwarder|server, run via `ssh ... 'sh -s <cmd>' < script`): written, status tested; forwarder/server not run yet.
  Forwarder defaults: 192.168.28.86:1700/1700, sub-band 2 (PF section was sub-band 1, would have missed our board).
- Workstation ufw is active; Docker-published UDP 1700 is expected to bypass it (unverified, needs root to check).
- gateway/conduit-dhcp-client.sh automates this (also disables all DHCP server entries). Untested on the gateway: the /api/dhcp endpoints are unverified.
- Config backup on gateway: /var/config/db.json.bak-20261027.
- Gateway EUI 00800000A00006DD. LoRa mode: built-in network server, AU915 sub-band 2.

## Board (NUCLEO-WL55JC)
- Flashed with firmware/LoRaWAN_End_Node_ADR (ADR trace), joined the gateway's built-in network server, uplink every 15 s.
- Teammate demo backup: firmware/backup/teammate_demo_backup.bin.

## ChirpStack
- Running in Docker (chirpstack/), UI http://localhost:8080, no gateway connected yet.
- Controls: chirpstack/register-gateway.sh (status|add|remove) and register-device.sh (status|add|remove|reset), wrappers over provision.py. Only status tested; ChirpStack has 0 gateways and 0 devices.
- Board identity (firmware defaults): DevEUI 0080e1150530f717 (chip UID), JoinEUI 0101010101010101, AppKey = ST public test key.
- DevNonce restarts at 1 on every board reset (saved only on button B3). Workaround: register-device.sh reset before each board reset. Later option: firmware saves context after join.
- api.secret now read from $CHIRPSTACK_API_SECRET in git-ignored chirpstack/.env (same value, existing API token still valid).

## Pending
- END OF DAY 2026-10-27 (verified): board transmits every 15 s and the gateway receives it (built-in NS, MIC passes,
  RSSI about -73 dBm, SNR about 12 dB). MultiTech ADR has the board at DR3, TX idx 10 (7 dBm, LP amplifier).
  Gateway: 192.168.28.28, NETWORK SERVER mode, no DHCP server.
- NEXT: 1) register-gateway.sh add + register-device.sh add  2) conduit-lora-mode.sh forwarder
  3) status checks (gateway seen in ChirpStack = UDP 1700 passes ufw)  4) reset board, watch for Join-Accept.
- gateway/watch-board.py: live view of board uplinks + last LinkADRReq, on the gateway (NS mode only). Tested.
  Gateway's local API needs no token over http://127.0.0.1; /api/lora/packets/up ignores limit (returns ~100, newest first).
- knowledge-base/gateway-configuration.html (gateway setup explainer, 6 SVGs, capture example) written; lorawan-glossary.html
  (from another agent) had "TE" replaced by "Stakeholder" (6x). Both untracked. Glossary still has em-dashes/arrows (directive 9), not fixed.
- knowledge-base/lorawan-adr-lab-primer.html (faithful HTML of the .md + 5 SVGs) and stm32-firmware.html (flashing + architecture,
  10 SVGs) written. Primer's "Our bench" table still shows the morning state (DHCP staged), kept as in the .md.
- Board left running overnight (safe: gateway logs in RAM with rotation, NS packet DB trimmed to 100 rows).
- TOMORROW: STM32 code.
- FUTURE: terminal app showing board TX (ttyACM2 trace), gateway RX (SSH syslog), ChirpStack ADR (MQTT) side by side.
- Later: replace the vendored chirpstack/ with a submodule + deploy scripts.
- Goal 2: simple STM32WL55 app on the board (/dev/ttyACM2) to explore ADR via the gateway.
- Primer doc: https://claude.ai/artifact/EdqZffDoKNEMSF5QFoPdse
