# LoRaWAN_End_Node_ADR: STM32WL end node instrumented for ADR exploration

This is ST's `LoRaWAN_End_Node` example for NUCLEO-WL55JC (CPU1/Cortex-M4 only). It runs on the same LoRaWAN stack the customer's LSM110A / STM32WLE5 node uses: the STM32CubeWL LoRaWAN middleware, which is based on Semtech LoRaMac-node. The changes are:

* lab configuration: AU915, sub-band 2, OTAA, Class A, ADR on, unconfirmed uplinks every 15 s on FPort 2, and a 5-byte payload
* ADR and MAC tracing: `[JOIN]` `[UL]` `[DL]` `[MAC]` `[ADR]` lines on the ST-LINK virtual COM port
* a self-contained CMake/Ninja build that also works in the STM32 VS Code extension

```
firmware/
├── README.md                     <- this file
├── LoRaWAN_End_Node_ADR/         <- the project (self-contained, upstream layout)
│   ├── Core/ LoRaWAN/            <- application (CubeMX generated + LoRaWAN app)
│   │   └── LoRaWAN/App/lab_config.h   <- ONE place for region / sub-band / period / trace knobs
│   │   └── LoRaWAN/App/adr_trace.[ch] <- all trace decoding/printing
│   ├── Drivers/ Middlewares/ Utilities/   <- copies of the STM32CubeWL v1.4.0 files that are needed
│   ├── CMakeLists.txt CMakePresets.json cmake/gcc-arm-none-eabi.cmake
│   ├── STM32WL55JCIX_FLASH.ld    <- linker script from the STM32CubeIDE project
│   ├── .vscode/ (launch.json, settings.json)  .settings/ (STM32Cube bundles)
│   ├── readme_upstream.txt  LoRaWAN_End_Node_upstream.ioc   <- unmodified ST originals, kept as reference
├── patches/adr-trace.diff        <- every change against pristine upstream (diff -ruN)
├── backup/                       <- teammate demo flash backup + option bytes
├── logs/boot_capture_2026-10-27.log        <- first boot capture of this firmware
├── logs/final_flash_capture_2026-10-27.log <- capture around the final flash (includes NS ADR steps)
└── tools/  assemble_from_cube.sh  gen_adr_trace_diff.sh  capture_vcp.py
```

## 1. Which stack version is this?

| Item | Value |
|---|---|
| STM32CubeWL release used | **v1.4.0** (tag `v1.4.0`, 2025-10-29), in `third_party/STM32CubeWL` |
| Latest STM32CubeWL release | v1.6.0 (2026-06-05), kept for reference in `third_party/STM32CubeWL-v1.6.0-LBM` |
| Why not v1.6.0 | Since v1.5.0, ST ships **LoRa Basics Modem (LoRaWAN-LBM)** instead of the LoRaMac-node based "LoRaWAN" middleware. The `LoRaWAN_End_Node` project, `LoRaMac.c` and `LORAWAN_ADR_STATE` were removed and replaced by `LoRaWAN_End_Node_LBM`. v1.4.0 is the last release with the stack the customer uses. |
| LoRaWAN middleware | V2.6.2 (`MW_LORAWAN_VERSION`). LoRaMac-node 4.7-based. |
| SubGHz_Phy | V1.3.1 |
| LoRaWAN L2 | **TS001-1.0.4** (`LORAMAC_SPECIFICATION_VERSION 0x01000400`, default kept) |
| Regional Parameters | **RP002-1.0.3** (`REGION_VERSION 0x02010003`). The comment in `lorawan_conf.h` saying "RP002-1.0.1" is out of date; the boot banner prints `RP_SPEC_VERSION: V2-1.0.3`. |

If the customer is on an older CubeWL (1.2.x or 1.3.x), compare their `MW_LORAWAN_VERSION` with ours. The ADR/LinkADRReq code paths are the 1.0.4 ones described below.

## 2. Build

### CLI

```bash
cd firmware/LoRaWAN_End_Node_ADR
# Use ST's GCC 14.3 bundle (same compiler as VS Code). The system arm-none-eabi-gcc 10.3 also builds warning-free.
export PATH=$HOME/.local/share/stm32cube/bundles/gnu-tools-for-stm32/14.3.1+st.2/bin:$PATH
cmake --preset Debug          # or Release
cmake --build --preset Debug
# -> build/Debug/LoRaWAN_End_Node_ADR.elf (+ .hex, .bin, .map)
```

The toolchain file looks for `arm-none-eabi-gcc` in this order: `-DARM_TOOLCHAIN_DIR=<dir>` (or the env var of the same name), then `PATH`, then the newest `~/.local/share/stm32cube/bundles/gnu-tools-for-stm32/*`.

Debug uses `-Og -g3 -DDEBUG` and Release uses `-Os`. These are the same as the STM32CubeIDE configurations. Defines are `CORE_CM4 USE_HAL_DRIVER STM32WL55xx`. Sources, include paths and the linker script are taken from the example's `.project` and `.cproject`.

### VS Code (STM32 extension 3.x / cube-cmake)

1. Open **the project folder** `firmware/LoRaWAN_End_Node_ADR` (File > Open Folder). Don't open the repo root.
2. The extension detects `CMakeLists.txt` with `.settings/bundles.store.json` already present (cmake 4.3.1, ninja 1.13.2, GCC 14.3.1, programmer 2.23.0, stlink-gdbserver 7.14.0). Accept any setup prompt. If it asks for the device, it is STM32WL55JCIx on NUCLEO-WL55JC1, CPU1 / Cortex-M4.
3. Select the configure preset **Debug** and build.
4. Debug with *Run and Debug* > **"STM32Cube: Debug LoRaWAN_End_Node_ADR (ST-LINK GDB server)"**. This builds, flashes and stops at `main`. The second configuration, **"Attach to running..."**, attaches without a reset.
   `DEBUGGER_ENABLED=1` and `LOW_POWER_DISABLE=1` keep SWD alive all the time.

I checked that the bundled CMake 4.3.1 + Ninja build produces a binary identical to the CLI build.

## 3. Flash

```bash
PROG=~/.local/share/stm32cube/bundles/programmer/2.23.0/bin/STM32_Programmer_CLI
SN=003F00223431511931343632    # NUCLEO ST-LINK-V3E on this bench
cd firmware/LoRaWAN_End_Node_ADR
$PROG -c port=SWD sn=$SN mode=UR -e all -d build/Debug/LoRaWAN_End_Node_ADR.elf -v -rst
```

Use `-e all` when you switch from another firmware. Otherwise an old LoRaWAN NVM context at 0x0803F000 may be restored. That page is only written when you press B3 (see §6). For a quick reflash of this same project, `-d ... -v -rst` without `-e all` is enough.

Option bytes on this board: RDP Level 0, ESE=0 (no secure CPU2 area), FSD=1, no PCROP, no WRP, no boot locks. Never change them.

## 4. Monitor

```bash
picocom -b 115200 /dev/ttyACM2        # exit: Ctrl-A Ctrl-X
# or a timed capture with host timestamps:
python3 tools/capture_vcp.py /dev/ttyACM2 60 /tmp/run.log
```

`/dev/ttyACM2` is the ST-LINK VCP (`/dev/serial/by-id/usb-STMicroelectronics_STLINK-V3_003F00223431511931343632-if02`). **Never open `/dev/ttyACM1`**: it is the gateway console.
Right after you open the port, the first bytes can be stale data buffered by the ST-LINK.

## 5. Network-server provisioning (MSB first, as typed in ChirpStack/TTS)

| Field | Value |
|---|---|
| DevEUI | **`0080E1150530F717`** (derived from the chip UID64 as upstream does: `0080E1` = ST OUI. Another chip gives a different value, so read it from the `[JOIN] cfg DevEUI=` line.) |
| JoinEUI / AppEUI | **`0101010101010101`** (ST default) |
| AppKey | **`2B7E151628AED2A6ABF7158809CF4F3C`** (ST default = the FIPS-197 AES test key; public, lab only) |
| NwkKey | same as AppKey. For 1.0.x only the AppKey is used. |
| Activation / class | OTAA, Class A |
| MAC version | **LoRaWAN 1.0.4** |
| Regional parameters | **RP002-1.0.3**, AU915, **sub-band 2** (channels 8-15 + 65) = ChirpStack region `au915_1` / TTS `AU_915_928_FSB_2` |
| ADR | on (device sets FCtrl.ADR=1) |
| Uplinks | unconfirmed, FPort 2, 5 bytes: `[LED][seq MSB][seq LSB][temp °C int8][battery]` |

**DevNonce caveat (1.0.4).** The DevNonce is a counter in the NVM context, and the context is written to flash only when you press **B3**. After every reset or power cycle the device starts again at DevNonce=1. ChirpStack and TTS silently drop join requests with an already-used DevNonce. The first boot capture shows this: after the reset, DevNonce=1 got no Join-Accept and DevNonce=2 joined. So after a reset, expect a few failed joins, or flush the nonces on the server (ChirpStack: device > *Flush OTAA device nonces*; TTS: enable *Resets join nonces* or reset the session).
This is also a **certification item for the customer**. L2 1.0.4 requires that a DevNonce is never reused, so the product must persist it (for example, store the context after each join request or accept).

## 6. Changing things

All lab knobs are in **`LoRaWAN_End_Node_ADR/LoRaWAN/App/lab_config.h`**. You can override them from CMake without editing the file (the cache variables are empty by default):

| What | Header (`lab_config.h`) | CMake override |
|---|---|---|
| Region | `#define LAB_REGION_AU915` (or `_US915`, `_EU868`; all three are compiled into the MAC) | `-DLAB_REGION=US915` |
| Sub-band | `LAB_SUBBAND 2` (1..8 enables 8x125 kHz + its 500 kHz channel through the stack's HYBRID mask; 0 = all 72 channels) | `-DLAB_SUBBAND=1` |
| Uplink period | `LAB_APP_TX_DUTYCYCLE_MS 15000` | `-DLAB_APP_TX_DUTYCYCLE_MS=30000` |
| Tracing | `LAB_ADR_TRACE 1` (0 = every hook compiles to nothing) | `-DLAB_ADR_TRACE=0` |
| Payload | `LAB_SMALL_PAYLOAD 1` (0 = ST's original 10-byte payload) | (edit header) |

Example: `cmake --preset Debug -DLAB_SUBBAND=1 && cmake --build --preset Debug` (re-run with `-DLAB_SUBBAND=` to go back to the header value).

* **Keys / EUIs:** `LoRaWAN/App/se-identity.h`: `LORAWAN_DEVICE_EUI` (all zeros = derive from UID), `LORAWAN_JOIN_EUI`, `LORAWAN_APP_KEY`, `LORAWAN_NWK_KEY`. Flash with `-e all` after changing keys.
* **ADR on/off, confirmed uplinks, port, default DR/power, class:** `LoRaWAN/App/lora_app.h` (`LORAWAN_ADR_STATE`, `LORAWAN_DEFAULT_CONFIRMED_MSG_STATE`, `LORAWAN_USER_APP_PORT`, ...).
* **LoRaWAN version:** `LORAMAC_SPECIFICATION_VERSION` in `LoRaWAN/Target/lorawan_conf.h` (kept at 0x01000400).
* **Verbosity:** `VERBOSE_LEVEL` in `Core/Inc/sys_conf.h` (set to `VLEVEL_H`; the trace lines use `VLEVEL_M`).

Buttons (upstream behaviour, unchanged):

* B1 is unused in timer mode.
* B2 stops the stack and toggles OTAA/ABP.
* B3 stores the NVM context to flash at 0x0803F000. After that the MAC stays halted, so reset the board.

## 7. Trace lines

Every line starts with the tag, then `t=<s.ms>` (MCU RTC time, which continues across software resets). ST's own lines (`TX on freq...`, `RX_1 on...`, `###### ...`) are still printed.

| Tag / line | Emitted from | Meaning |
|---|---|---|
| `[JOIN] cfg ...` (5 lines at boot) | app, MIB reads | Region, sub-band, ADR, period. DevEUI/JoinEUI. Default ChMask and channel list. Default/min DR (with dwell time). UplinkDwellTime, DownlinkDwellTime, MaxEIRP, antenna gain. TX power table end points. ADR_ACK_LIMIT/DELAY. RX1 delay, RX2, PA configuration. |
| `[JOIN] JoinRequest ...` | `LoRaMac.c SendFrameOnChannel()` | DevEUI, JoinEUI, DevNonce, channel/frequency, DR (SF/BW), power, time on air |
| `[JOIN] JoinAccept in RX1 ...` + `after Join-Accept: ChMask=` | `ProcessRadioRxDone()` | DevAddr, NetID, DLSettings (RX1DROffset, RX2 DR), RxDelay, CFList raw/type or "absent", resulting channel mask |
| `[JOIN] JoinAccept REJECTED: ...` | `ProcessRadioRxDone()` | MIC/decrypt failure (wrong key) or invalid RX2 DR |
| `[JOIN] result=JOINED / FAILED ...` | app `OnJoinRequest`, MIB reads | DevAddr, DR, TX power, RX2, RX1 delay/offset, ChMask after the join |
| `[UL] FCnt=.. UNCONF port=.. len=.. tx=n/NbTrans ch=.. f=.. DRx(SF/BW) pwr=idxN EIRP=.. cond=.. rf=..(HP/LP[,CLAMPED]) ToA=.. ADR=.. ADRACKReq=.. ACK=.. ADR_ACK_CNT=c/LIMIT+DELAY ULdwell=.. FOpts=..` | `SendFrameOnChannel()`, right after `Radio.Send()` | One line per transmission, repetitions included. `EIRP` = MaxEIRP - 2*idx (what the NS thinks). `cond` = conducted power requested from the radio (EIRP - antenna gain, floored). `rf` = what the SubGHz PA really sets after HP/LP selection and clamping. `FOpts` = MAC answers sent in this frame. |
| `[UL] done FCnt=.. status=.. | MIB now: ADR DR TXPower NbTrans ChMask` | app `OnTxData`, MIB reads | End of the uplink cycle (RX windows closed) and the MAC state that will apply to the next uplink |
| `[DL] RX1 DRx rssi snr CONF/UNCONF FCntDown DevAddr FPort FRMPayload FOptsLen ACK ADR FPending ADR_ACK_CNT=a->b FOpts=` | `ProcessRadioRxDone()` (MIC ok) | Every accepted downlink. `ADR_ACK_CNT a->b` shows the reset caused by the downlink. |
| `[DL] RXn frame DROPPED by MAC: MIC_FAIL/ADDRESS_FAIL/DOWNLINK_REPEATED/...` | `PrepareRxDoneAbort()` | Received frame thrown away by the MAC |
| `[MAC] RX n byte(s) of MAC commands in RX1 ... : <hex>` | `ProcessMacCommands()` entry | Whole MAC command buffer (FOpts or FPort 0) and the number of commands. Commands outside RX1/RX2 are flagged as IGNORED (1.0.4 rule). |
| `[MAC]   <Name> raw=<hex> <decoded>` | same | One line per command. LinkADRReq shows `[block b, cmd i/n]` and DataRate/TXPower (EIRP)/ChMask/ChMaskCntl/NbTrans. TxParamSetupReq shows Up/Downlink dwell + MaxEIRP. RXParamSetupReq, DutyCycleReq, NewChannelReq, DlChannelReq, RXTimingSetupReq, DevStatusReq, LinkCheckAns, DeviceTimeAns are decoded too. An unknown CID is flagged (the stack stops parsing there). |
| `[ADR] LinkADRReq block of n cmd(s) -> LinkADRAns status=0x07 (PowerACK DataRateACK ChMaskACK) xn => APPLIED: DRa->DRb TXPower a->b NbTrans a->b ChMask(now)=.. ch=..` or `=> REJECTED, nothing changed ... reason: ...` | after `RegionLinkAdrReq()` | Outcome of the LinkADRReq block (applied atomically), with the reason for each NACK bit |
| `[ADR] TxParamSetupReq 0x.. APPLIED: UplinkDwellTime DownlinkDwellTime MaxEIRP -> min UL DR now ...` | TxParamSetupReq handler | Dwell/EIRP change and its effect on the minimum DR |
| `[ADR] device ADR back-off: ADR_ACK_CNT=.. ADRACKReq=1 TXPower a->b DRa->DRb ...` | `Send()` after `LoRaMacAdrCalcNext()` | The device's own ADR fallback: no downlink for ADR_ACK_LIMIT uplinks, then power and DR step-downs |
| `[MAC] queue <Answer> (CID ..) payload=.. [decoded] (sticky)` | `LoRaMacCommands.c LoRaMacCommandsAddCmd()` | Every MAC answer or request queued for the next uplink (LinkADRAns status bits, RXParamSetupAns, DevStatusAns battery/margin, ...) |

Real example (first boot, see `logs/boot_capture_2026-10-27.log`):

```
[DL] t=48.440 RX1 DR10(SF10/500) rssi=-52 snr=7 CONF FCntDown=1 DevAddr=0146BD4D FPort=0 FRMPayload=10B FOptsLen=0 ACK=0 ADR=0 FPending=0 ADR_ACK_CNT=0->0
[MAC] t=48.441 RX 10 byte(s) of MAC commands in RX1 (snr=7), 2 parsable cmd(s): 0320020071032000FF01
[MAC] t=48.441   LinkADRReq [block 1, cmd 1/2] raw=0320020071 DataRate=DR2(SF10/125) TXPower=0(EIRP 30.0dBm) ChMask=0x0002 ChMaskCntl=7 NbTrans=1
[MAC] t=48.441   LinkADRReq [block 1, cmd 2/2] raw=032000FF01 DataRate=DR2(SF10/125) TXPower=0(EIRP 30.0dBm) ChMask=0xFF00 ChMaskCntl=0 NbTrans=1
[ADR] t=48.442 LinkADRReq block of 2 cmd(s) -> LinkADRAns status=0x07 (PowerACK=1 DataRateACK=1 ChMaskACK=1) x2 => APPLIED: DR2(SF10/125)->DR2(SF10/125) TXPower 0->0 NbTrans 1->1 ChMask(now)=FF00.0000.0000.0000.0002 ch=8-15,65
[MAC] t=48.442 queue LinkADRAns (CID 0x03) payload=07 [PowerACK=1 DataRateACK=1 ChMaskACK=1]
[UL] t=48.449 FCnt=2 UNCONF port=0 len=4 tx=1/1 ch=12 f=917.600MHz DR2(SF10/125) pwr=idx0 EIRP=30.0dBm cond=27dBm rf=22dBm(HP,CLAMPED) ToA=330ms ADR=1 ADRACKReq=0 ACK=1 ADR_ACK_CNT=0/64+32 ULdwell=1 (FPort 0 = MAC cmds only, see [MAC] queue lines)
```

## 8. How auditable and removable the instrumentation is

* Every change to an upstream file is marked `LAB:` (configuration) or `/* ADR-TRACE */` (tracing). List them with `grep -rn "ADR-TRACE\|LAB:" LoRaWAN_End_Node_ADR/{Core,LoRaWAN,Middlewares}`.
* Stack files touched: only `Middlewares/Third_Party/LoRaWAN/Mac/LoRaMac.c` (12 hook calls, some wrapped over 2-4 lines, + 1 include) and `LoRaMacCommands.c` (1 hook + 1 include). Every hook is `ADR_TRACE( AdrTrace_Xxx(...) );`. It only passes values to `adr_trace.c`, which reads state through MIB getters and `RegionGetPhyParam()` and never writes MAC state. The uplink hook runs after `Radio.Send()`, and RX hooks run in the deferred `LoRaMacProcess()` context, so RX window timing is not affected.
* `-DLAB_ADR_TRACE=0` makes every hook expand to nothing, leaving a functionally upstream stack (flash shrinks from 93.0 KB to 81.1 KB).
* `patches/adr-trace.diff` is `diff -ruN` of all modified or added firmware sources against a pristine assembly of STM32CubeWL v1.4.0. Regenerate it with `tools/gen_adr_trace_diff.sh`. To remove it: `cd LoRaWAN_End_Node_ADR && patch -R -p2 < ../patches/adr-trace.diff`.
* The trace FIFO was enlarged from 1 KB to 4 KB (`UTIL_ADV_TRACE_FIFO_SIZE`) so bursts of `[MAC]` lines are not dropped.

## 9. Restore the teammate demo

`backup/teammate_demo_backup.bin` is the full 256 KB flash read on 2026-10-27, read twice with identical results. SHA-256 is in `backup/teammate_demo_backup.bin.sha256`; the option bytes are in `backup/teammate_demo_option_bytes.txt`. It is a Zephyr application ("PoC Nivel GLS LoRa - Zephyr"): code at 0x08000000-0x08015800 plus a 2 KB data page at 0x0803E000.

```bash
cd firmware/backup && sha256sum -c teammate_demo_backup.bin.sha256
$PROG -c port=SWD sn=$SN mode=UR -e all -d teammate_demo_backup.bin 0x08000000 -v -rst
```

Option bytes were not changed, so they don't need restoring.

## 10. ADR-relevant behaviour of this stack (AU915)

* **Uplink dwell time.** `AU915_DEFAULT_UPLINK_DWELL_TIME = 1`, so the minimum uplink DR is DR2 (SF10) and DR2 carries at most 11 bytes. A LinkADRReq asking for DR0/DR1 is NACKed (DataRateACK=0), and per 1.0.4 the whole block is then discarded. Any AU915 TxParamSetupReq is accepted and can switch dwell time off.
* **TX power.** Index 0..14 maps to EIRP 30..2 dBm (MaxEIRP 30, 2 dB steps). The conducted request is EIRP - 2.15 dBi. On the NUCLEO, PA config is LP+HP (HP ≤ 22 dBm, LP ≤ 15 dBm), so idx0/1/2 all transmit **22 dBm** (clamped) and the PA switches HP→LP at idx6 (15 dBm).
* **Join.** Join requests alternate 8x DR2 (125 kHz) and 1x DR6 (500 kHz, ch65). Data uplinks start at the DR of the successful join.
* **Device ADR back-off.** ADRACKReq is set after 64 uplinks without a downlink (about 16 min at 15 s). TX power goes back to max after 96. DR steps down at 128 and then every 32 uplinks. At the minimum DR, the default (sub-band 2) channel mask and NbTrans=1 are restored.
* **LinkADRReq (1.0.4 code path).**
  * The first contiguous block is processed as one unit: ChMask commands are applied in order, and DR/TXPower/NbTrans come from the last command.
  * Every command gets a LinkADRAns with the same status. If any bit is 0, nothing is applied, not even the ChMask.
  * Later blocks in the same frame are ignored.
  * DR=0xF and TXPower=0xF mean "keep".
  * NbTrans=0 becomes 1.
  * The AU915 code also applies the FCC "at least 2 x 125 kHz channels when DR<6" check, so a ChMask leaving a single 125 kHz channel gets ChMaskACK=0.
  * MAC commands are only processed in RX1/RX2.
  * The downlink FCtrl.ADR bit is not used by the stack.
* **What the first live run showed.** A network server answered the join: NetID 000000, DevAddr 0146BD4D, RX2 = DR12 (SF8/500), RX1 delay 1 s, no CFList. It then sent a *confirmed* FPort-0 downlink carrying a 2-command LinkADRReq block: ChMaskCntl=7 enabling ch65, then ChMaskCntl=0 with 0xFF00, DR2, TXPower 0, NbTrans 1. The device accepted it (0x07). Because the downlink was confirmed, the device answered at once with an extra FPort-0 uplink with ACK=1.
  During that run the local ChirpStack MQTT bus showed **no** gateway uplink events, and its `au915_1` config says `rx2_dr=8`. So the server that answered is most likely the Conduit's own built-in network server, not ChirpStack. Confirm this before attributing behaviour to ChirpStack.
  Over the next uplinks the same server ran ADR: at FCnt 6 it sent LinkADRReq DR2→DR3 with TXPower 0→1, and at FCnt 10 TXPower 1→2. Both were unconfirmed FPort-0 downlinks, and both were accepted. The `[UL]` lines show the radio output stayed at **22 dBm (HP, clamped)** through all three steps, so the server's link budget is off by 4 dB. See `logs/final_flash_capture_2026-10-27.log`.
