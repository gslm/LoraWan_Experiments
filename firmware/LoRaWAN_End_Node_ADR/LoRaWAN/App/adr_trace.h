/**
  ******************************************************************************
  * @file    adr_trace.h
  * @brief   ADR / MAC-command tracing for the ADR lab (ADR-TRACE).
  *
  * The LoRaMac-node stack files only contain one-line hook calls of the form
  *     ADR_TRACE( AdrTrace_Xxx( ... ) ); /- ADR-TRACE -/
  * All decoding/printing is done here, from data passed by the hook or read
  * through public MIB getters (LoRaMacMibGetRequestConfirm). The hooks never
  * write MAC state, so protocol behaviour is unchanged.
  *
  * With LAB_ADR_TRACE == 0 (lab_config.h / -DLAB_ADR_TRACE=0) ADR_TRACE(x)
  * expands to nothing: the stack is byte-for-byte functionally upstream.
  *
  * Output (UART2 / ST-LINK VCP, 115200 8N1), one line per event, tag first:
  *   [JOIN] join request / join-accept / join result / boot configuration
  *   [UL]   every radio uplink (incl. NbTrans repetitions) + MCPS confirm
  *   [DL]   every downlink accepted or dropped by the MAC
  *   [MAC]  every MAC command received (decoded) and every answer queued
  *   [ADR]  LinkADRReq block outcome, TxParamSetupReq outcome, ADR back-off
  ******************************************************************************
  */
#ifndef ADR_TRACE_H
#define ADR_TRACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "lab_config.h"

#if (LAB_ADR_TRACE == 1)

#include "LoRaMacMessageTypes.h"

/* ---- hooks called from the stack (Middlewares/Third_Party/LoRaWAN) ------- */

/* LoRaMac.c ProcessMacCommands(): whole MAC command buffer (FOpts or FPort 0) */
void AdrTrace_MacCommandsRx(const uint8_t *payload, uint8_t macIndex, uint8_t commandsSize,
                            int8_t snr, uint8_t rxSlot);

/* LoRaMac.c ProcessMacCommands(): result of RegionLinkAdrReq() for the LinkADRReq block,
 * called BEFORE the MAC copies the result into its NVM (cur* = values in use until now). */
void AdrTrace_LinkAdrResult(uint8_t status, uint8_t nbBytesParsed,
                            int8_t drOut, int8_t txPowOut, uint8_t nbRepOut,
                            int8_t curDr, int8_t curTxPow, uint8_t curNbTrans,
                            bool adrOn, uint8_t uplinkDwellTime);

/* LoRaMac.c ProcessMacCommands(): 2nd/3rd LinkADRReq block in the same frame (ignored by spec) */
void AdrTrace_LinkAdrBlockIgnored(const uint8_t *cmd);

/* LoRaMac.c ProcessMacCommands(): TxParamSetupReq accepted and applied */
void AdrTrace_TxParamSetup(uint8_t eirpDwellTime, uint8_t uplinkDwellTime, uint8_t downlinkDwellTime,
                           float maxEirp, int8_t datarateAfter);

/* LoRaMacCommands.c LoRaMacCommandsAddCmd(): every uplink MAC command queued (answers + requests) */
void AdrTrace_MacCmdQueued(uint8_t cid, const uint8_t *payload, uint32_t payloadSize, bool sticky);

/* LoRaMac.c Send(): after LoRaMacAdrCalcNext() (device-side ADR back-off) */
void AdrTrace_AdrCalcNext(int8_t drBefore, int8_t txPowBefore, uint8_t nbTransBefore,
                          int8_t drAfter, int8_t txPowAfter, uint8_t nbTransAfter,
                          bool adrAckReq, uint32_t adrAckCounter, uint16_t adrAckLimit,
                          uint16_t adrAckDelay);

/* LoRaMac.c SendFrameOnChannel(): every frame handed to the radio (join request or data) */
void AdrTrace_Uplink(const LoRaMacMessage_t *msg, uint8_t channel, int8_t txPowerIdxUsed,
                     uint8_t nbTransCounter, uint32_t timeOnAirMs);

/* LoRaMac.c ProcessRadioRxDone(): Join-Accept accepted (after CFList applied) */
void AdrTrace_JoinAccept(const LoRaMacMessageJoinAccept_t *ja, uint16_t size, int16_t rssi,
                         int8_t snr, uint8_t rxSlot);

/* LoRaMac.c ProcessRadioRxDone(): Join-Accept rejected */
void AdrTrace_JoinAcceptRejected(const char *reason, int status);

/* LoRaMac.c ProcessRadioRxDone(): data downlink accepted (MIC ok), before ADR_ACK_CNT reset */
void AdrTrace_Downlink(const LoRaMacMessageData_t *msg, uint8_t mhdr, uint8_t fType, uint8_t rxSlot,
                       int8_t rxDr, int16_t rssi, int8_t snr, uint32_t fCntDown, bool multicast,
                       uint32_t adrAckCounterBefore);

/* LoRaMac.c PrepareRxDoneAbort(): received frame dropped by the MAC */
void AdrTrace_RxAbort(int mcpsStatus, uint8_t rxSlot);

/* ---- application-layer helpers (lora_app.c), MIB based ------------------- */
struct LmHandlerTxParams_s;
struct LmHandlerJoinParams_s;
void AdrTrace_PrintConfig(void);
void AdrTrace_AppTxConfirm(const struct LmHandlerTxParams_s *params);
void AdrTrace_AppJoinResult(const struct LmHandlerJoinParams_s *params);

#define ADR_TRACE(call)   do { call; } while (0)

#else  /* LAB_ADR_TRACE == 0 */

#define ADR_TRACE(call)   do { } while (0)

#endif /* LAB_ADR_TRACE */

#ifdef __cplusplus
}
#endif

#endif /* ADR_TRACE_H */
