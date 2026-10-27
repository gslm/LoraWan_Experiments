# LoRaWAN ADR Lab Primer

Oct 27, 2026 · Guilherme Moreira

## Briefing for the customer meeting

Our best current reading: ChirpStack's default ADR pushes TX power down far faster than TTN does, and that exposes a weakness on the device's radio side that TTN reaches far more slowly, if at all. This comes from reading the source of all three stacks. It is not reproduced yet, so treat it as the leading hypothesis, not a finding.

**What the customer asked for**: their Class A end node uses an STM32WLE5 in the SJIT LSM110A module, running ST's LoRaWAN stack. ADR behaves on The Things Network but, on ChirpStack, Data Rate and TX Power changes lead to unwanted end-node behaviour. They want the root cause (firmware/stack vs server config), fixes, and confidence before LoRa Alliance certification.

**What the code says so far**

- **The device's ADR code is Semtech's reference code.** ST's stack (LoRaMac-node 4.x inside STM32CubeWL up to v1.4.0) is line-for-line Semtech on the ADR path. We found no device-side ADR bug that alone explains a ChirpStack-only symptom.
- **ChirpStack's default ADR is much more aggressive than TTN's.** It uses a 10 dB margin, acts on a single uplink, and clears its history after every change. At good signal it lowers TX power by 2 steps per uplink down to index 14 (about -1 dBm). TTN uses a 15 dB margin plus a 2.5 dB penalty until it has 20 uplinks, so it moves about one step per 20 uplinks, and only while SNR stays above about 9.5 dB at SF7.
- **Low TX indices hit the STM32WL's power-amplifier structure.** Indices 0-2 all clamp to 22 dBm, so the first steps change nothing. Index 6 (15 dBm) and every higher index switch from the high-power to the low-power amplifier. The LSM110A vendor code uses the high-power path only, so a build with ST's Nucleo default (both paths) on that module is a prime suspect.
- **ChirpStack then amplifies a weak link.** After the device's own ADR back-off it re-imposes its stale low-power setting. It flip-flops NbTrans, and after two rejected LinkADRReq it stops sending them for the rest of the session.
- **A public report matches the symptom.** An unanswered ST Community post (Aug 2026): STM32WL, ST middleware 2.5.0, AU915, ChirpStack steps power 0, 2, 5, 7, then the device stops transmitting. It is stable on TTN, and ADR off avoids it.

**What to say tomorrow**: the likely root cause is an interaction between ChirpStack's ADR settings and the device's RF configuration at low TX power, not a defect in ST's ADR logic. Both sides have quick levers: a higher installation margin or an ADR plugin on ChirpStack, and an RF-path check on the device. We will confirm it on our bench before committing to a root cause.

## Our bench

The board already runs ST's end-node example with full ADR tracing, and it has joined. What answered was the Conduit's built-in network server, which has applied ADR twice. Local ChirpStack is running but no gateway feeds it yet.

| Part | What it is | State on 2026-10-27 |
| --- | --- | --- |
| End device | NUCLEO-WL55JC (STM32WL55JC), STLINK-V3E, log on /dev/ttyACM2 at 115200 | Flashed with `firmware/LoRaWAN_End_Node_ADR`, joined, uplink every 15 s |
| Firmware | STM32CubeWL v1.4.0 End\_Node: ST LoRaWAN MW 2.6.2 (LoRaMac-node 4.7.0), L2 1.0.4, RP002-1.0.3 | AU915 sub-band 2, OTAA, Class A, ADR on, `[ADR]` `[MAC]` `[UL]` `[DL]` `[JOIN]` trace lines |
| Gateway | MultiTech Conduit MTCDT-210A, AEP 1.7.4, MTAC-LORA-915 card (8 channels), EUI 00800000A00006DD | Built-in network server active, AU915 sub-band 2. eth0 to DHCP client is staged but not applied |
| ChirpStack | v4.20.0 in Docker on 192.168.28.86, region `au915_1`, UI on port 8080, UDP forwarder port 1700 | Running, device profile and app `adr-lab` created, no gateway connected |
| TTN | Your account, cluster still to pick | Gateway not registered yet |

**Device identity for any network server**: DevEUI `0080E1150530F717`, JoinEUI `0101010101010101`, AppKey `2B7E151628AED2A6ABF7158809CF4F3C` (ST's public test key, lab use only), MAC 1.0.4, RP002-1.0.3.

**What we have seen live**

- **The gateway's own server joined our board without registration.** Its network EUI is ST's default JoinEUI, and it is configured for AU915 sub-band 2.
- **First downlink after join**: a two-command LinkADRReq block. ChMaskCntl 7 enables channel 65, then ChMaskCntl 0 with mask 0xFF00 enables channels 8-15. The device answered 0x07 (all acknowledged).
- **ADR from the MultiTech server**: DR2 to DR3 with TX index 0 to 1, then TX index 1 to 2. Its ADR is capped at DR3 with 3 dB steps.
- **The radio output stayed at 22 dBm the whole time.** Indices 0, 1 and 2 ask for 27, 25 and 23 dBm conducted, and the high-power amplifier clamps all three to 22 dBm. The server believes it saved power; the gateway sees no change.
- **DevNonce restarts at 1 after every reset unless the NVM context was saved (ST's example saves it only on button B3).** The server silently drops reused nonces, so the first join attempts after a reset fail. TS001-1.0.4 forbids reusing a DevNonce, so the customer's product must store it in flash if it joins by OTAA.

**Where things are in the workspace**: `firmware/` (project, README, `patches/adr-trace.diff`, backup of the teammate's Zephyr demo), `chirpstack/` (Docker stack, `provision.py`, README), `third_party/STM32CubeWL` (v1.4.0 source).

## The stack, layer by layer

A LoRaWAN network has four parts: the end device, gateways, a network server, and an application. The gateway is a dumb radio relay; every MAC setting, ADR included, is decided by the network server and executed by the device, apart from the device's own ADR back-off.

_(Diagram not included in this export: the LoRaWAN chain on our bench, from device layers through the gateway to the network servers.)_

Uplinks climb from the device's application through its radio and the gateway to one network server; ADR commands come back down the same path in RX1 or RX2.

### 1. LoRa: the radio (physical layer)

- **LoRa is Semtech's chirp spread-spectrum modulation.** The spreading factor (SF7 to SF12) trades speed for range: each step up roughly doubles time on air and gains about 2.5 dB of sensitivity.
- **Each SF can be demodulated below the noise floor.** The required SNR runs from -7.5 dB at SF7 to -20 dB at SF12. ADR algorithms use exactly these numbers.
- **In AU915, bandwidth is 125 kHz or 500 kHz.** Wider is faster and less sensitive.
- **Data rate (DR) is just a regional index** for a SF/bandwidth pair, so "DR2" means different things in different regions.

### 2. LoRaWAN: the MAC protocol (LoRa Alliance spec TS001)

- **Class A**, which the customer uses: the device transmits whenever it wants, then opens two short receive windows. RX1 opens 1 s after the uplink (configurable) on a frequency tied to the uplink. RX2 opens 1 s later on a fixed frequency and DR. The network can only talk to the device in those windows.
- **Frames** carry a frame counter (FCnt), an application port (FPort), optional MAC commands in FOpts (up to 15 bytes), and control bits such as ADR, ADRACKReq and ACK.
- **MAC commands** are how the server configures the device: LinkADRReq/Ans (data rate, power, channels, repetitions), TxParamSetupReq (dwell time, max EIRP), RXParamSetupReq, DevStatusReq and others. FPort 0 means the payload is MAC commands only.
- **OTAA join**: the device sends a JoinRequest (DevEUI, JoinEUI, DevNonce); the server answers with a JoinAccept giving a DevAddr, RX settings and optionally a channel list or mask (CFList). Session keys are derived from the AppKey.
- **Versions that matter here**: L2 1.0.3 vs 1.0.4. Version 1.0.4 adds the 0xF "keep current" values in LinkADRReq, requires DevNonce to never repeat, and refines the ADR back-off.

### 3. Regional parameters: AU915 for Brazil (spec RP002)

- **Brazil uses AU915-928** (RP002-1.0.3). TTN also offers a Brazil-only variant, `AU_915_928_FSB_2_BR`, with dwell time disabled. LA915 is an Everynet network plan, not a LoRa Alliance region.
- **Channels**: 64 uplink channels at 125 kHz (915.2 MHz + 0.2 MHz steps), 8 at 500 kHz, and 8 downlink channels at 500 kHz. An 8-channel gateway like our Conduit listens to one sub-band: sub-band 2 is channels 8-15 (916.8-918.2 MHz) plus 500 kHz channel 65 (917.5 MHz).
- **Uplink DRs**: DR0 = SF12/125 kHz down to DR5 = SF7/125 kHz, and DR6 = SF8/500 kHz. Downlink DR8-DR13 are SF12-SF7 at 500 kHz; RX2 defaults to 923.3 MHz at DR8.
- **TX power is an index, not dBm**: index n means max EIRP minus 2n dB, so 30 dBm EIRP at index 0 down to 2 dBm at index 14.
- **Dwell time**: RP002 says AU915 devices boot with a 400 ms uplink dwell limit. That forbids DR0 and DR1 and limits DR2 to 11 bytes of payload until the server sends TxParamSetupReq.
- **Channel masks in AU915 are sent in blocks.** ChMaskCntl 7 switches all 125 kHz channels off and sets the 500 kHz mask; then ChMaskCntl 0-3 set 16 channels at a time (4 sets channels 64-71). Servers send them as one contiguous block of LinkADRReq commands.

### 4. Gateway and packet forwarder

- **The concentrator** (SX1301 on our MTAC-LORA-915 card) demodulates 8 channels at once and timestamps every packet.
- **The packet forwarder** (`lora_pkt_fwd`) relays packets to a server over **Semtech UDP** on port 1700 and transmits the downlinks it is handed. **LoRa Basics Station** is the newer, secured alternative, but it needs mPower 5.3 or later, which our gateway does not have.
- **The MultiTech Conduit can also run its own network server**, which is how it runs today.

### 5. Network servers in this story

- **ChirpStack**: open source and self-hosted (v4 is written in Rust), very common in private deployments. ADR is a pluggable algorithm, and its knobs live in per-region config files.
- **TTN / The Things Stack (TTS)**: The Things Industries' open-source server. The Things Network is the free community deployment on three clusters (eu1, nam1, au1).
- **MultiTech built-in server**: a small network server inside the Conduit's AEP firmware, with its own ADR settings.
- **What a server does**: de-duplicates uplinks from several gateways, checks the integrity code (MIC) and frame counters, keeps the session, runs ADR, queues MAC commands, and schedules each downlink to hit RX1 or RX2 on time.

### 6. ST's device stack (STM32CubeWL)

- **Layers, top to bottom**: `lora_app.c` (application), `LmHandler` (high-level API plus packages such as the compliance package), `LoRaMac` (MAC core, `LoRaMacAdr.c`, `Region/RegionAU915.c`), `SubGHz_Phy` (radio driver), `radio_board_if` (board-specific RF switch and amplifier configuration), and the STM32WL's built-in SX126x-class radio.
- **The MAC core is Semtech's LoRaMac-node**, the reference end-device stack, with ST's port around it. ST adds nothing on the ADR path.
- **The STM32WL radio has two power amplifiers**: low power (LP, up to 15 dBm) and high power (HP, up to 22 dBm). The board file says which ones are wired to the antenna. Getting this wrong only shows up at some power levels.
- **Semtech's newer stack is LoRa Basics Modem (LBM).** ST switched CubeWL to it in v1.5.0 (2026) and removed the LoRaMac-node projects. So a customer on "ST's LoRaMac-node stack" is on CubeWL v1.4.0 or older.

| CubeWL | ST LoRaWAN MW | LoRaMac-node | L2 / RP by default | Notes |
| --- | --- | --- | --- | --- |
| v1.0.0 (2020-10) | 2.2.1 | 4.4.4 | 1.0.3 / RP001-1.0.3 | No 0xF keep values; 1.0.3 back-off |
| v1.1.0 (2021-06) | 2.3.0 | 4.4.7 | 1.0.3 / RP001-1.0.3 | Same as v1.0.0 |
| v1.2.0 (2022-02) | 2.4.0 | 4.5.2 | 1.0.4 / RP002-1.0.1 | 0xF supported; NVM context added. LSM110A SDK 1.0.4 is based on this MW |
| v1.3.0 (2022-11) | 2.5.0 | 4.6.0 | 1.0.4 / RP002-1.0.1 | Version in the public ST forum report |
| v1.3.1 (2024-12) | 2.6.1 | 4.7.0 | 1.0.4 / RP002-1.0.3 | Radio register access made atomic |
| v1.4.0 (2025-10) | 2.6.2 | 4.7.0 | 1.0.4 / RP002-1.0.3 | Last LoRaMac-node release; our bench |
| v1.5.0+ (2026) | LBM | LoRa Basics Modem | n/a | Legacy stack removed |

The End\_Node boot banner prints `MW_LORAWAN_VERSION`, `L2_SPEC_VERSION` and `RP_SPEC_VERSION`, so one boot log from the customer tells us which row they are on.

## ADR in depth

ADR is a contract: the device sets the ADR bit to let the network choose its data rate, TX power and repetitions; the server decides; the device obeys through LinkADRReq. The device keeps one safety net, the ADR back-off, for when it stops hearing the network.

### How a network server decides

1. **Collect SNR** of recent uplinks for the device (usually the best of the last 20).
2. **Compute the margin**: best SNR minus the SNR the current SF needs, minus an installation margin (ChirpStack 10 dB, TTN 15 dB).
3. **Turn the margin into steps** (ChirpStack: one step per 3 dB; TTN: 2.5 dB per DR step, the real 2 dB per TX-power step).
4. **Spend positive steps** first on a faster DR, up to the region's ADR maximum (DR5 in AU915), then on lower TX power.
5. **Spend negative steps** only on more TX power. Neither ChirpStack nor TTN lowers the DR on a weak link (they only cap DR6 to DR5); recovering range is left to the device's back-off.
6. **Pick NbTrans** (1-3 copies of each uplink) from the packet loss.

### Anatomy of LinkADRReq and LinkADRAns

One LinkADRReq is 4 bytes after its command ID 0x03. Our trace shows `03 20 02 00 71`:

| Byte(s) | Field | Value in the example |
| --- | --- | --- |
| `20` | DataRate (high nibble), TXPower (low nibble) | DR2, TX index 0 |
| `02 00` | ChMask, 16 bits little-endian | 0x0002 |
| `71` | ChMaskCntl (bits 6-4), NbTrans (bits 3-0) | ChMaskCntl 7, NbTrans 1 |

- **The answer, LinkADRAns, is one status byte**: bit 2 power OK, bit 1 data rate OK, bit 0 channel mask OK. 0x07 means all accepted.
- **In L2 1.0.4 the block is all or nothing.** If any bit is a NACK, the device applies none of it: no DR, power or mask change.
- **DR or TXPower = 0xF means "keep current"** (1.0.4 only). An L2 1.0.3 device rejects it.
- With ADR on, **ST applies DR, power and NbTrans from the last command of the block**, and answers each command with the same status.

### The device's back-off (L2 1.0.4, as in ST's stack)

The device counts uplinks since its last downlink (ADR\_ACK\_CNT). With ADR\_ACK\_LIMIT = 64 and ADR\_ACK\_DELAY = 32:

1. **At 64 uplinks without a downlink** it sets the ADRACKReq bit, asking the server for any reply. At our 15 s period that is 16 minutes.
2. **At 96** it resets TX power to the maximum (index 0).
3. **At 128, and every 32 after**, it steps the DR down by one.
4. **At the lowest DR** it restores the default channel mask and NbTrans = 1. In a stock AU915 build (HYBRID off) the default mask is all 72 channels (our bench build restores sub-band 2 instead), so on an 8-channel gateway about 7 of 8 uplinks then go unheard until the server re-sends the mask.

### What is special about AU915 on the STM32WL

- **Dwell time on at boot.** ST's stack starts with uplink dwell time = 1, so its lowest uplink DR is DR2 and DR2 carries at most 11 bytes of payload. Stock ChirpStack never sends TxParamSetupReq, so it does not change this. TTN's FSB\_2 plan matches it; TTN's FSB\_2\_BR plan turns it off.
- **TX power index to dBm**: the stack asks the radio for floor(30 - 2 x index - 2.15) dBm conducted, using a 2.15 dBi assumed antenna gain.
- **The two amplifiers split the range.** Requests above 15 dBm use the HP amplifier, capped at 22 dBm; 15 dBm and below use the LP amplifier. On the Nucleo, indices 0-2 (27, 25, 23 dBm requested) all come out at 22 dBm, and index 6 (15 dBm) is the first LP index.
- **The board file decides which amplifier exists.** ST's Nucleo template declares both (`RBI_CONF_RFO_LP_HP`). SJIT's LSM110A code declares HP only (`RBI_CONF_RFO_HP`). A product built from the Nucleo template on an HP-only module would send indices 6-14 into an amplifier that is not connected to the antenna.

_(Chart not included in this export: TX power index against actual radio output on the NUCLEO-WL55JC, AU915, 2.15 dBi assumed gain, computed from CubeWL v1.4.0 RegionCommon.c and radio\_driver.c.)_

On TTN the device stays at or near full power on the HP amplifier, stepping down about once per 20 uplinks. Stock ChirpStack walks it across the amplifier switch and down to about -1 dBm, territory TTN reaches only if SNR stays above about 9.5 dB, and then only one step per \~20 uplinks.

## ChirpStack vs TTN: ADR and MAC defaults for AU915

With stock settings ChirpStack moves faster, goes lower and recovers worse than TTN. Values are from ChirpStack v4.20.0 (`region_au915_1`) and The Things Stack v3.36-3.37 source.

| Behaviour | ChirpStack v4.20 (stock) | TTN / The Things Stack |
| --- | --- | --- |
| ADR safety margin | 10 dB (`installation_margin`, per region) | 15 dB, plus 2.5 dB more until 20 uplinks |
| dB per step | 3 dB per DR or TX step (an AU915 TX step is really 2 dB) | 2.5 dB per DR; real 2 dB per TX step |
| History needed before acting | None for upward steps; cleared after every change | Uplinks since the last change; penalty below 20 |
| Order of changes | DR up to DR5, then TX power down, in one command | DR up (TX reset to index 0), leftover margin lowers TX |
| Example: SF7 at +10 dB SNR | +2 TX indices every uplink, until index 14 | +1 index per \~20 uplinks, only while SNR stays above 9.5 dB |
| Raises TX power again | Only after 20 uplinks at the same index and margin of -3 dB or worse | On negative margin; a 1-index raise waits for 20 uplinks |
| NbTrans | 1-3 from loss over 20 uplinks; drops back right after a raise | Needs 10+ uplinks; otherwise kept |
| After a rejected LinkADRReq | Error count; after the 2nd, no LinkADRReq for the rest of the session | Blacklists only the rejected DR or TX index |
| 0xF "keep current" | Never sent | Sent to MAC 1.0.4 devices for unchanged DR/TX |
| On ADRACKReq | Forced downlink plus a channel-reset block carrying its stored DR and TX index | Forced downlink; LinkADRReq only if needed |
| TxParamSetupReq (dwell time) | Never with stock config; assumes dwell off | Sent after join per frequency plan |
| `uplink_max_eirp` trap | Default 0 means EIRP index 0 = 8 dBm if dwell signalling is enabled | Not applicable (30 dBm) |
| RX1 delay | 1 s | 5 s |
| Channel mask after join | CFList when the profile is MAC 1.0.3+; otherwise a LinkADRReq block | CFList for RP002 bands |

**The Conduit's built-in MultiTech server** is a third behaviour. From its config: ADR capped at DR3, step setting 30, RX1 delay 1 s, RX2 at DR12 (SF8/500), uplink dwell 0. On join it sent a ChMaskCntl 7 + 0 block rather than a CFList. We have not inspected its algorithm.

**Server-side levers on ChirpStack**: raise `installation_margin` (15-17.5 dB keeps it near TTN), set `uplink_dwell_time_400ms` together with `uplink_max_eirp = 30`, raise `rx1_delay` to 3-5 s, raise `max_mac_command_error_count`, or write a JavaScript ADR plugin that caps the TX index. All but the plugin and max\_mac\_command\_error\_count (server-wide, under \[network\] in chirpstack.toml) are region-wide settings.

## Root-cause hypotheses, ranked

Hypothesis 1 is the largest deterministic ADR difference between the two servers with stock settings; 2-4 are what could turn it into a device failure. A skeptical reviewer checked every claim against the ChirpStack, The Things Stack and CubeWL source. "Supported in code" means the mechanism is confirmed there, not yet reproduced on the bench.

| # | Hypothesis | Side | Code verdict | Bench test | Bench result |
| --- | --- | --- | --- | --- | --- |
| 1 | At good SNR ChirpStack lowers TX power 2 indices per uplink, down to 14; TTN steps about once per 20 uplinks | Server | Supported in code | 30 uplinks on each server, logging TX index per uplink; then `installation_margin = 17.5` should stop the ramp | Not run |
| 2 | The LP amplifier is not wired or configured on the customer's board, so uplinks at index 6 and above are lost | Device | Plausible | ADR off, sweep TX index 0-14 and compare gateway RSSI per index; repeat with the customer's `radio_board_if` files | Not run |
| 3 | The device hangs after its first LP transmission (pattern of the ST forum report) | Device | Plausible | CubeWL v1.3.0 and v1.4.0 builds, 100+ uplinks with current logging; also ADR off with TX index 7 forced | Not run |
| 4 | After the device's back-off, ChirpStack re-imposes its stale low-power TX index (e.g. 14) | Both | Supported in code | Test build with ADR\_ACK\_LIMIT 8 / DELAY 4, block downlinks, check that the first downlink after ADRACKReq carries TX index 14 | Not run |
| 5 | ChirpStack flip-flops NbTrans: up after loss, back to 1 within one or two uplinks | Server | Supported in code | Induce about 20% uplink loss; watch NbTrans go 1, 2-3, 1 | Not run |
| 6 | Channel mask diverges when device ADR is off and the join has no CFList | Both | Supported in code | Profile MAC 1.0.2, device ADR off, fresh join; expect LinkADRAns 0x04 then 0x07 with channels 8-71 (64) left on (HYBRID-off build) | Not run |
| 7 | ChirpStack stops sending LinkADRReq after 2 rejections | Server | Plausible | Watch for "not acknowledged" in ChirpStack logs; retry with `max_mac_command_error_count = 5` | Not run |
| 8 | Dwell-time asymmetry: payload dropped at DR2, or the 8 dBm `uplink_max_eirp` trap | Both | Plausible | Check the customer's TTN frequency plan; send 10 bytes at DR2 while a MAC block is pending | Not run |
| 9 | ChirpStack's 1 s RX1 delay is too tight for the setup (TTN uses 5 s) | Config | Plausible | Count RX1/RX2 misses vs scheduled downlinks and TOO\_LATE gateway acks; retry with `rx1_delay = 3` | Not run |
| 10 | LoRaWAN or RP version mismatch between ChirpStack profile, TTN registration and the build | Config | Plausible | Get the customer's boot banner and profile settings; rerun test 1 with profile MAC 1.0.2 | Not run |
| 11 | Back-off at the lowest DR re-enables all 72 channels | Device | Plausible | Test build with HYBRID\_ENABLED 0; block downlinks until the mask resets; count uplinks until ChirpStack re-sends the mask | Not run |
| 12 | Saved NVM context carries ChirpStack state into TTN tests | Device | Supported in code | Erase the NVM page at 0x0803F000 before every server A/B run | Not run |

**Refuted by the reviewer**: "ChirpStack commands DR0/DR1 and the dwell-limited device rejects it in a loop" only applies to ChirpStack v3; v4 only raises the DR or caps it at DR5, never commanding DR0/DR1. "TTN leaves DR6 alone" is also wrong; both servers cap AU915 ADR at DR5.

## Questions for the customer

The first four answers decide which hypotheses survive; ask for logs, not descriptions.

- [ ] **What exactly goes wrong on ChirpStack?** The device stops transmitting, uplinks are lost, the battery drains, it rejoins, or DR/power oscillate? After how many uplinks, and does it recover?
- [ ] **A device boot log**: the banner lines `MW_LORAWAN_VERSION`, `L2_SPEC_VERSION` and `RP_SPEC_VERSION` identify their CubeWL release.
- [ ] **Their `radio_board_if.c` / `.h`**: the `RBI_CONF_RFO` setting, the RF-switch truth table and the max powers. Did they port it from the Nucleo template or from SJIT's code?
- [ ] **ChirpStack details**: v3 or v4 (which version), ADR algorithm, `installation_margin`, `min_dr`/`max_dr`, dwell-time and `uplink_max_eirp` settings, `rx1_delay`, and the device profile's MAC version and regional-parameters revision.
- [ ] **ChirpStack evidence for a failure window**: the device's LoRaWAN frames (LinkADRReq/Ans), and server logs containing "not acknowledged".
- [ ] **TTN details**: cluster, frequency plan (`AU_915_928_FSB_2` or `AU_915_928_FSB_2_BR`), and the MAC/PHY versions the device is registered with.
- [ ] **Test conditions on each server**: same gateway? 8 or 16 channels? Bench or field, and typical RSSI/SNR? Very strong bench signal makes ChirpStack drive power to the minimum.
- [ ] **Application behaviour**: ADR always on? Does the app set DR or TX power itself? Uplink period, payload size, confirmed or unconfirmed?
- [ ] **Persistence**: OTAA or ABP? If OTAA, is DevNonce stored in flash? Is the NVM context enabled, and do they rejoin at boot?
- [ ] **Firmware on the module**: their own CubeWL application, or SJIT's AT-command firmware?
- [ ] **What they tried**: ADR off on ChirpStack? A higher installation margin? Did either fix it?
- [ ] **Certification plan**: regions, test house, LCTT version and timeline; any pre-scan results so far.

## Certification notes

The ADR code should behave in LoRa Alliance tests exactly like Semtech's reference stack. But the newest stack versions the customer can be on (CubeWL v1.3.x and v1.4.0) have no public AU915 certification evidence, and certification does not test the RF problem we suspect.

- **Public evidence for ST's own reference designs is outside AU915.** We found a NUCLEO-WL55JC1 on CubeWL v1.0.0 certified for EU868, and an STM32WL5M on CubeWL v1.3.0 (L2 1.0.4) for KR920 and IN865. No AU915 or US915 report for an ST reference design turned up.
- **Semtech's own pre-certification** passed AU915 with LoRaMac-node 4.5.0 and 4.5.1; 4.5.2 (CubeWL v1.2.0) left AU915 Class A untested. For 4.6.0 and 4.7.0, which CubeWL v1.3.x and v1.4.0 use, AU915 is listed as "Not tested". An AU915 LCTT pre-scan is worth recommending.
- **The LSM110A itself is LoRaWAN-certified for AU915** (DEKRA, L2 1.0.4) with SJIT's firmware, which is built on ST's LoRaWAN middleware 2.4.0 (CubeWL v1.2.0). If the customer runs its own CubeWL application on the module, expect to certify the end product. This is our inference; confirm it with the LoRa Alliance or a test house.
- **The test tool (LCTT) does not measure radiated power per TX index.** A wrong amplifier configuration (hypothesis 2) would pass certification and still fail in the field, so it needs its own RF test.
- **Keep the certified image stock.** It must keep the compliance package (`LmhpCompliance`, port 224) and the region defaults: 72-channel mask, dwell time on, full back-off, TX indices 0-14. Our lab firmware's sub-band-2 default mask must not go into it. Prefer server-side fixes; a device workaround belongs in a separate build.
- With OTAA, **DevNonce must survive resets.** TS001-1.0.4 forbids reuse, and the stock End\_Node only saves it when a button is pressed.
- **One possible conformance gap to check before testing.** With ADR off, ST's stack evaluates each LinkADRReq in a block on its own rather than as one unit; compare this with TS001-1.0.4 sections 5.2 and 4.3.1.1.
- **ANATEL (Brazilian regulator) is separate from LoRaWAN certification.** The LSM110A has ANATEL certificate 05243-24-12325. Secondary sources say Ato 14158 ends an exemption 125 kHz LoRa relied on (the 6 dB bandwidth rule) for new filings from 6 Oct 2026; confirm with a Brazilian certification body.

## Glossary

Alphabetical; terms specific to this case are explained in its context.

| Term | Meaning |
| --- | --- |
| ABP | Activation By Personalisation: session keys and DevAddr are pre-loaded, with no join. The alternative to OTAA. |
| ADR | Adaptive Data Rate: the network server tunes each device's data rate, TX power and repetitions from link quality. ("ANR" in our notes means ADR.) |
| ADR\_ACK\_CNT / LIMIT / DELAY | Device counter of uplinks since the last downlink, and the thresholds (64, 32 in ST's stack) that trigger ADRACKReq and the back-off. |
| ADRACKReq | Uplink bit meaning "I have not heard from you; please answer". Set by the device during back-off. |
| AEP / mPower / mLinux | MultiTech Conduit firmware families. Ours is AEP 1.7.4 on mLinux 4.1.9; mPower is the newer line (5.x and later). |
| AppKey | Root key for a LoRaWAN 1.0.x device, used to derive session keys at join. ChirpStack v4 stores it in the "nwkKey" field. |
| AU915 | Regional plan for 915-928 MHz, used by Brazil and Australia. Sub-band 2 = channels 8-15 plus 65. |
| Basic Station | Semtech's newer, secured gateway-to-server protocol (WebSocket/TLS). Not available on our AEP gateway. |
| BW | Bandwidth of a LoRa channel: 125 kHz or 500 kHz in AU915. |
| CFList | Optional list in the JoinAccept; in AU915 it carries the channel mask, so no LinkADRReq block is needed after join. |
| ChirpStack | Open-source LoRaWAN network server ("Chirp" in our notes); v4 is current. |
| ChMask / ChMaskCntl | Channel-enable bitmap in LinkADRReq, and the selector saying which 16 channels it applies to (7 = all 125 kHz off, set the 500 kHz mask). |
| Class A / B / C | Device receive modes: A listens only after its own uplinks (the customer's device); B adds scheduled slots; C listens continuously. |
| Concentrator | Multi-channel gateway radio (SX1301 on our card) that demodulates eight 125 kHz channels plus one 500 kHz channel at once. |
| CubeWL | STM32CubeWL, ST's software package for STM32WL: HAL, examples, and the LoRaWAN middleware. |
| DevAddr | 32-bit network address assigned to the device at join. |
| DevEUI | Device's unique 64-bit ID. Ours is derived from the chip's UID. |
| DevNonce | Join counter sent in each JoinRequest; in 1.0.4 it must never repeat. |
| DR | Data Rate index: a SF/bandwidth pair defined per region. AU915 uplink DR0 = SF12/125 to DR5 = SF7/125, DR6 = SF8/500. |
| Dwell time | Maximum time on air per transmission; AU915 devices boot with a 400 ms uplink limit. |
| EIRP | Effective radiated power: conducted power plus antenna gain. LoRaWAN TX power indices are in EIRP. |
| FCnt | Frame counter, up and down, protecting against replay. |
| FOpts / FPort | Up to 15 bytes of MAC commands piggy-backed on a frame / application port (0 = MAC commands only). |
| FSB / sub-band | Group of 8 125 kHz channels plus one 500 kHz channel; an 8-channel gateway serves one. |
| Gateway EUI | Gateway's 64-bit ID; ours is 00800000A00006DD (from the LoRa card). |
| HP / LP PA | STM32WL's high-power (up to 22 dBm) and low-power (up to 15 dBm) amplifiers; the board file says which are wired. |
| Installation margin | Safety margin a server keeps above the required SNR when running ADR (ChirpStack 10 dB, TTN 15 dB). |
| JoinEUI (AppEUI) | ID of the join server the device joins through; ST's examples use 0101010101010101. |
| L2 / TS001 | The LoRaWAN link-layer specification; versions 1.0.3 and 1.0.4 matter here. |
| LBM | LoRa Basics Modem: Semtech's newer device stack, used by CubeWL v1.5.0 and later. |
| LCTT | LoRa Certification Test Tool, used by test houses for LoRa Alliance certification. |
| LinkADRReq / LinkADRAns | MAC command setting DR, TX power, channel mask and NbTrans / the device's 1-byte accept-reject answer. |
| LmHandler | ST/Semtech's high-level LoRaWAN API between the application and the MAC. |
| LoRa | Semtech's chirp spread-spectrum radio modulation (the physical layer). |
| LoRa Alliance | The body that publishes LoRaWAN specs and runs device certification. |
| LoRaMac-node | Semtech's reference end-device LoRaWAN stack, the MAC core inside ST's stack up to CubeWL v1.4.0. |
| LoRaWAN | The MAC protocol and network architecture on top of LoRa. |
| LSM110A | SJIT's LoRaWAN module built on an STM32WLE5CC; the customer's hardware. |
| MAC command | Control message between server and device (e.g. LinkADRReq, TxParamSetupReq, DevStatusReq). |
| MIC | Message Integrity Code: a cryptographic check on every frame. |
| NbTrans | Number of times each uplink is sent (1-15), set by the server through LinkADRReq. |
| NetID | Network operator ID; the Conduit's built-in server uses 000000. |
| Network server | Server that runs the LoRaWAN MAC for devices: sessions, ADR, MAC commands, downlink timing. |
| NVM context | ST's saved MAC state in flash, restored at boot so the device can skip rejoining. |
| OTAA | Over-The-Air Activation: the device joins with a JoinRequest/JoinAccept exchange. Our bench device uses it; the customer's activation mode is still to be confirmed. |
| Packet forwarder | Gateway program relaying radio packets to a server (`lora_pkt_fwd`, Semtech UDP). |
| RBI | ST's `radio_board_if`: board file for RF switch and amplifier configuration. |
| RP002 | LoRaWAN Regional Parameters spec (1.0.3 here): channels, DRs and power tables per region. |
| RSSI / SNR | Received signal strength (dBm) / signal-to-noise ratio (dB); ADR is driven by SNR. |
| RX1 / RX2 | Class A receive windows after an uplink: RX1 after 1 s (default) on a linked channel, RX2 1 s later on 923.3 MHz in AU915. |
| RX1DROffset | Offset from the uplink DR to the RX1 downlink DR, set at join. |
| Semtech | Inventor of LoRa; makes the radios and the reference software (LoRaMac-node, packet forwarder, Basic Station, LBM). |
| Semtech UDP | Original gateway-to-server protocol on UDP port 1700, used by our Conduit. |
| SF | Spreading factor, SF7 to SF12: higher is slower and longer range. |
| SubGHz\_Phy | ST's radio driver layer for the STM32WL's built-in radio. |
| ToA | Time on air of one transmission. |
| TTN / TTS | The Things Network, the community deployment of The Things Stack network server by The Things Industries. |
| TX power index | LoRaWAN power level: index n = max EIRP minus 2n dB (AU915: 30 dBm at 0, 2 dBm at 14). |
| TxParamSetupReq | MAC command setting dwell-time limits and max EIRP; key in AU915. |

## Sources

Source code was read from fresh clones: STM32CubeWL v1.4.0, ChirpStack v4.20.0 and The Things Stack (lorawan-stack). Device and gateway facts come from our own bench.

- [ST Community thread 167559](https://community.st.com/stm32-mcus-wireless-35/stm32wl-lorawan-mw-2-5-0-mac-1-0-4-au915-freezes-uplinks-after-linkadrreq-on-chirpstack-stable-on-ttn-167559): STM32WL AU915 stops after LinkADRReq on ChirpStack, stable on TTN
- [STM32CubeWL on GitHub](https://github.com/STMicroelectronics/STM32CubeWL): release notes and source
- [ChirpStack default ADR (`adr/default.rs`)](https://github.com/chirpstack/chirpstack/blob/master/chirpstack/src/adr/default.rs) and [`region_au915_1.toml`](https://github.com/chirpstack/chirpstack/blob/master/chirpstack/configuration/region_au915_1.toml)
- [ChirpStack issue #537](https://github.com/chirpstack/chirpstack/issues/537): multiple LinkADRReq right after join is intended
- [ChirpStack issue #1015](https://github.com/chirpstack/chirpstack/issues/1015): LinkADRReq suppressed after two NACKs; 4.20 stops counting TX-power NACKs
- [ChirpStack issue #850](https://github.com/chirpstack/chirpstack/issues/850): server's TX-power belief goes stale
- [ChirpStack forum 5338](https://forum.chirpstack.io/t/linkadrreq-for-au915-minimum-data-rate/5338) and [12265](https://forum.chirpstack.io/t/stm32wl-sdk-issue/12265): AU915 minimum DR and an STM32WL device that worked on TTN only (ChirpStack v3)
- [ChirpStack forum 14969](https://forum.chirpstack.io/t/when-both-dwelltime400ms-set-to-false-in-chipstack-ns-configuration-no-txparamsetupreq-sent-to-device/14969): no TxParamSetupReq with dwell flags off
- [ChirpStack forum 15588](https://forum.chirpstack.io/t/adr-issue-dropping-devices/15588): AU915 ADR dropping devices, caused by server latency
- [The Things Stack ADR docs](https://www.thethingsindustries.com/docs/concepts/features/lorawan/adr/) and [MAC settings docs](https://www.thethingsindustries.com/docs/hardware/devices/configuring-devices/mac-settings/)
- [TTN frequency plans](https://github.com/TheThingsNetwork/lorawan-frequency-plans/blob/master/frequency-plans.yml): `AU_915_928_FSB_2` and `AU_915_928_FSB_2_BR`
- [TTN community clusters](https://www.thethingsnetwork.org/forum/t/current-and-upcoming-community-clusters/43718)
- [LoRaMac-node issues #531](https://github.com/Lora-net/LoRaMac-node/issues/531), [#533](https://github.com/Lora-net/LoRaMac-node/issues/533), [#1211](https://github.com/Lora-net/LoRaMac-node/issues/1211), [#1304](https://github.com/Lora-net/LoRaMac-node/issues/1304)
- [LoRaMac-node pre-certification results](https://github.com/Lora-net/LoRaMac-node/wiki/releases-pre-certification-results)
- [RP002-1.0.3 Regional Parameters](https://lora-alliance.org/wp-content/uploads/2021/05/RP002-1.0.3-FINAL-1.pdf)
- [SJIT LSM110A repository](https://github.com/Support-SJI/LSM110A): datasheet, certificates, `radio_board_if.h`
- [MultiTech AEP software guide S000727](https://multitech.com/wp-content/uploads/rss/mPower_Software_Documentation/mPower/AEP_Software_Guide/pdf/S000727_mPower_Conduit_AEP_Software_Guide.pdf)
- [ANATEL 6 dB bandwidth exemption ending](https://ib-lenhardt.com/news/anatel-6-db-bandwidth-exemption-to-end-on-october-6-2026) (secondary source)
