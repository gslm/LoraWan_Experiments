/**
  ******************************************************************************
  * @file    lab_config.h
  * @brief   ONE place for the ADR-lab knobs of LoRaWAN_End_Node_ADR.
  *
  * Everything here can also be overridden from CMake without editing this file:
  *   cmake --preset Debug -DLAB_REGION=US915 -DLAB_SUBBAND=1 -DLAB_APP_TX_DUTYCYCLE_MS=30000
  * (the CMake cache variables are empty by default => the values below apply).
  *
  * Consumers:
  *   - LoRaWAN/Target/lorawan_conf.h : HYBRID_ENABLED + HYBRID_DEFAULT_MASK0..4
  *   - LoRaWAN/App/lora_app.h       : ACTIVE_REGION, APP_TX_DUTYCYCLE
  *   - LoRaWAN/App/adr_trace.[ch]   : LAB_ADR_TRACE
  ******************************************************************************
  */
#ifndef LAB_CONFIG_H
#define LAB_CONFIG_H

/* ===========================================================================
 * 1) REGION  (exactly one; Brazil = AU915-928 per LoRa Alliance RP002 "Brazil")
 *    The three regions below are always compiled into the MAC (lorawan_conf.h),
 *    so switching is only a matter of changing this selection.
 * ========================================================================= */
#if !defined(LAB_REGION_AU915) && !defined(LAB_REGION_US915) && !defined(LAB_REGION_EU868)
#define LAB_REGION_AU915
/* #define LAB_REGION_US915 */
/* #define LAB_REGION_EU868 */
#endif

/* ===========================================================================
 * 2) SUB-BAND (AU915 / US915 only, ignored for EU868)
 *    1..8 : enable only that block of 8 x 125 kHz channels + its 500 kHz channel
 *           (stack "HYBRID" mode, RegionAU915/US915InitDefaults()).
 *           Sub-band 2 = channels 8..15 + 65:
 *             AU915: 916.8 .. 918.2 MHz (125 kHz) + 917.5 MHz (500 kHz, DR6)
 *             (= TTN "AU_915_928_FSB_2", ChirpStack "au915_1" config file)
 *    0    : all 64 + 8 channels enabled (stack default, HYBRID off).
 *    NOTE: this is only the DEFAULT mask used for the join procedure. After the
 *    join, the network can (and ChirpStack/TTN do) overwrite it with the CFList
 *    of the Join-Accept and/or LinkADRReq ChMask commands.
 * ========================================================================= */
#ifndef LAB_SUBBAND
#define LAB_SUBBAND                 2
#endif

/* ===========================================================================
 * 3) Uplink period (ms). 15 s so that NS ADR (typically 20 frames of history)
 *    converges in ~5 minutes. Unconfirmed, FPort 2 (lora_app.h).
 * ========================================================================= */
#ifndef LAB_APP_TX_DUTYCYCLE_MS
#define LAB_APP_TX_DUTYCYCLE_MS     15000
#endif

/* ===========================================================================
 * 4) ADR / MAC tracing ("[ADR]", "[MAC]", "[UL]", "[DL]", "[JOIN]" lines).
 *    0 removes every hook at compile time (stack hooks become empty macros).
 * ========================================================================= */
#ifndef LAB_ADR_TRACE
#define LAB_ADR_TRACE               1
#endif

/* ===========================================================================
 * 5) Payload: 1 = fixed 5-byte lab payload (LED, seq[2], temp, battery);
 *             0 = upstream ST payload (10 bytes for AU915/US915).
 * ========================================================================= */
#ifndef LAB_SMALL_PAYLOAD
#define LAB_SMALL_PAYLOAD           1
#endif

/* ------------------------------------------------------------------------- */
/* Derived values - do not edit below                                        */
/* ------------------------------------------------------------------------- */
#if defined(LAB_REGION_AU915)
#define LAB_ACTIVE_REGION           LORAMAC_REGION_AU915
#define LAB_REGION_NAME             "AU915"
#define LAB_REGION_HAS_SUBBANDS     1
#elif defined(LAB_REGION_US915)
#define LAB_ACTIVE_REGION           LORAMAC_REGION_US915
#define LAB_REGION_NAME             "US915"
#define LAB_REGION_HAS_SUBBANDS     1
#elif defined(LAB_REGION_EU868)
#define LAB_ACTIVE_REGION           LORAMAC_REGION_EU868
#define LAB_REGION_NAME             "EU868"
#define LAB_REGION_HAS_SUBBANDS     0
#else
#error "lab_config.h: select one LAB_REGION_xxx"
#endif

#if (LAB_REGION_HAS_SUBBANDS == 1) && (LAB_SUBBAND >= 1) && (LAB_SUBBAND <= 8)
/* consumed by lorawan_conf.h -> HYBRID_ENABLED */
#define LAB_HYBRID_ENABLED          1
#define LAB_SB_IDX                  ((LAB_SUBBAND) - 1)
#define LAB_SB_125K_BITS            (((LAB_SB_IDX) & 1) ? 0xFF00U : 0x00FFU)
/* HYBRID_DEFAULT_MASKx are #ifndef-guarded in RegionAU915.c / RegionUS915.c */
#define HYBRID_DEFAULT_MASK0        ((((LAB_SB_IDX) >> 1) == 0) ? LAB_SB_125K_BITS : 0x0000U) /* ch  0..15 */
#define HYBRID_DEFAULT_MASK1        ((((LAB_SB_IDX) >> 1) == 1) ? LAB_SB_125K_BITS : 0x0000U) /* ch 16..31 */
#define HYBRID_DEFAULT_MASK2        ((((LAB_SB_IDX) >> 1) == 2) ? LAB_SB_125K_BITS : 0x0000U) /* ch 32..47 */
#define HYBRID_DEFAULT_MASK3        ((((LAB_SB_IDX) >> 1) == 3) ? LAB_SB_125K_BITS : 0x0000U) /* ch 48..63 */
#define HYBRID_DEFAULT_MASK4        (1U << (LAB_SB_IDX))                                       /* ch 64..71 (500 kHz) */
#elif (LAB_REGION_HAS_SUBBANDS == 0) || (LAB_SUBBAND == 0)
#define LAB_HYBRID_ENABLED          0
#else
#error "lab_config.h: LAB_SUBBAND must be 0..8"
#endif

#endif /* LAB_CONFIG_H */
