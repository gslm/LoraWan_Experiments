# ChirpStack v4 – SENSE ADR lab (AU915 sub-band 2)

Local ChirpStack network server used to reproduce the "ADR behaves on TTN but not on
ChirpStack" problem with the STM32WL (LoRaMac-node based ST stack) end node.
Based on upstream [chirpstack-docker](https://github.com/chirpstack/chirpstack-docker)
(upstream README kept as `README.upstream.md`); upstream's floating image tags are
kept unchanged.

| Component | Image tag (upstream pin) | Resolved on 2026-10-27 |
|---|---|---|
| ChirpStack | `chirpstack/chirpstack:4` | **4.20.0** (`sha256:6ef779ce…`) |
| Gateway Bridge (UDP + Basic Station) | `chirpstack/chirpstack-gateway-bridge:4` | `sha256:cc820a19…` (image built 2026-07-10) |
| REST API | `chirpstack/chirpstack-rest-api:4` | `sha256:51491749…` |
| PostgreSQL / Redis / Mosquitto | `postgres:14-alpine` / `redis:7-alpine` / `eclipse-mosquitto:2` | – |

> Because the tags float (`:4`), a later `docker compose pull` can bring a newer 4.x.
> To freeze the lab, replace a tag with the digest above (e.g.
> `chirpstack/chirpstack@sha256:6ef779ce1dfbef273a1c99649672170c791441b42832c8245b01110eab3a369a`).

## What differs from upstream

* `configuration/chirpstack/chirpstack.toml`: `enabled_regions = ["au915_1", "eu868"]`
  (au915_1 = AU915 **channels 8-15 + 65**, i.e. TTN "FSB 2"); `api.secret` is read from
  `$CHIRPSTACK_API_SECRET`, set in `.env` (chmod 600, git-ignored). Create it with
  `echo "CHIRPSTACK_API_SECRET=$(openssl rand -base64 32)" > .env` on a fresh clone.
* `configuration/chirpstack-gateway-bridge/chirpstack-gateway-bridge.toml`: Semtech UDP
  backend on `0.0.0.0:1700`, MQTT topic prefix **`au915_1`**, JSON marshaler (human-readable
  gateway topics). The eu868 `INTEGRATION__MQTT__*` env overrides were removed from
  `docker-compose.yml`, so this toml is the only place to configure it.
* Basic Station bridge switched to `chirpstack-gateway-bridge-basicstation-au915_1.toml`
  (LNS endpoint `ws://<host>:3001`, also au915_1).
* `configuration/chirpstack/region_au915_1.toml` is **unchanged** (upstream defaults, see the ADR knobs below).
* `provision.py` + `.api_token` (global API key, chmod 600, git-ignored).

## Start / stop

```bash
cd /media/guilhermes/ssd/LoraWan_Experiments/chirpstack
docker compose up -d            # start (restart: unless-stopped → also comes back after reboot)
docker compose ps               # all 7 services must be "Up"
docker compose logs -f chirpstack chirpstack-gateway-bridge
docker compose restart chirpstack   # after editing chirpstack.toml / region_*.toml
docker compose restart chirpstack-gateway-bridge   # after editing the bridge toml
docker compose down             # stop (data kept in volumes chirpstack_postgresqldata / chirpstack_redisdata)
docker compose down -v          # stop AND wipe database + redis (token/devices are lost; recreate both)
```

The first ChirpStack start logs a few `error connecting to server` lines while PostgreSQL is
still initialising; it restarts itself and then runs normally.

## URLs and ports

| What | URL / port | Notes |
|---|---|---|
| Web UI | http://localhost:8080 (LAN: http://192.168.28.86:8080) | login **admin / admin** (change it in the UI if the box is reachable by others) |
| REST API + Swagger | http://localhost:8090 | header `Grpc-Metadata-Authorization: Bearer $(cat .api_token)` |
| gRPC API | localhost:8080 | same port as the UI |
| Semtech UDP packet forwarder | **UDP 1700** on 192.168.28.86 | up and down port are both 1700 |
| Basic Station LNS | ws://192.168.28.86:3001 | no TLS |
| MQTT (Mosquitto) | tcp://localhost:1883 | anonymous |

192.168.28.86 is the Wi-Fi DHCP address of this workstation (`ip -4 addr show wlp0s20f3`); if it
changes, the gateway's server address must change too. The gateway must reach that address over
IPv4 (same LAN or routed). Docker publishes the ports on 0.0.0.0 and [::].

## Provisioning (API token, provision.py)

The API key `claude-provision` was created with
`docker compose exec chirpstack chirpstack -c /etc/chirpstack create-api-key --name claude-provision`
and its token stored in `.api_token`. Create another one the same way if it is lost (or after `down -v`).

Pre-provisioned in tenant **ChirpStack**:

* application **adr-lab**
* device profile **ST-WL55-AU915-ADR**: region AU915, region config `au915_1`, LoRaWAN **1.0.4**,
  RP002-**1.0.3**, OTAA, Class A, ADR algorithm `default`, uplink interval 15 s,
  DevStatusReq 1/day, flush queue on activate.

`provision.py` (python3 stdlib only) is idempotent; each run also (re)creates the application and
profile if they are missing.

For our bench, two short wrappers hold the IDs (override any of them as env vars, see each header):

```bash
# workstation
cd /media/guilhermes/ssd/LoraWan_Experiments/chirpstack
./register-gateway.sh status|add|remove          # Conduit, EUI 00800000a00006dd
./register-device.sh  status|add|remove|reset    # NUCLEO-WL55JC, DevEUI 0080e1150530f717
```

`register-device.sh reset` (remove + add) clears ChirpStack's DevNonce history. Run it before
resetting the board: our firmware restarts DevNonce at 1 on every reset (it is only saved when
button B3 is pressed).

```bash
# gateway + OTAA device (LoRaWAN 1.0.x AppKey; ChirpStack stores it in the "nwkKey" field)
./provision.py --gateway-eui 00800000a0001234 --gateway-name conduit-bench \
               --dev-eui 0080e115000a1b2c --join-eui 0000000000000000 \
               --app-key 2b7e151628aed2a6abf7158809cf4f3c --device-name wl55-1
./provision.py --show                                   # profile + gateways + devices
./provision.py --mac-version 1.0.3 --reg-params A       # firmware reports another LoRaWAN version
./provision.py --adr-algorithm default                  # default | lr_fhss | lora_lr_fhss | plugin id
./provision.py --supported-uplink-drs 2,3,4,5           # see note in the knobs table
./provision.py --delete --dev-eui 0080e115000a1b2c --gateway-eui 00800000a0001234
```

Profile flags are only applied when given, so a plain run never reverts edits done in the UI.
After changing MAC version / regional-parameters revision, rejoin the device (the session is created
at join). `--skip-fcnt-check` exists for debugging only.

The gateway **must** be registered (`[gateway] allow_unknown_gateways=false`): uplinks from an
unknown gateway EUI are dropped by ChirpStack even though the bridge ACKs them.

## Pointing a Semtech UDP packet forwarder (MultiTech Conduit) at it

* MultiTech AEP web UI → *LoRaWAN → Network Settings*: Mode **Packet Forwarder**, Network
  **Manual**, Channel plan **AU915**, Frequency sub-band **2**, Server address **192.168.28.86**,
  Upstream port **1700**, Downstream port **1700**. Save and restart LoRaWAN.
* Plain `global_conf.json` / `local_conf.json` (`gateway_conf` section):

  ```json
  "gateway_ID": "<16 hex chars of the LoRa card EUI>",
  "server_address": "192.168.28.86",
  "serv_port_up": 1700,
  "serv_port_down": 1700,
  "keepalive_interval": 10,
  "stat_interval": 30,
  "push_timeout_ms": 100
  ```
  Radio plan for FSB2: 125 kHz channels 916.8, 917.0 … 918.2 MHz (ch 8-15) + 500 kHz 917.5 MHz (ch 65); RX2 923.3 MHz.
* Register the EUI the forwarder reports: `./provision.py --gateway-eui <EUI> --gateway-name conduit`.
* Verify: `docker compose logs -f chirpstack-gateway-bridge` shows
  `subscribing to topic ... au915_1/gateway/<EUI>/command/#` and `publishing event ... event=stats`;
  the UI gateway turns *online*. A PULL_DATA/PUSH_DATA test from this host was ACKed and reached
  ChirpStack region `au915_1` (fake gateway + fake join-request, since deleted).

## Watching LinkADRReq / LinkADRAns

* **UI → Tenant → Applications → adr-lab → device → *LoRaWAN frames***: every uplink/downlink
  PHYPayload decoded. LinkADRReq (DR, TxPower, ChMask, ChMaskCntl, NbTrans) is in downlink `fOpts`
  (or FRMPayload with FPort 0); the LinkADRAns ACK bits (channel-mask / data-rate / power) are in
  the next uplink's `fOpts`. *Events* tab: `up` events show `dr`, `adr` bit, per-gateway `rssi`/`snr`;
  `log` events show errors. The gateway page has its own *LoRaWAN frames* tab (raw, before dedup).
* **Server log** (decisions and ACK/NACK outcome):

  ```bash
  docker compose logs -f chirpstack | grep -E 'LinkADR|TxParamSetup|RxTimingSetup|mac|join'
  # "LinkADRReq acknowledged tx_power_index=.. dr=.. nb_trans=.. enabled_channels=[..]"
  # "LinkADRReq not acknowledged ch_mask_ack=.. dr_ack=.. tx_power_ack=.."
  ```
* **Device session** (current DR, tx_power_index, nb_trans, enabled channels, ADR history,
  mac_command_error_count, min/max supported tx power learned from NACKs):

  ```bash
  docker compose exec chirpstack chirpstack -c /etc/chirpstack print-ds --dev-eui <DEVEUI>
  ```
* **MQTT** (mosquitto_sub inside the broker container; gateway topics are JSON):

  ```bash
  # raw gateway traffic: uplinks (rxInfo rssi/snr, txInfo dr) and downlinks sent to the gateway
  docker compose exec mosquitto mosquitto_sub -v -t 'au915_1/gateway/+/event/+' -t 'au915_1/gateway/+/command/+'
  # application events: up (dr, adr, fCnt, rxInfo), join, txack, ack, log, status
  docker compose exec mosquitto mosquitto_sub -v -t 'application/+/device/+/event/+'
  ```
  `command/down` carries the downlink PHYPayload (base64) + txInfo (frequency, power, RX1/RX2 timing);
  MAC commands inside it are not decoded there, use the UI frames tab for that.

## Switching the ADR algorithm / ADR on-off

* Per device profile: UI → Device profiles → ST-WL55-AU915-ADR → *ADR algorithm*
  (`default` = "Default ADR algorithm (LoRa only)", `lr_fhss`, `lora_lr_fhss`), or
  `./provision.py --adr-algorithm <id>`. Applies at the next uplink.
* Custom JavaScript algorithm: put the file under `configuration/chirpstack/` (mounted at
  `/etc/chirpstack`), list it in `chirpstack.toml` → `[network] adr_plugins=["/etc/chirpstack/my_adr.js"]`,
  `docker compose restart chirpstack`; it then appears in the ADR algorithm dropdown (skeleton:
  https://github.com/chirpstack/chirpstack/blob/master/examples/adr_plugins/plugin_skeleton.js).
  A plugin is the only way to get per-profile min/max DR or installation margin (it receives
  `minDr`, `maxDr`, `installationMargin`, `uplinkHistory`, `deviceVariables`).
* Network-side off for the whole region: `adr_disabled=true` in `region_au915_1.toml` + restart.
  Device-side off: clear the ADR bit in the firmware (ChirpStack then only requires the channel-mask ACK).

## ADR / MAC knobs (effective values in this setup)

### Region `au915_1` – `configuration/chirpstack/region_au915_1.toml`, `[regions.network]`

| Key | Value | Effect |
|---|---|---|
| `installation_margin` | **10** dB | margin = maxSNR(history) − requiredSNR(SF) − 10; steps = floor(margin/3) |
| `min_dr` / `max_dr` | **0 / 5** | ADR DR range (DR0 SF12 … DR5 SF7, 125 kHz). DR6 (SF8/500 kHz) never chosen by `default` |
| `adr_disabled` | false | true = no ADR LinkADRReq at all for this region |
| `rx_window` | 0 | RX1, fall back to RX2 |
| `rx1_delay` | **1** s | sent in Join-accept RxDelay; device profile `rx1Delay` can only raise it (→ RXTimingSetupReq) |
| `rx1_dr_offset` | 0 | Join-accept DLSettings; AU915 RX1 DR with offset 0: DR0→DR8 … DR5→DR13, DR6→DR13 |
| `rx2_dr` / `rx2_frequency` | **8** (SF12/500 kHz) / **923.3 MHz** | Join-accept DLSettings |
| `rx2_prefer_on_rx1_dr_lt` / `rx2_prefer_on_link_budget` | 0 / false | RX1 preferred |
| `gateway_prefer_min_margin` | 10 (default, not in file) | downlink gateway selection |
| `downlink_tx_power` | −1 | gateway TX power = band default (30 dBm EIRP for AU915; gateway clamps to its table) |
| `enabled_uplink_channels` | **[8..15, 65]** | → CFList ChMask in Join-accept (MAC ≥1.0.3) or LinkADRReq ChMask blocks after join (MAC ≤1.0.2) |
| `extra_channels` | none | AU915 has no user-defined channels |
| `uplink_dwell_time_400ms` / `downlink_dwell_time_400ms` | **false / false** (default, not in file) | |
| `uplink_max_eirp` | **0** (default, not in file → EIRP index 0) | TxParamSetupReq is only sent when these differ from the device session (which starts at false/false/0), i.e. **never with these defaults** |
| `dwell_time_400ms`, `repeater_compatible` | false / false | only alter the region's payload-size tables (AU915 code ignores `dwell_time_400ms`) |
| `rejoin_request.enabled` | false | LoRaWAN 1.1 only |
| enforce flags | none | ChirpStack v4 has no "enforce" options for ADR/dwell |

### Network – `configuration/chirpstack/chirpstack.toml` (`[network]`, defaults not written in the file)

| Key | Value | Effect |
|---|---|---|
| `mac_commands_disabled` | false | true = no MAC commands at all (no ADR, no channel mask) |
| `max_mac_command_error_count` | **1** | once a CID (e.g. LinkADRReq) has been NACKed (non-recoverably) more than once, ChirpStack stops sending it for the rest of the session (counter only cleared by an ACK or a new join) |
| `adr_plugins` | [] | custom JS ADR algorithms |
| `deduplication_delay` / `get_downlink_data_delay` | 200 ms / 100 ms | RX1 timing budget (rx1_delay 1 s) |

### AU915 constants in ChirpStack (lrwn/src/region/au915.rs, v4.20.0)

* TxPower index 0…14 = MaxEIRP − 0…28 dB (MaxEIRP 30 dBm). ADR may command any index up to 14
  unless the device NACKs the power; then ChirpStack stores `max_supported_tx_power_index = req−1`
  (a NACK on index 0 sets min supported index 1, RN2483 workaround).
* Uplink DR0-5 on ch 0-63, DR6 on ch 64-71; RX1 delay 1 s, Join-accept delays 5/6 s.
* TxParamSetupReq supported for AU915 except LoRaWAN 1.0.1/1.0.2.

### `default` ADR algorithm (chirpstack/src/adr/default.rs)

* Runs on every uplink that has the ADR bit set. Uses the **max SNR** (best gateway) over the
  stored history (last 20 uplinks; the "20 entries" checks below count only those at the current TxPower). Required SNR: SF7 −7.5, SF8 −10, SF9 −12.5,
  SF10 −15, SF11 −17.5, SF12 −20 dB.
* Positive steps are applied **immediately, even with a single uplink in history**: first raise DR
  up to `max_dr`, then raise the TxPower index (lower power) up to the max index (14).
* Negative steps only lower the TxPower index (more power), and only once 20 history entries exist; it
  **never lowers the DR**.
* NbTrans 1-3 from packet loss (needs 20 entries). History is reset whenever DR/TxPower/NbTrans change
  after a LinkADRAns full ACK.

### Device-profile settings that override / add to the above

| Field | Current | Effect |
|---|---|---|
| `adrAlgorithmId` | default | which algorithm runs |
| `macVersion` / `regParamsRevision` | 1.0.4 / RP002-1.0.3 | CFList type (ChMask vs none), TxParamSetupReq support, max payload tables |
| `regionConfigId` | au915_1 | binds devices to this region config |
| `rx1Delay` | 0 (use region) | can only increase RX1 delay |
| `supportedUplinkDataRates` | [] | only used when (re)configuring user-defined channels; **does not limit ADR** (AU915 has none) |
| `uplinkInterval` | 15 s | inactivity detection only |
| `deviceStatusReqInterval` | 1/day | DevStatusReq (battery, link margin) |
| `flushQueueOnActivate` | true | clear downlink queue on (re)join |
| device `skipFcntCheck`, `variables` | – | debug; variables are passed to ADR plugins |

Not configurable per device profile: `installation_margin`, `min_dr`, `max_dr`, `adr_disabled`, dwell
time, uplink max EIRP (region-wide only; use a separate region config id or an ADR plugin).
