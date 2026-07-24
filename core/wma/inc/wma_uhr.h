/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef __WMA_UHR_H
#define __WMA_UHR_H
#include "wma.h"
#include "wlan_cmn_ieee80211.h"

#if defined(WLAN_FEATURE_11BN)
#define MAX_UHR_DCM_INDEX 2

/**
 * struct index_uhr_data_rate_type - UHR data rate type
 * @beacon_rate_index: Beacon rate index
 * @supported_uhr20_rate: uhr20 rate
 * @supported_uhr40_rate: uhr40 rate
 * @supported_uhr80_rate: uhr80 rate
 * @supported_uhr160_rate: uhr160 rate
 * @supported_uhr320_rate: uhr320 rate
 */
struct index_uhr_data_rate_type {
	uint8_t beacon_rate_index;
	uint32_t supported_uhr20_rate[MAX_UHR_DCM_INDEX][3];
	uint32_t supported_uhr40_rate[MAX_UHR_DCM_INDEX][3];
	uint32_t supported_uhr80_rate[MAX_UHR_DCM_INDEX][3];
	uint32_t supported_uhr160_rate[MAX_UHR_DCM_INDEX][3];
	uint32_t supported_uhr320_rate[MAX_UHR_DCM_INDEX][3];
};

/*
 * wma_uhr_update_tgt_services() - update tgt cfg to indicate 11bn support
 * @wmi_handle: pointer to WMI handle
 * @cfg: pointer to WMA target services
 *
 * Based on WMI SERVICES information, enable 11bn support and set DOT11BN
 * bit in feature caps bitmap.
 *
 * Return: None
 */
void wma_uhr_update_tgt_services(struct wmi_unified *wmi_handle,
				 struct wma_tgt_services *cfg);
/**
 * wma_update_target_ext_uhr_cap() - Update UHR caps with given extended cap
 * @tgt_hdl: target psoc information
 * @tgt_cfg: Target config
 *
 * This function loop through each hardware mode and for each hardware mode
 * again it loop through each MAC/PHY and pull the caps 2G and 5G specific
 * UHR caps and derives the final cap.
 *
 * Return: None
 */
void wma_update_target_ext_uhr_cap(struct target_psoc_info *tgt_hdl,
				   struct wma_tgt_cfg *tgt_cfg);

/**
 * wma_populate_peer_uhr_cap() - populate peer UHR capabilities in
 *                               peer assoc cmd
 * @peer: pointer to peer assoc params
 * @params: pointer to ADD STA params
 *
 * Return: None
 */
void wma_populate_peer_uhr_cap(struct peer_assoc_params *peer,
			       tpAddStaParams params);

/**
 * wma_is_peer_uhr_capable() - whether peer is uhr capable or not
 * @params: add sta params
 *
 * Return: true if uhr capable is present
 */
static inline bool wma_is_peer_uhr_capable(tpAddStaParams params)
{
	return params->uhr_capable;
}

/**
 * wma_get_bss_uhr_capable() - whether bss is uhr capable or not
 * @add_bss: add_bss params
 *
 * Return: true if uhr capable is present
 */
bool wma_get_bss_uhr_capable(struct bss_params *add_bss);

/**
 * wma_set_bss_rate_flags_uhr() - set rate flags based on BSS UHR capability
 * @rate_flags: pointer to rate flags to be updated
 * @add_bss: add_bss params
 *
 * Return: QDF_STATUS_SUCCESS if the bss is UHR capable and rate_flags was
 * updated, QDF_STATUS_E_NOSUPPORT otherwise
 */
QDF_STATUS wma_set_bss_rate_flags_uhr(enum tx_rate_info *rate_flags,
				      struct bss_params *add_bss);

/**
 * wma_match_uhr_rate() - get UHR rate matching with nss
 * @raw_rate: raw rate from fw
 * @rate_flags: rate flags
 * @nss: nss
 * @dcm: dcm
 * @guard_interval: guard interval
 * @mcs_rate_flag: mcs rate flags (output — set to matched UHR BW flag)
 * @p_index: index for matched rate
 *
 * Reuses EHT rate tables since UHR MCS/NSS/BW is identical to EHT.
 *
 * Return: matched rate if found, else 0
 */
uint32_t wma_match_uhr_rate(uint16_t raw_rate,
			    enum tx_rate_info rate_flags,
			    uint8_t *nss, uint8_t *dcm,
			    enum txrate_gi *guard_interval,
			    enum tx_rate_info *mcs_rate_flag,
			    uint8_t *p_index);

static
inline bool wma_is_uhr_phymode_supported(enum wlan_phymode bss_phymode)
{
	return IS_WLAN_PHYMODE_UHR(bss_phymode);
}
#else
static inline void wma_uhr_update_tgt_services(struct wmi_unified *wmi_handle,
					       struct wma_tgt_services *cfg)
{
	cfg->en_11bn = false;
}

static inline
void wma_update_target_ext_uhr_cap(struct target_psoc_info *tgt_hdl,
				   struct wma_tgt_cfg *tgt_cfg)
{
}

static inline
void wma_populate_peer_uhr_cap(struct peer_assoc_params *peer,
			       tpAddStaParams params)
{
}

static inline bool wma_is_peer_uhr_capable(tpAddStaParams params)
{
	return false;
}

static inline
bool wma_get_bss_uhr_capable(struct bss_params *add_bss)
{
	return false;
}

static inline
QDF_STATUS wma_set_bss_rate_flags_uhr(enum tx_rate_info *rate_flags,
				      struct bss_params *add_bss)
{
	return QDF_STATUS_E_NOSUPPORT;
}

static inline
uint32_t wma_match_uhr_rate(uint16_t raw_rate,
			    enum tx_rate_info rate_flags,
			    uint8_t *nss, uint8_t *dcm,
			    enum txrate_gi *guard_interval,
			    enum tx_rate_info *mcs_rate_flag,
			    uint8_t *p_index)
{
	return 0;
}

static inline bool wma_is_uhr_phymode_supported(enum wlan_phymode bss_phymode)
{
	return false;
}
#endif
#endif /* __WMA_UHR_H */

