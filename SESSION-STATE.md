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
- gateway/conduit-dhcp-client.sh automates this (also disables all DHCP server entries). Untested on the gateway: the /api/dhcp endpoints are unverified.
- Config backup on gateway: /var/config/db.json.bak-20261027.
- Gateway EUI 00800000A00006DD. LoRa mode: built-in network server, AU915 sub-band 2.

## Board (NUCLEO-WL55JC)
- Flashed with firmware/LoRaWAN_End_Node_ADR (ADR trace), joined the gateway's built-in network server, uplink every 15 s.
- Teammate demo backup: firmware/backup/teammate_demo_backup.bin.

## ChirpStack
- Running in Docker (chirpstack/), UI http://localhost:8080, no gateway connected yet.
- api.secret now read from $CHIRPSTACK_API_SECRET in git-ignored chirpstack/.env (same value, existing API token still valid).

## Pending
- First commit and push to GitHub (operator runs the commands).
- Later: replace the vendored chirpstack/ with a submodule + deploy scripts.
- Goal 2: simple STM32WL55 app on the board (/dev/ttyACM2) to explore ADR via the gateway.
- Primer doc: https://claude.ai/artifact/EdqZffDoKNEMSF5QFoPdse
