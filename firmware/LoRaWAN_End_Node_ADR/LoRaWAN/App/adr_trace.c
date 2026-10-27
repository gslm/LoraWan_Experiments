/**
  ******************************************************************************
  * @file    adr_trace.c
  * @brief   ADR / MAC-command tracing for the ADR lab (ADR-TRACE). See adr_trace.h.
  *
  * Rules followed here:
  *  - read-only: only MIB *get* requests and RegionGetPhyParam() queries,
  *  - one APP_LOG() per output line (lines are assembled in a local buffer first),
  *  - stm32_tiny_vsnprintf is built with TINY_PRINTF: no %l, no %-, no precision,
  *    so 32-bit values are printed with %u / %d and explicit casts.
  ******************************************************************************
  */
#include "adr_trace.h"

#if (LAB_ADR_TRACE == 1)

#include <stdarg.h>
#include <string.h>
#include "sys_app.h"                /* APP_LOG */
#include "utilities_def.h"
#include "stm32_tiny_vsnprintf.h"
#include "stm32_systime.h"
#include "LoRaMac.h"
#include "LoRaMacCommands.h"
#include "Region.h"
#include "RegionCommon.h"
#include "LmHandler.h"
#include "radio_board_if.h"
#include "lora_app.h"

/* ------------------------------------------------------------------------- */
/* small line builder                                                        */
/* ------------------------------------------------------------------------- */
#define LINE_MAX 420U   /* < UTIL_ADV_TRACE_TMP_BUF_SIZE (512) */

typedef struct
{
  char b[LINE_MAX];
  int  n;
} Line_t;

static void L_init(Line_t *l)
{
  l->n = 0;
  l->b[0] = '\0';
}

static void L_add(Line_t *l, const char *fmt, ...)
{
  va_list ap;
  int room = (int)LINE_MAX - l->n;
  if (room <= 1)
  {
    return;
  }
  va_start(ap, fmt);
  int r = tiny_vsnprintf_like(&l->b[l->n], room, fmt, ap);
  va_end(ap);
  if (r > 0)
  {
    l->n += r;
    if (l->n > (int)LINE_MAX - 1)
    {
      l->n = (int)LINE_MAX - 1;
    }
  }
}

static void L_hex(Line_t *l, const uint8_t *p, uint32_t n)
{
  for (uint32_t i = 0; i < n; i++)
  {
    L_add(l, "%02X", p[i]);
  }
}

static void L_emit(Line_t *l)
{
  APP_LOG(TS_OFF, VLEVEL_M, "%s\r\n", l->b);
}

/* "[TAG] t=123.456 " */
static void L_start(Line_t *l, const char *tag)
{
  SysTime_t t = SysTimeGetMcuTime();
  L_init(l);
  L_add(l, "%s t=%u.%03u ", tag, (unsigned)t.Seconds, (unsigned)t.SubSeconds);
}

/* value in tenths -> "12.3" / "-0.5" */
static void L_tenths(Line_t *l, int v10)
{
  if (v10 < 0)
  {
    L_add(l, "-");
    v10 = -v10;
  }
  L_add(l, "%d.%d", v10 / 10, v10 % 10);
}

static int ToTenths(float f)
{
  return (int)((f * 10.0f) + ((f >= 0.0f) ? 0.5f : -0.5f));
}

/* value in hundredths -> "2.15" */
static void L_hundredths(Line_t *l, float f)
{
  int v100 = (int)((f * 100.0f) + ((f >= 0.0f) ? 0.5f : -0.5f));
  if (v100 < 0)
  {
    L_add(l, "-");
    v100 = -v100;
  }
  L_add(l, "%d.%02d", v100 / 100, v100 % 100);
}

/* ------------------------------------------------------------------------- */
/* MAC / region helpers (read-only)                                          */
/* ------------------------------------------------------------------------- */
static LoRaMacNvmData_t *Nvm(void)
{
  MibRequestConfirm_t mib;
  mib.Type = MIB_NVM_CTXS;
  if (LoRaMacMibGetRequestConfirm(&mib) != LORAMAC_STATUS_OK)
  {
    return NULL;
  }
  return mib.Param.Contexts;
}

static LoRaMacRegion_t CurRegion(void)
{
  LoRaMacNvmData_t *nvm = Nvm();
  return (nvm != NULL) ? nvm->MacGroup2.Region : LAB_ACTIVE_REGION;
}

static uint32_t PhyGet(PhyAttribute_t attr, int8_t dr, uint8_t dwell)
{
  GetPhyParams_t g;
  PhyParam_t p;
  memset(&g, 0, sizeof(g));
  g.Attribute = attr;
  g.Datarate = dr;
  g.UplinkDwellTime = dwell;
  g.DownlinkDwellTime = dwell;
  p = RegionGetPhyParam(CurRegion(), &g);
  return p.Value;
}

/* lowest-power TX power index of the region (TX_POWER_0 = max EIRP) */
static int MaxTxPowerIdx(void)
{
  switch (CurRegion())
  {
    case LORAMAC_REGION_AU915:
    case LORAMAC_REGION_US915:
      return 14;
    case LORAMAC_REGION_EU868:
      return 7;
    default:
      return 15;
  }
}

/* "DR5(SF7/125)" */
static void L_dr(Line_t *l, int dr)
{
  if ((dr < 0) || (dr > 15))
  {
    L_add(l, "DR?%d", dr);
    return;
  }
  if (dr == 15)
  {
    L_add(l, "DR15(keep)");
    return;
  }
  uint32_t sf = PhyGet(PHY_SF_FROM_DR, (int8_t)dr, 0);
  uint32_t bw = PhyGet(PHY_BW_FROM_DR, (int8_t)dr, 0);
  if (sf == 0U)
  {
    L_add(l, "DR%d(non-LoRa/RFU)", dr);
  }
  else
  {
    L_add(l, "DR%d(SF%u/%u)", dr, (unsigned)sf, (bw == 2U) ? 500U : ((bw == 1U) ? 250U : 125U));
  }
}

/* "917.200MHz" */
static void L_freq(Line_t *l, uint32_t hz)
{
  L_add(l, "%u.%03uMHz", (unsigned)(hz / 1000000U), (unsigned)((hz / 1000U) % 1000U));
}

static uint32_t ChannelFreq(uint8_t ch)
{
  MibRequestConfirm_t mib;
  mib.Type = MIB_CHANNELS;
  if ((LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) && (mib.Param.ChannelList != NULL))
  {
    return mib.Param.ChannelList[ch].Frequency;
  }
  return 0;
}

static uint8_t MaskWords(LoRaMacRegion_t r)
{
  return ((r == LORAMAC_REGION_AU915) || (r == LORAMAC_REGION_US915)) ? 5U : 1U;
}

/* current channel mask "FF00.0000.0000.0000.0002" */
static void L_chmask(Line_t *l)
{
  MibRequestConfirm_t mib;
  mib.Type = MIB_CHANNELS_MASK;
  if ((LoRaMacMibGetRequestConfirm(&mib) != LORAMAC_STATUS_OK) || (mib.Param.ChannelsMask == NULL))
  {
    L_add(l, "?");
    return;
  }
  uint8_t n = MaskWords(CurRegion());
  for (uint8_t i = 0; i < n; i++)
  {
    L_add(l, "%s%04X", (i == 0U) ? "" : ".", mib.Param.ChannelsMask[i]);
  }
}

/* enabled channel list "8-15,65" */
static void L_chlist(Line_t *l)
{
  MibRequestConfirm_t mib;
  mib.Type = MIB_CHANNELS_MASK;
  if ((LoRaMacMibGetRequestConfirm(&mib) != LORAMAC_STATUS_OK) || (mib.Param.ChannelsMask == NULL))
  {
    return;
  }
  uint8_t n = (uint8_t)(MaskWords(CurRegion()) * 16U);
  int runStart = -1;
  bool first = true;
  for (int ch = 0; ch <= n; ch++)
  {
    bool on = (ch < n) && ((mib.Param.ChannelsMask[ch / 16] & (1U << (ch % 16))) != 0U);
    if (on && (runStart < 0))
    {
      runStart = ch;
    }
    if (!on && (runStart >= 0))
    {
      if ((ch - 1) == runStart)
      {
        L_add(l, "%s%d", first ? "" : ",", runStart);
      }
      else
      {
        L_add(l, "%s%d-%d", first ? "" : ",", runStart, ch - 1);
      }
      first = false;
      runStart = -1;
    }
  }
  if (first)
  {
    L_add(l, "none");
  }
}

/* TX power index -> spec EIRP, conducted request, what the SubGHz radio really does */
static void L_power(Line_t *l, int8_t idx)
{
  LoRaMacNvmData_t *nvm = Nvm();
  if (nvm == NULL)
  {
    L_add(l, "pwr=idx%d", idx);
    return;
  }
  float maxEirp = nvm->MacGroup2.MacParams.MaxEirp;
  float antGain = nvm->MacGroup2.MacParams.AntennaGain;
  int eirp10 = ToTenths(maxEirp) - (20 * idx);
  int8_t phy = RegionCommonComputeTxPower(idx, maxEirp, antGain);

  /* replicate SUBGRF_SetRfTxPower()/SUBGRF_SetTxParams() PA selection and clamping */
  int32_t cfg = RBI_GetTxConfig();
  bool hp = (cfg == RBI_CONF_RFO_HP) || ((cfg == RBI_CONF_RFO_LP_HP) && (phy > 15));
  int32_t max = RBI_GetRFOMaxPowerConfig(hp ? RBI_RFO_HP_MAXPOWER : RBI_RFO_LP_MAXPOWER);
  int32_t rf = phy;
  if (rf > max)
  {
    rf = max;
  }
  if (hp && (rf < -9))
  {
    rf = -9;
  }
  if (!hp && (rf < -17))
  {
    rf = -17;
  }
  L_add(l, "pwr=idx%d EIRP=", idx);
  L_tenths(l, eirp10);
  L_add(l, "dBm cond=%ddBm rf=%ddBm(%s%s)", phy, (int)rf, hp ? "HP" : "LP", (rf != phy) ? ",CLAMPED" : "");
}

static const char *SlotStr(uint8_t s)
{
  static const char *n[] = { "RX1", "RX2", "RXC", "RXC_MC", "PING", "PING_MC", "NONE" };
  return (s <= RX_SLOT_NONE) ? n[s] : "RX?";
}

static const char *StatusStr(int s)
{
  switch (s)
  {
    case LORAMAC_EVENT_INFO_STATUS_OK:                       return "OK";
    case LORAMAC_EVENT_INFO_STATUS_ERROR:                    return "ERROR";
    case LORAMAC_EVENT_INFO_STATUS_TX_TIMEOUT:               return "TX_TIMEOUT";
    case LORAMAC_EVENT_INFO_STATUS_RX1_TIMEOUT:              return "RX1_TIMEOUT";
    case LORAMAC_EVENT_INFO_STATUS_RX2_TIMEOUT:              return "RX2_TIMEOUT";
    case LORAMAC_EVENT_INFO_STATUS_RX1_ERROR:                return "RX1_ERROR";
    case LORAMAC_EVENT_INFO_STATUS_RX2_ERROR:                return "RX2_ERROR";
    case LORAMAC_EVENT_INFO_STATUS_JOIN_FAIL:                return "JOIN_FAIL";
    case LORAMAC_EVENT_INFO_STATUS_DOWNLINK_REPEATED:        return "DOWNLINK_REPEATED";
    case LORAMAC_EVENT_INFO_STATUS_TX_DR_PAYLOAD_SIZE_ERROR: return "TX_DR_PAYLOAD_SIZE_ERROR";
    case LORAMAC_EVENT_INFO_STATUS_ADDRESS_FAIL:             return "ADDRESS_FAIL";
    case LORAMAC_EVENT_INFO_STATUS_MIC_FAIL:                 return "MIC_FAIL";
    case LORAMAC_EVENT_INFO_STATUS_MULTICAST_FAIL:           return "MULTICAST_FAIL";
    default:                                                 return "OTHER";
  }
}

/* ------------------------------------------------------------------------- */
/* MAC command names                                                         */
/* ------------------------------------------------------------------------- */
static const char *SrvCmdName(uint8_t cid)
{
  switch (cid)
  {
    case 0x01: return "ResetConf";
    case 0x02: return "LinkCheckAns";
    case 0x03: return "LinkADRReq";
    case 0x04: return "DutyCycleReq";
    case 0x05: return "RXParamSetupReq";
    case 0x06: return "DevStatusReq";
    case 0x07: return "NewChannelReq";
    case 0x08: return "RXTimingSetupReq";
    case 0x09: return "TxParamSetupReq";
    case 0x0A: return "DlChannelReq";
    case 0x0B: return "RekeyConf";
    case 0x0C: return "ADRParamSetupReq";
    case 0x0D: return "DeviceTimeAns";
    case 0x0E: return "ForceRejoinReq";
    case 0x0F: return "RejoinParamSetupReq";
    case 0x10: return "PingSlotInfoAns";
    case 0x11: return "PingSlotChannelReq";
    case 0x12: return "BeaconTimingAns";
    case 0x13: return "BeaconFreqReq";
    case 0x20: return "DeviceModeConf";
    default:   return (cid >= 0x80U) ? "Proprietary" : "Unknown";
  }
}

static const char *MoteCmdName(uint8_t cid)
{
  switch (cid)
  {
    case 0x01: return "ResetInd";
    case 0x02: return "LinkCheckReq";
    case 0x03: return "LinkADRAns";
    case 0x04: return "DutyCycleAns";
    case 0x05: return "RXParamSetupAns";
    case 0x06: return "DevStatusAns";
    case 0x07: return "NewChannelAns";
    case 0x08: return "RXTimingSetupAns";
    case 0x09: return "TxParamSetupAns";
    case 0x0A: return "DlChannelAns";
    case 0x0B: return "RekeyInd";
    case 0x0C: return "ADRParamSetupAns";
    case 0x0D: return "DeviceTimeReq";
    case 0x0F: return "RejoinParamSetupAns";
    case 0x10: return "PingSlotInfoReq";
    case 0x11: return "PingSlotChannelAns";
    case 0x12: return "BeaconTimingReq";
    case 0x13: return "BeaconFreqAns";
    case 0x20: return "DeviceModeInd";
    default:   return "Unknown";
  }
}

static const uint8_t MaxEirpTable[16] = { 8, 10, 12, 13, 14, 16, 18, 20, 21, 24, 26, 27, 29, 30, 33, 36 };

static uint32_t Le24(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

/* decoded fields of one downlink (server -> device) MAC command */
static void L_decodeSrv(Line_t *l, const uint8_t *c, uint8_t size)
{
  switch (c[0])
  {
    case 0x02: /* LinkCheckAns */
      if (size >= 3U)
      {
        L_add(l, " Margin=%udB GwCnt=%u", c[1], c[2]);
      }
      break;
    case 0x03: /* LinkADRReq */
      if (size >= 5U)
      {
        int dr = (c[1] >> 4) & 0x0F;
        int txp = c[1] & 0x0F;
        uint16_t chMask = (uint16_t)(c[2] | ((uint16_t)c[3] << 8));
        L_add(l, " DataRate=");
        L_dr(l, dr);
        if (txp == 15)
        {
          L_add(l, " TXPower=15(keep)");
        }
        else
        {
          LoRaMacNvmData_t *nvm = Nvm();
          L_add(l, " TXPower=%d", txp);
          if (nvm != NULL)
          {
            L_add(l, "(EIRP ");
            L_tenths(l, ToTenths(nvm->MacGroup2.MacParams.MaxEirp) - (20 * txp));
            L_add(l, "dBm)");
          }
        }
        L_add(l, " ChMask=0x%04X ChMaskCntl=%u NbTrans=%u%s", chMask, (c[4] >> 4) & 0x07U, c[4] & 0x0FU,
              ((c[4] & 0x0FU) == 0U) ? "(0=keep/1)" : "");
      }
      break;
    case 0x04: /* DutyCycleReq */
      if (size >= 2U)
      {
        L_add(l, " MaxDutyCycle=%u (aggregated 1/%u)", c[1] & 0x0FU, 1U << (c[1] & 0x0FU));
      }
      break;
    case 0x05: /* RXParamSetupReq */
      if (size >= 5U)
      {
        L_add(l, " RX1DROffset=%u RX2DataRate=", (c[1] >> 4) & 0x07U);
        L_dr(l, c[1] & 0x0F);
        L_add(l, " Freq=");
        L_freq(l, Le24(&c[2]) * 100U);
      }
      break;
    case 0x07: /* NewChannelReq */
      if (size >= 6U)
      {
        L_add(l, " ChIndex=%u Freq=", c[1]);
        L_freq(l, Le24(&c[2]) * 100U);
        L_add(l, " DR=%u..%u", c[5] & 0x0FU, (c[5] >> 4) & 0x0FU);
      }
      break;
    case 0x08: /* RXTimingSetupReq */
      if (size >= 2U)
      {
        L_add(l, " Del=%us", ((c[1] & 0x0FU) == 0U) ? 1U : (c[1] & 0x0FU));
      }
      break;
    case 0x09: /* TxParamSetupReq */
      if (size >= 2U)
      {
        L_add(l, " DownlinkDwellTime=%u UplinkDwellTime=%u MaxEIRP=%udBm(idx %u)",
              (c[1] >> 5) & 1U, (c[1] >> 4) & 1U, MaxEirpTable[c[1] & 0x0FU], c[1] & 0x0FU);
      }
      break;
    case 0x0A: /* DlChannelReq */
      if (size >= 5U)
      {
        L_add(l, " ChIndex=%u Freq=", c[1]);
        L_freq(l, Le24(&c[2]) * 100U);
      }
      break;
    case 0x0C: /* ADRParamSetupReq (1.1) */
      if (size >= 2U)
      {
        L_add(l, " ADR_ACK_LIMIT=%u ADR_ACK_DELAY=%u", 1U << ((c[1] >> 4) & 0x0FU), 1U << (c[1] & 0x0FU));
      }
      break;
    case 0x0D: /* DeviceTimeAns */
      if (size >= 6U)
      {
        uint32_t s = (uint32_t)c[1] | ((uint32_t)c[2] << 8) | ((uint32_t)c[3] << 16) | ((uint32_t)c[4] << 24);
        L_add(l, " GPSsec=%u frac=%u/256", (unsigned)s, c[5]);
      }
      break;
    case 0x11: /* PingSlotChannelReq */
      if (size >= 5U)
      {
        L_add(l, " Freq=");
        L_freq(l, Le24(&c[1]) * 100U);
        L_add(l, " DR=%u", c[4] & 0x0FU);
      }
      break;
    case 0x13: /* BeaconFreqReq */
      if (size >= 4U)
      {
        L_add(l, " Freq=");
        L_freq(l, Le24(&c[1]) * 100U);
      }
      break;
    default:
      break;
  }
}

/* ------------------------------------------------------------------------- */
/* hooks                                                                     */
/* ------------------------------------------------------------------------- */
void AdrTrace_MacCommandsRx(const uint8_t *payload, uint8_t macIndex, uint8_t commandsSize,
                            int8_t snr, uint8_t rxSlot)
{
  Line_t l;
  if ((payload == NULL) || (macIndex >= commandsSize))
  {
    return;
  }
  /* pass 1: count commands / LinkADRReq blocks exactly like the stack parses them */
  uint8_t nCmds = 0;
  uint8_t i = macIndex;
  while (i < commandsSize)
  {
    uint8_t sz = LoRaMacCommandsGetCmdSize(payload[i]);
    if ((sz == 0U) || ((uint16_t)i + sz > commandsSize))
    {
      break;
    }
    nCmds++;
    i = (uint8_t)(i + sz);
  }

  L_start(&l, "[MAC]");
  L_add(&l, "RX %u byte(s) of MAC commands in %s (snr=%d), %u parsable cmd(s): ",
        (unsigned)(commandsSize - macIndex), SlotStr(rxSlot), snr, nCmds);
  L_hex(&l, &payload[macIndex], (uint32_t)(commandsSize - macIndex));
  if ((rxSlot != RX_SLOT_WIN_1) && (rxSlot != RX_SLOT_WIN_2))
  {
    L_add(&l, "  => IGNORED by stack (L2 1.0.4: MAC cmds only processed in RX1/RX2)");
  }
  L_emit(&l);

  /* pass 2: one line per command */
  uint8_t blockNo = 0;
  uint8_t posInBlock = 0;
  uint8_t blockLen = 0;
  bool prevWasAdr = false;
  i = macIndex;
  while (i < commandsSize)
  {
    uint8_t cid = payload[i];
    uint8_t sz = LoRaMacCommandsGetCmdSize(cid);
    L_start(&l, "[MAC]");
    if ((sz == 0U) || ((uint16_t)i + sz > commandsSize))
    {
      L_add(&l, "  CID 0x%02X (%s) unknown to this stack build or truncated -> stack STOPS parsing here, rest ignored: ",
            cid, SrvCmdName(cid));
      L_hex(&l, &payload[i], (uint32_t)(commandsSize - i));
      L_emit(&l);
      break;
    }
    if (cid == SRV_MAC_LINK_ADR_REQ)
    {
      if (!prevWasAdr)
      {
        /* new contiguous block: measure it */
        blockNo++;
        posInBlock = 0;
        blockLen = 0;
        uint8_t j = i;
        while (((uint16_t)j + 5U <= commandsSize) && (payload[j] == SRV_MAC_LINK_ADR_REQ))
        {
          blockLen++;
          j = (uint8_t)(j + 5U);
        }
      }
      posInBlock++;
      L_add(&l, "  %s [block %u, cmd %u/%u%s] raw=", SrvCmdName(cid), blockNo, posInBlock, blockLen,
            (blockNo > 1U) ? ", IGNORED: only 1st block is processed" : "");
      prevWasAdr = true;
    }
    else
    {
      L_add(&l, "  %s raw=", SrvCmdName(cid));
      prevWasAdr = false;
    }
    L_hex(&l, &payload[i], sz);
    L_decodeSrv(&l, &payload[i], sz);
    L_emit(&l);
    i = (uint8_t)(i + sz);
  }
}

void AdrTrace_LinkAdrResult(uint8_t status, uint8_t nbBytesParsed,
                            int8_t drOut, int8_t txPowOut, uint8_t nbRepOut,
                            int8_t curDr, int8_t curTxPow, uint8_t curNbTrans,
                            bool adrOn, uint8_t uplinkDwellTime)
{
  Line_t l;
  bool ok = ((status & 0x07U) == 0x07U);
  L_start(&l, "[ADR]");
  L_add(&l, "LinkADRReq block of %u cmd(s) -> LinkADRAns status=0x%02X (PowerACK=%u DataRateACK=%u ChMaskACK=%u) x%u => ",
        (unsigned)(nbBytesParsed / 5U), status, (status >> 2) & 1U, (status >> 1) & 1U, status & 1U,
        (unsigned)(nbBytesParsed / 5U));
  if (ok)
  {
    if (adrOn)
    {
      L_add(&l, "APPLIED: ");
      L_dr(&l, curDr);
      L_add(&l, "->");
      L_dr(&l, drOut);
      L_add(&l, " TXPower %d->%d NbTrans %u->%u ChMask(now)=", curTxPow, txPowOut, curNbTrans, nbRepOut);
    }
    else
    {
      L_add(&l, "ADR OFF: only ChMask applied (DR/TXPower/NbTrans ignored) ChMask(now)=");
    }
    L_chmask(&l);
    L_add(&l, " ch=");
    L_chlist(&l);
  }
  else
  {
    L_add(&l, "REJECTED, nothing changed (keeps ");
    L_dr(&l, curDr);
    L_add(&l, " TXPower=%d NbTrans=%u ChMask=", curTxPow, curNbTrans);
    L_chmask(&l);
    L_add(&l, ") reason:");
    if ((status & 0x02U) == 0U)
    {
      L_add(&l, " DR ");
      L_dr(&l, drOut);
      L_add(&l, " not allowed (min UL DR with UplinkDwellTime=%u is DR%u; or no channel enabled by the new ChMask supports it)",
            uplinkDwellTime, (unsigned)PhyGet(PHY_MIN_TX_DR, 0, uplinkDwellTime));
    }
    if ((status & 0x04U) == 0U)
    {
      L_add(&l, " TXPower %d out of range (0..%d)", txPowOut, MaxTxPowerIdx());
    }
    if ((status & 0x01U) == 0U)
    {
      L_add(&l, " ChMask invalid (e.g. <2 x 125kHz ch for DR<6, all channels off, undefined channel)");
    }
  }
  L_emit(&l);
}

void AdrTrace_LinkAdrBlockIgnored(const uint8_t *cmd)
{
  Line_t l;
  L_start(&l, "[ADR]");
  L_add(&l, "additional LinkADRReq block IGNORED (only the first contiguous block is processed): ");
  L_hex(&l, cmd, 5U);
  L_emit(&l);
}

void AdrTrace_TxParamSetup(uint8_t eirpDwellTime, uint8_t uplinkDwellTime, uint8_t downlinkDwellTime,
                           float maxEirp, int8_t datarateAfter)
{
  Line_t l;
  L_start(&l, "[ADR]");
  L_add(&l, "TxParamSetupReq 0x%02X APPLIED: UplinkDwellTime=%u DownlinkDwellTime=%u MaxEIRP=",
        eirpDwellTime, uplinkDwellTime, downlinkDwellTime);
  L_tenths(&l, ToTenths(maxEirp));
  L_add(&l, "dBm -> min UL DR now DR%u, current ", (unsigned)PhyGet(PHY_MIN_TX_DR, 0, uplinkDwellTime));
  L_dr(&l, datarateAfter);
  L_emit(&l);
}

void AdrTrace_MacCmdQueued(uint8_t cid, const uint8_t *payload, uint32_t payloadSize, bool sticky)
{
  Line_t l;
  L_start(&l, "[MAC]");
  L_add(&l, "queue %s (CID 0x%02X) payload=", MoteCmdName(cid), cid);
  if (payloadSize == 0U)
  {
    L_add(&l, "-");
  }
  L_hex(&l, payload, payloadSize);
  if ((cid == MOTE_MAC_LINK_ADR_ANS) && (payloadSize >= 1U))
  {
    L_add(&l, " [PowerACK=%u DataRateACK=%u ChMaskACK=%u]", (payload[0] >> 2) & 1U, (payload[0] >> 1) & 1U,
          payload[0] & 1U);
  }
  else if ((cid == MOTE_MAC_RX_PARAM_SETUP_ANS) && (payloadSize >= 1U))
  {
    L_add(&l, " [RX1DROffsetACK=%u RX2DataRateACK=%u ChannelACK=%u]", (payload[0] >> 2) & 1U,
          (payload[0] >> 1) & 1U, payload[0] & 1U);
  }
  else if (((cid == MOTE_MAC_NEW_CHANNEL_ANS) || (cid == MOTE_MAC_DL_CHANNEL_ANS)) && (payloadSize >= 1U))
  {
    L_add(&l, " [%s=%u ChannelFreqOK=%u]", (cid == MOTE_MAC_NEW_CHANNEL_ANS) ? "DataRateRangeOK" : "UplinkFreqExists",
          (payload[0] >> 1) & 1U, payload[0] & 1U);
  }
  else if ((cid == MOTE_MAC_DEV_STATUS_ANS) && (payloadSize >= 2U))
  {
    int margin = (int)(payload[1] & 0x3FU);
    if (margin >= 32)
    {
      margin -= 64;
    }
    L_add(&l, " [Battery=%u%s Margin=%ddB]", payload[0],
          (payload[0] == 255U) ? "(unknown)" : ((payload[0] == 0U) ? "(ext power)" : ""), margin);
  }
  L_add(&l, "%s", sticky ? " (sticky: repeated until a downlink is received)" : "");
  L_emit(&l);
}

void AdrTrace_AdrCalcNext(int8_t drBefore, int8_t txPowBefore, uint8_t nbTransBefore,
                          int8_t drAfter, int8_t txPowAfter, uint8_t nbTransAfter,
                          bool adrAckReq, uint32_t adrAckCounter, uint16_t adrAckLimit,
                          uint16_t adrAckDelay)
{
  if ((drBefore == drAfter) && (txPowBefore == txPowAfter) && (nbTransBefore == nbTransAfter) && !adrAckReq)
  {
    return;
  }
  Line_t l;
  L_start(&l, "[ADR]");
  L_add(&l, "device ADR back-off: ADR_ACK_CNT=%u (LIMIT=%u DELAY=%u) ADRACKReq=%u", (unsigned)adrAckCounter,
        adrAckLimit, adrAckDelay, adrAckReq ? 1U : 0U);
  if (txPowBefore != txPowAfter)
  {
    L_add(&l, " TXPower %d->%d(default)", txPowBefore, txPowAfter);
  }
  if (drBefore != drAfter)
  {
    L_add(&l, " ");
    L_dr(&l, drBefore);
    L_add(&l, "->");
    L_dr(&l, drAfter);
  }
  if (nbTransBefore != nbTransAfter)
  {
    L_add(&l, " NbTrans %u->%u (+default ChMask restored)", nbTransBefore, nbTransAfter);
  }
  L_emit(&l);
}

void AdrTrace_Uplink(const LoRaMacMessage_t *msg, uint8_t channel, int8_t txPowerIdxUsed,
                     uint8_t nbTransCounter, uint32_t timeOnAirMs)
{
  Line_t l;
  LoRaMacNvmData_t *nvm = Nvm();
  if ((msg == NULL) || (nvm == NULL))
  {
    return;
  }
  int8_t dr = nvm->MacGroup1.ChannelsDatarate;

  if (msg->Type == LORAMAC_MSG_TYPE_JOIN_REQUEST)
  {
    const LoRaMacMessageJoinRequest_t *jr = &msg->Message.JoinReq;
    L_start(&l, "[JOIN]");
    L_add(&l, "JoinRequest DevEUI=");
    L_hex(&l, jr->DevEUI, LORAMAC_DEV_EUI_FIELD_SIZE);
    L_add(&l, " JoinEUI=");
    L_hex(&l, jr->JoinEUI, LORAMAC_JOIN_EUI_FIELD_SIZE);
    L_add(&l, " DevNonce=%u ch=%u f=", jr->DevNonce, channel);
    L_freq(&l, ChannelFreq(channel));
    L_add(&l, " ");
    L_dr(&l, dr);
    L_add(&l, " ");
    L_power(&l, txPowerIdxUsed);
    L_add(&l, " ToA=%ums ULdwell=%u", (unsigned)timeOnAirMs, nvm->MacGroup2.MacParams.UplinkDwellTime);
    L_emit(&l);
    return;
  }

  if (msg->Type != LORAMAC_MSG_TYPE_DATA)
  {
    L_start(&l, "[UL]");
    L_add(&l, "frame type %d ch=%u", (int)msg->Type, channel);
    L_emit(&l);
    return;
  }

  const LoRaMacMessageData_t *d = &msg->Message.Data;
  L_start(&l, "[UL]");
  L_add(&l, "FCnt=%u %s port=%u len=%u tx=%u/%u ch=%u f=", d->FHDR.FCnt,
        (d->MHDR.Bits.MType == FRAME_TYPE_DATA_CONFIRMED_UP) ? "CONF" : "UNCONF", d->FPort,
        d->FRMPayloadSize, nbTransCounter, nvm->MacGroup2.MacParams.ChannelsNbTrans, channel);
  L_freq(&l, ChannelFreq(channel));
  L_add(&l, " ");
  L_dr(&l, dr);
  L_add(&l, " ");
  L_power(&l, txPowerIdxUsed);
  if (txPowerIdxUsed != nvm->MacGroup1.ChannelsTxPower)
  {
    L_add(&l, "(MAC idx%d limited by band)", nvm->MacGroup1.ChannelsTxPower);
  }
  L_add(&l, " ToA=%ums ADR=%u ADRACKReq=%u ACK=%u ADR_ACK_CNT=%u/%u+%u ULdwell=%u",
        (unsigned)timeOnAirMs, d->FHDR.FCtrl.Bits.Adr, d->FHDR.FCtrl.Bits.AdrAckReq, d->FHDR.FCtrl.Bits.Ack,
        (unsigned)nvm->MacGroup1.AdrAckCounter, nvm->MacGroup2.MacParams.AdrAckLimit,
        nvm->MacGroup2.MacParams.AdrAckDelay, nvm->MacGroup2.MacParams.UplinkDwellTime);
  if (d->FHDR.FCtrl.Bits.FOptsLen > 0U)
  {
    L_add(&l, " FOpts=");
    L_hex(&l, d->FHDR.FOpts, d->FHDR.FCtrl.Bits.FOptsLen);
  }
  if (d->FPort == 0U)
  {
    L_add(&l, " (FPort 0 = MAC cmds only, see [MAC] queue lines)");
  }
  L_emit(&l);
}

void AdrTrace_JoinAccept(const LoRaMacMessageJoinAccept_t *ja, uint16_t size, int16_t rssi,
                         int8_t snr, uint8_t rxSlot)
{
  Line_t l;
  if (ja == NULL)
  {
    return;
  }
  L_start(&l, "[JOIN]");
  L_add(&l, "JoinAccept in %s rssi=%d snr=%d: DevAddr=%08X NetID=%02X%02X%02X DLSettings=0x%02X (RX1DROffset=%u RX2DataRate=",
        SlotStr(rxSlot), rssi, snr, (unsigned)ja->DevAddr, ja->NetID[2], ja->NetID[1], ja->NetID[0],
        ja->DLSettings.Value, ja->DLSettings.Bits.RX1DRoffset);
  L_dr(&l, ja->DLSettings.Bits.RX2DataRate);
  L_add(&l, ") RxDelay=%u(%us)", ja->RxDelay, ((ja->RxDelay & 0x0FU) == 0U) ? 1U : (ja->RxDelay & 0x0FU));
  if (size > 17U)
  {
    L_add(&l, " CFList(type %u: %s)=", ja->CFList[15], (ja->CFList[15] == 1U) ? "ChMask" : "freq list");
    L_hex(&l, ja->CFList, 16U);
  }
  else
  {
    L_add(&l, " CFList=absent");
  }
  L_emit(&l);

  L_start(&l, "[JOIN]");
  L_add(&l, "after Join-Accept: ChMask=");
  L_chmask(&l);
  L_add(&l, " ch=");
  L_chlist(&l);
  L_emit(&l);
}

void AdrTrace_JoinAcceptRejected(const char *reason, int status)
{
  Line_t l;
  L_start(&l, "[JOIN]");
  L_add(&l, "JoinAccept REJECTED: %s (status %d)", (reason != NULL) ? reason : "?", status);
  L_emit(&l);
}

void AdrTrace_Downlink(const LoRaMacMessageData_t *msg, uint8_t mhdr, uint8_t fType, uint8_t rxSlot,
                       int8_t rxDr, int16_t rssi, int8_t snr, uint32_t fCntDown, bool multicast,
                       uint32_t adrAckCounterBefore)
{
  Line_t l;
  if (msg == NULL)
  {
    return;
  }
  LoRaMacHeader_t h;
  h.Value = mhdr;
  bool hasPort = (fType == FRAME_TYPE_A) || (fType == FRAME_TYPE_C) || (fType == FRAME_TYPE_D);
  L_start(&l, "[DL]");
  L_add(&l, "%s ", SlotStr(rxSlot));
  L_dr(&l, rxDr);
  L_add(&l, " rssi=%d snr=%d %s%s FCntDown=%u DevAddr=%08X", rssi, snr,
        (h.Bits.MType == FRAME_TYPE_DATA_CONFIRMED_DOWN) ? "CONF" : "UNCONF", multicast ? " MULTICAST" : "",
        (unsigned)fCntDown, (unsigned)msg->FHDR.DevAddr);
  if (hasPort)
  {
    L_add(&l, " FPort=%u", msg->FPort);
  }
  else
  {
    L_add(&l, " FPort=none");
  }
  L_add(&l, " FRMPayload=%uB FOptsLen=%u ACK=%u ADR=%u FPending=%u ADR_ACK_CNT=%u->%u",
        msg->FRMPayloadSize, msg->FHDR.FCtrl.Bits.FOptsLen, msg->FHDR.FCtrl.Bits.Ack, msg->FHDR.FCtrl.Bits.Adr,
        msg->FHDR.FCtrl.Bits.FPending, (unsigned)adrAckCounterBefore,
        ((rxSlot == RX_SLOT_WIN_1) || (rxSlot == RX_SLOT_WIN_2)) ? 0U : (unsigned)adrAckCounterBefore);
  if (msg->FHDR.FCtrl.Bits.FOptsLen > 0U)
  {
    L_add(&l, " FOpts=");
    L_hex(&l, msg->FHDR.FOpts, msg->FHDR.FCtrl.Bits.FOptsLen);
  }
  L_emit(&l);
}

void AdrTrace_RxAbort(int mcpsStatus, uint8_t rxSlot)
{
  Line_t l;
  L_start(&l, "[DL]");
  L_add(&l, "%s frame DROPPED by MAC: %s", SlotStr(rxSlot), StatusStr(mcpsStatus));
  L_emit(&l);
}

/* ------------------------------------------------------------------------- */
/* application layer (MIB based)                                             */
/* ------------------------------------------------------------------------- */
void AdrTrace_PrintConfig(void)
{
  Line_t l;
  MibRequestConfirm_t mib;
  LoRaMacNvmData_t *nvm = Nvm();
  uint8_t eui[8];

  L_start(&l, "[JOIN]");
  L_add(&l, "cfg region=%s subband=%d HYBRID=%d class=A ADR=%s port=%u period=%ums uplinks=%s",
        LAB_REGION_NAME, LAB_SUBBAND, LAB_HYBRID_ENABLED,
        (LORAWAN_ADR_STATE == LORAMAC_HANDLER_ADR_ON) ? "ON" : "OFF", LORAWAN_USER_APP_PORT,
        (unsigned)APP_TX_DUTYCYCLE,
        (LORAWAN_DEFAULT_CONFIRMED_MSG_STATE == LORAMAC_HANDLER_CONFIRMED_MSG) ? "CONFIRMED" : "UNCONFIRMED");
  L_emit(&l);

  L_start(&l, "[JOIN]");
  mib.Type = MIB_DEV_EUI;
  mib.Param.DevEui = eui;
  LoRaMacMibGetRequestConfirm(&mib);
  L_add(&l, "cfg DevEUI=");
  L_hex(&l, eui, 8U);
  mib.Type = MIB_JOIN_EUI;
  mib.Param.JoinEui = eui;
  LoRaMacMibGetRequestConfirm(&mib);
  L_add(&l, " JoinEUI=");
  L_hex(&l, eui, 8U);
  L_add(&l, " (MSB first, as entered in the network server)");
  L_emit(&l);

  if (nvm == NULL)
  {
    return;
  }
  L_start(&l, "[JOIN]");
  L_add(&l, "cfg ChMask=");
  L_chmask(&l);
  L_add(&l, " ch=");
  L_chlist(&l);
  L_add(&l, " defaultDR=DR%u minUL=DR%u (with UplinkDwellTime=%u)",
        (unsigned)PhyGet(PHY_DEF_TX_DR, 0, 0),
        (unsigned)PhyGet(PHY_MIN_TX_DR, 0, nvm->MacGroup2.MacParams.UplinkDwellTime),
        nvm->MacGroup2.MacParams.UplinkDwellTime);
  L_emit(&l);

  L_start(&l, "[JOIN]");
  L_add(&l, "cfg UplinkDwellTime=%u DownlinkDwellTime=%u MaxEIRP=", nvm->MacGroup2.MacParams.UplinkDwellTime,
        nvm->MacGroup2.MacParams.DownlinkDwellTime);
  L_tenths(&l, ToTenths(nvm->MacGroup2.MacParams.MaxEirp));
  L_add(&l, "dBm AntennaGain=");
  L_hundredths(&l, nvm->MacGroup2.MacParams.AntennaGain);
  L_add(&l, "dBi TXPower idx 0..%d: ", MaxTxPowerIdx());
  L_power(&l, 0);
  L_add(&l, " | ");
  L_power(&l, (int8_t)MaxTxPowerIdx());
  L_emit(&l);

  L_start(&l, "[JOIN]");
  L_add(&l, "cfg ADR_ACK_LIMIT=%u ADR_ACK_DELAY=%u NbTrans=%u RX1Delay=%ums JoinAcceptDelay1=%ums RX2=",
        nvm->MacGroup2.MacParams.AdrAckLimit, nvm->MacGroup2.MacParams.AdrAckDelay,
        nvm->MacGroup2.MacParams.ChannelsNbTrans, (unsigned)nvm->MacGroup2.MacParams.ReceiveDelay1,
        (unsigned)nvm->MacGroup2.MacParams.JoinAcceptDelay1);
  L_freq(&l, nvm->MacGroup2.MacParams.Rx2Channel.Frequency);
  L_add(&l, "/");
  L_dr(&l, nvm->MacGroup2.MacParams.Rx2Channel.Datarate);
  L_add(&l, " RX1DROffset=%u PA cfg=%s (LPmax=%d HPmax=%d dBm)", nvm->MacGroup2.MacParams.Rx1DrOffset,
        (RBI_GetTxConfig() == RBI_CONF_RFO_LP_HP) ? "LP+HP" : ((RBI_GetTxConfig() == RBI_CONF_RFO_HP) ? "HP" : "LP"),
        (int)RBI_GetRFOMaxPowerConfig(RBI_RFO_LP_MAXPOWER), (int)RBI_GetRFOMaxPowerConfig(RBI_RFO_HP_MAXPOWER));
  L_emit(&l);
}

void AdrTrace_AppTxConfirm(const struct LmHandlerTxParams_s *params)
{
  Line_t l;
  MibRequestConfirm_t mib;
  if ((params == NULL) || (params->IsMcpsConfirm == 0U))
  {
    return;
  }
  L_start(&l, "[UL]");
  L_add(&l, "done FCnt=%u status=%s ", (unsigned)params->UplinkCounter, StatusStr((int)params->Status));
  L_dr(&l, params->Datarate);
  L_add(&l, " pwrIdx=%d ch=%u%s | MIB now:", params->TxPower, params->Channel,
        (params->MsgType == LORAMAC_HANDLER_CONFIRMED_MSG) ? (params->AckReceived ? " ACKed" : " NOT-ACKed") : "");

  mib.Type = MIB_ADR;
  LoRaMacMibGetRequestConfirm(&mib);
  L_add(&l, " ADR=%u", mib.Param.AdrEnable ? 1U : 0U);
  mib.Type = MIB_CHANNELS_DATARATE;
  LoRaMacMibGetRequestConfirm(&mib);
  L_add(&l, " ");
  L_dr(&l, mib.Param.ChannelsDatarate);
  mib.Type = MIB_CHANNELS_TX_POWER;
  LoRaMacMibGetRequestConfirm(&mib);
  L_add(&l, " TXPower=%d", mib.Param.ChannelsTxPower);
  mib.Type = MIB_CHANNELS_NB_TRANS;
  LoRaMacMibGetRequestConfirm(&mib);
  L_add(&l, " NbTrans=%u ChMask=", mib.Param.ChannelsNbTrans);
  L_chmask(&l);
  L_emit(&l);
}

void AdrTrace_AppJoinResult(const struct LmHandlerJoinParams_s *params)
{
  Line_t l;
  MibRequestConfirm_t mib;
  if (params == NULL)
  {
    return;
  }
  L_start(&l, "[JOIN]");
  if (params->Status != LORAMAC_HANDLER_SUCCESS)
  {
    L_add(&l, "result=FAILED (no valid Join-Accept) mode=%s ", (params->Mode == ACTIVATION_TYPE_ABP) ? "ABP" : "OTAA");
    L_dr(&l, params->Datarate);
    L_emit(&l);
    return;
  }
  mib.Type = MIB_DEV_ADDR;
  LoRaMacMibGetRequestConfirm(&mib);
  L_add(&l, "result=JOINED mode=%s DevAddr=%08X ", (params->Mode == ACTIVATION_TYPE_ABP) ? "ABP" : "OTAA",
        (unsigned)mib.Param.DevAddr);
  L_dr(&l, params->Datarate);
  L_add(&l, " TXPower=%d", params->TxPower);
  mib.Type = MIB_RX2_CHANNEL;
  LoRaMacMibGetRequestConfirm(&mib);
  L_add(&l, " RX2=");
  L_freq(&l, mib.Param.Rx2Channel.Frequency);
  L_add(&l, "/");
  L_dr(&l, mib.Param.Rx2Channel.Datarate);
  mib.Type = MIB_RECEIVE_DELAY_1;
  LoRaMacMibGetRequestConfirm(&mib);
  L_add(&l, " RX1Delay=%ums", (unsigned)mib.Param.ReceiveDelay1);
  LoRaMacNvmData_t *nvm = Nvm();
  if (nvm != NULL)
  {
    L_add(&l, " RX1DROffset=%u", nvm->MacGroup2.MacParams.Rx1DrOffset);
  }
  L_add(&l, " ChMask=");
  L_chmask(&l);
  L_emit(&l);
}

#endif /* LAB_ADR_TRACE == 1 */
