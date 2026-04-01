/*
 * Copyright (c) 2011-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This file lim_assoc_utils.h contains the utility definitions
 * LIM uses while processing Re/Association messages.
 * Author:        Chandra Modumudi
 * Date:          02/13/02
 * History:-
 * Date           Modified by    Modification Information
 * --------------------------------------------------------------------
 * 05/26/10       js             WPA handling in (Re)Assoc frames
 *
 */
#ifndef __LIM_ASSOC_UTILS_H
#define __LIM_ASSOC_UTILS_H

#include "sir_api.h"
#include "sir_debug.h"

#include "lim_types.h"
#include "wlan_cm_api.h"

#define SIZE_OF_NOA_DESCRIPTOR 13
#define MAX_NOA_PERIOD_IN_MICROSECS 3000000

uint8_t lim_compare_capabilities(struct mac_context *,
				 tSirAssocReq *,
				 tSirMacCapabilityInfo *, struct pe_session *);
uint8_t lim_check_rx_basic_rates(struct mac_context *, tSirMacRateSet, struct pe_session *);
uint8_t lim_check_mcs_set(struct mac_context *mac, uint8_t *supportedMCSSet);

/**
 * lim_validate_he_mcs_for_bw() - Validate HE MCS maps for configured bandwidth
 * @rx_he_mcs_map: HE Rx MCS map (for the bandwidth being validated)
 * @tx_he_mcs_map: HE Tx MCS map (for the bandwidth being validated)
 *
 * This helper checks that the HE MCS maps for a given bandwidth are usable
 * for both directions (Rx/Tx). It is typically used when negotiating HE
 * bandwidth (e.g. 20/40/80/160 MHz) to ensure:
 *
 * - At least one spatial stream is enabled with a valid MCS in both
 *   rx_he_mcs_map and tx_he_mcs_map
 * - The Rx/Tx MCS maps are consistent for the selected bandwidth, so that
 *   the negotiated bandwidth does not result in an unusable MCS configuration
 *
 * The function does not modify any state; it only inspects the two maps.
 *
 * Return:
 * true  - if HE MCS configuration for this bandwidth is valid
 * false - if no usable NSS/MCS combination exists or maps are inconsistent
 */
bool lim_validate_he_mcs_for_bw(uint16_t rx_he_mcs_map, uint16_t tx_he_mcs_map);

/**
 * lim_cleanup_rx_path() - Called to cleanup STA state at SP & RFP.
 * @mac: Pointer to Global MAC structure
 * @sta: Pointer to the per STA data structure initialized by LIM
 *	 and maintained at DPH
 * @pe_session: pointer to pe session
 * @delete_peer: is peer delete allowed
 *
 * To circumvent RFP's handling of dummy packet when it does not
 * have an incomplete packet for the STA to be deleted, a packet
 * with 'more framgents' bit set will be queued to RFP's WQ before
 * queuing 'dummy packet'.
 * A 'dummy' BD is pushed into RFP's WQ with type=00, subtype=1010
 * (Disassociation frame) and routing flags in BD set to eCPU's
 * Low Priority WQ.
 * RFP cleans up its local context for the STA id mentioned in the
 * BD and then pushes BD to eCPU's low priority WQ.
 *
 * Return: QDF_STATUS_SUCCESS or QDF_STATUS_E_FAILURE.
 */
QDF_STATUS lim_cleanup_rx_path(struct mac_context *, tpDphHashNode,
			       struct pe_session *, bool delete_peer);

void lim_reject_association(struct mac_context *, tSirMacAddr, uint8_t,
			    uint8_t, tAniAuthType, uint16_t, uint8_t,
			    enum wlan_status_code, struct pe_session *);

QDF_STATUS lim_populate_peer_rate_set(struct mac_context *mac,
				      struct supported_rates *pRates,
				      uint8_t *pSupportedMCSSet,
				      struct pe_session *pe_session,
				      tDot11fIEVHTCaps *pVHTCaps,
				      tDot11fIEhe_cap *he_caps,
				      tDot11fIEeht_cap *eht_caps,
				      struct sDphHashNode *sta_ds,
				      struct bss_description *bss_desc);

/**
 * lim_populate_own_rate_set() - comprises the basic and extended rates read
 *                                from CFG
 * @mac_ctx: pointer to global mac structure
 * @rates: pointer to supported rates
 * @session_entry: pe session entry
 *
 * This function is called by limProcessAssocRsp() or
 * lim_add_staInIBSS()
 * - It creates a combined rate set of 12 rates max which
 *   comprises the basic and extended rates read from CFG
 * - It sorts the combined rate Set and copy it in the
 *   rate array of the pSTA descriptor
 * - It sets the erpEnabled bit of the STA descriptor
 * ERP bit is set iff the dph PHY mode is 11G and there is at least
 * an A rate in the supported or extended rate sets
 *
 * Return: QDF_STATUS_SUCCESS or QDF_STATUS_E_FAILURE.
 */
QDF_STATUS lim_populate_own_rate_set(struct mac_context *mac_ctx,
				     struct supported_rates *rates,
				     struct pe_session *session_entry);

QDF_STATUS lim_populate_matching_rate_set(struct mac_context *mac_ctx,
					  struct pe_session *session_entry,
					  tpDphHashNode sta_ds,
					  tpSirAssocReq assoc_req);

QDF_STATUS lim_add_sta(struct mac_context *, tpDphHashNode, uint8_t, struct pe_session *);
QDF_STATUS lim_del_bss(struct mac_context *, tpDphHashNode, uint16_t, struct pe_session *);
QDF_STATUS lim_del_sta(struct mac_context *, tpDphHashNode, bool, struct pe_session *);
QDF_STATUS lim_add_sta_self(struct mac_context *, uint8_t, struct pe_session *);

/**
 *lim_del_peer_info() - remove all peer information from host driver and fw
 * @mac:    Pointer to Global MAC structure
 * @pe_session: Pointer to PE Session entry
 *
 * @Return: QDF_STATUS
 */
QDF_STATUS lim_del_peer_info(struct mac_context *mac,
			     struct pe_session *pe_session);

/**
 * lim_del_sta_all() - Cleanup all peers associated with VDEV
 * @mac:    Pointer to Global MAC structure
 * @pe_session: Pointer to PE Session entry
 *
 * @Return: QDF Status of operation.
 */
QDF_STATUS lim_del_sta_all(struct mac_context *mac,
			   struct pe_session *pe_session);
/**
 * lim_get_sta_ds() -get sta ds
 * @mac_ctx: mac ctx
 * @sa: source addr
 * @mld_mac: mld mac
 * @assoc_id: assoc id
 * @session: pe session ctx
 *
 * @Return: sta ds in case of success else NULL
 */
tpDphHashNode lim_get_sta_ds(struct mac_context *mac_ctx,
			     tSirMacAddr sa, tSirMacAddr mld_mac,
			     uint16_t *assoc_id,
			     struct pe_session *session);

#ifdef WLAN_FEATURE_HOST_ROAM
void lim_restore_pre_reassoc_state(struct mac_context *,
				   tSirResultCodes, uint16_t, struct pe_session *);
void lim_post_reassoc_failure(struct mac_context *,
			      tSirResultCodes, uint16_t, struct pe_session *);
bool lim_is_reassoc_in_progress(struct mac_context *, struct pe_session *);

void lim_handle_add_bss_in_re_assoc_context(struct mac_context *mac,
		tpDphHashNode sta, struct pe_session *pe_session);
void lim_handle_del_bss_in_re_assoc_context(struct mac_context *mac,
		   tpDphHashNode sta, struct pe_session *pe_session);
void lim_send_retry_reassoc_req_frame(struct mac_context *mac,
	      tLimMlmReassocReq *pMlmReassocReq, struct pe_session *pe_session);
QDF_STATUS lim_add_ft_sta_self(struct mac_context *mac, uint16_t assocId,
				  struct pe_session *pe_session);
#else
static inline void lim_restore_pre_reassoc_state(struct mac_context *mac_ctx,
			tSirResultCodes res_code, uint16_t prot_status,
			struct pe_session *pe_session)
{}
static inline void lim_post_reassoc_failure(struct mac_context *mac_ctx,
			      tSirResultCodes res_code, uint16_t prot_status,
			      struct pe_session *pe_session)
{}
static inline void lim_handle_add_bss_in_re_assoc_context(struct mac_context *mac,
		tpDphHashNode sta, struct pe_session *pe_session)
{}
static inline void lim_handle_del_bss_in_re_assoc_context(struct mac_context *mac,
		   tpDphHashNode sta, struct pe_session *pe_session)
{}
static inline void lim_send_retry_reassoc_req_frame(struct mac_context *mac,
	      tLimMlmReassocReq *pMlmReassocReq, struct pe_session *pe_session)
{}
static inline bool lim_is_reassoc_in_progress(struct mac_context *mac_ctx,
		struct pe_session *pe_session)
{
	return false;
}
static inline QDF_STATUS lim_add_ft_sta_self(struct mac_context *mac,
		uint16_t assocId, struct pe_session *pe_session)
{
	return QDF_STATUS_SUCCESS;
}
#endif

#ifdef WLAN_FEATURE_11BE
static inline bool
lim_is_add_sta_params_eht_capable(tpAddStaParams add_sta_params)
{
	return add_sta_params->eht_capable;
}

/**
 * lim_calculate_ap_max_eht_ch_width() - Calculate AP max supported EHT width
 * @pe_session: PE session entry
 * @eht_cap: Pointer to EHT Capability IE (mandatory for EHT validation)
 *
 * This helper encapsulates the EHT max bandwidth decision logic so it can be
 * used from multiple paths (e.g., assoc response and beacon-based validation).
 *
 * Return: Maximum AP supported channel width for EHT.
 */
enum phy_ch_width
lim_calculate_ap_max_eht_ch_width(struct pe_session *pe_session,
				  tDot11fIEeht_cap *eht_cap);
#else
static inline bool
lim_is_add_sta_params_eht_capable(tpAddStaParams add_sta_params)
{
	return false;
}

static inline enum phy_ch_width
lim_calculate_ap_max_eht_ch_width(struct pe_session *pe_session,
				  tDot11fIEeht_cap *eht_cap)
{
	return CH_WIDTH_INVALID;
}
#endif

void
lim_send_del_sta_cnf(struct mac_context *mac, struct qdf_mac_addr sta_dsaddr,
		     struct qdf_mac_addr sta_mld_addr,
		     uint16_t staDsAssocId,
		     struct lim_sta_context mlmStaContext,
		     tSirResultCodes status_code,
		     struct pe_session *pe_session);

void lim_handle_cnf_wait_timeout(struct mac_context *mac, uint16_t staId);
void lim_delete_dph_hash_entry(struct mac_context *, tSirMacAddr, uint16_t, struct pe_session *);
void lim_check_and_announce_join_success(struct mac_context *,
					 tSirProbeRespBeacon *,
					 tpSirMacMgmtHdr, struct pe_session *);

/**
 * lim_update_session_nss_for_state() - Update session NSS based on state
 * @session: PE session entry
 * @nss_ies: NSS information from IEs
 *
 * This function updates the session's NSS (Number of Spatial Streams)
 * parameters based on the current LIM MLM state. It handles different
 * states like eLIM_MLM_WT_JOIN_BEACON_STATE and eLIM_MLM_WT_ASSOC_RSP_STATE
 * differently.
 * The function calculates and sets the capability and operational NSS values
 * for both Tx and Rx, considering the NSS values from IEs and
 * self capabilities.
 * It also updates the HE and EHT MCS sets based on the new NSS values.
 *
 * Return: None
 */
void lim_update_session_nss_for_state(struct pe_session *session,
				      struct sir_dot11f_nss_info *nss_ies);

void lim_update_re_assoc_globals(struct mac_context *mac,
				 tpSirAssocRsp pAssocRsp,
				 struct pe_session *pe_session);

/**
 * lim_update_assoc_sta_datas() - Updates station Descriptor
 * @mac_ctx: Pointer to Global MAC structure
 * @sta_ds: Station Descriptor in DPH
 * @assoc_rsp: Pointer to Association Response Structure
 * @session_entry : PE session Entry
 * @bss_desc: Pointer to BSS descriptor
 *
 * This function is called to Update the Station Descriptor (dph) Details from
 * Association / ReAssociation Response Frame
 *
 * Return: None
 */
void lim_update_assoc_sta_datas(struct mac_context *mac,
				tpDphHashNode sta, tpSirAssocRsp pAssocRsp,
				struct pe_session *pe_session,
				struct bss_description *bss_desc);

/**
 * lim_sta_add_bss_update_ht_parameter() - function to update ht related
 *                                         parameters when add bss request
 * @bss_chan_freq: operating frequency of bss
 * @ht_cap: ht capability extract from beacon/assoc response
 * @ht_inf: ht information extract from beacon/assoc response
 * @chan_width_support: local wide bandwidth support capability
 * @add_bss: add bss request struct to be updated
 *
 * Return: none
 */
void lim_sta_add_bss_update_ht_parameter(uint32_t bss_chan_freq,
					 tDot11fIEHTCaps* ht_cap,
					 tDot11fIEHTInfo* ht_inf,
					 bool chan_width_support,
					 struct bss_params *add_bss);

/**
 * lim_sta_send_add_bss() - add bss and send peer assoc after receive assoc
 * rsp in sta mode
 *.@mac: pointer to Global MAC structure
 * @pAssocRsp: contains the structured assoc/reassoc Response got from AP
 * @bss_desc: bss description passed to PE from the SME
 * @updateEntry: bool flag of whether update bss and sta
 * @pe_session: pointer to pe session
 *
 * Return: none
 */
QDF_STATUS lim_sta_send_add_bss(struct mac_context *mac,
				tpSirAssocRsp pAssocRsp,
				struct bss_description *bss_desc,
				uint8_t updateEntry,
				struct pe_session *pe_session);

/**
 * lim_sta_send_add_bss_pre_assoc() - add bss after channel switch and before
 * associate req in sta mode
 *.@mac: pointer to Global MAC structure
 * @pe_session: pointer to pe session
 *
 * Return: none
 */
QDF_STATUS lim_sta_send_add_bss_pre_assoc(struct mac_context *mac,
					  struct pe_session *pe_session);

void lim_prepare_and_send_del_all_sta_cnf(struct mac_context *mac,
					  tSirResultCodes status_code,
					  struct pe_session *pe_session);

void lim_prepare_and_send_del_sta_cnf(struct mac_context *mac,
				      tpDphHashNode sta,
				      tSirResultCodes status_code,
				      struct pe_session *pe_session);

void lim_init_pre_auth_timer_table(struct mac_context *mac,
				   tpLimPreAuthTable pPreAuthTimerTable);
tpLimPreAuthNode lim_acquire_free_pre_auth_node(struct mac_context *mac,
						tpLimPreAuthTable
						pPreAuthTimerTable);
tpLimPreAuthNode lim_get_pre_auth_node_from_index(struct mac_context *mac,
						  tpLimPreAuthTable pAuthTable,
						  uint32_t authNodeIdx);

/* Util API to check if the channels supported by STA is within range */
QDF_STATUS lim_is_dot11h_supported_channels_valid(struct mac_context *mac,
						     tSirAssocReq *assoc);

/* Util API to check if the txpower supported by STA is within range */
QDF_STATUS lim_is_dot11h_power_capabilities_in_range(struct mac_context *mac,
							tSirAssocReq *assoc,
							struct pe_session *);
/**
 * lim_fill_rx_highest_supported_rate() - Fill highest rx rate
 * @mac: Global MAC context
 * @rxHighestRate: location to store the highest rate
 * @pSupportedMCSSet: location of the 'supported MCS set' field in HT
 *                    capability element
 *
 * Fills in the Rx Highest Supported Data Rate field from
 * the 'supported MCS set' field in HT capability element.
 *
 * Return: void
 */
void lim_fill_rx_highest_supported_rate(struct mac_context *mac,
					uint16_t *rxHighestRate,
					uint8_t *pSupportedMCSSet);
void lim_send_sme_unprotected_mgmt_frame_ind(struct mac_context *mac, uint8_t frameType,
					     uint8_t *frame, uint32_t frameLen,
					     uint16_t sessionId,
					     struct pe_session *pe_session);
/**
 * lim_send_sme_tsm_ie_ind() - Send TSM IE information to SME
 * @mac: Global MAC context
 * @pe_session: PE session context
 * @tid: traffic id
 * @state: tsm state (enabled/disabled)
 * @measurement_interval: measurement interval
 *
 * Return: void
 */
#ifdef FEATURE_WLAN_ESE
void lim_send_sme_tsm_ie_ind(struct mac_context *mac,
			     struct pe_session *pe_session,
			     uint8_t tid, uint8_t state,
			     uint16_t measurement_interval);
#else
static inline
void lim_send_sme_tsm_ie_ind(struct mac_context *mac,
			     struct pe_session *pe_session,
			     uint8_t tid, uint8_t state,
			     uint16_t measurement_interval)
{}
#endif /* FEATURE_WLAN_ESE */

/**
 * lim_populate_vht_mcs_set - function to populate vht mcs rate set
 * @mac_ctx: pointer to global mac structure
 * @rates: pointer to supported rate set
 * @peer_vht_caps: pointer to peer vht capabilities
 * @session_entry: pe session entry
 * @tx_nss: number of Tx spatial streams
 * @rx_nss: number of Rx spatial streams
 * @sta_ds: pointer to peer sta data structure
 *
 * Populates vht mcs rate set based on peer and self capabilities
 *
 * Return: QDF_STATUS_SUCCESS on success else QDF_STATUS_E_FAILURE
 */
QDF_STATUS lim_populate_vht_mcs_set(struct mac_context *mac_ctx,
				    struct supported_rates *rates,
				    tDot11fIEVHTCaps *peer_vht_caps,
				    struct pe_session *session_entry,
				    uint8_t tx_nss, uint8_t rx_nss,
				    struct sDphHashNode *sta_ds);

/**
 * lim_extract_ies_from_deauth_disassoc() - Extract IEs from deauth/disassoc
 *
 * @session: PE session entry
 * @deauth_disassoc_frame: A pointer to the deauth/disconnect frame buffer
 *			   received from WMA.
 * @deauth_disassoc_frame_leni: Length of the deauth/disconnect frame.
 *
 * This function receives deauth/disassoc frame from header. It extracts
 * the IEs(tagged params) from the frame and caches in vdev object.
 *
 * Return: None
 */
void
lim_extract_ies_from_deauth_disassoc(struct pe_session *session,
				     uint8_t *deauth_disassoc_frame,
				     uint16_t deauth_disassoc_frame_len);

/**
 * lim_get_vht_ap_max_ch_width() - Get AP max channel width from VHT caps
 * @vht_caps: VHT capabilities IE from AP
 *
 * This function validates the VHT MCS maps and calculates the AP's maximum
 * supported channel width based on the VHT Capabilities IE
 * (supportedChannelWidthSet field).
 *
 * Return: AP max channel width on success, CH_WIDTH_INVALID if MCS maps
 *         are invalid (all spatial streams disabled).
 */
enum phy_ch_width lim_get_vht_ap_max_ch_width(tDot11fIEVHTCaps *vht_caps);

/**
 * lim_update_vhtcaps_assoc_resp : Update VHT caps in assoc response.
 * @mac_ctx Pointer to Global MAC structure
 * @pAddBssParams: parameters required for add bss params.
 * @vht_caps: VHT capabilities.
 *
 * Return : void
 */
void lim_update_vhtcaps_assoc_resp(struct mac_context *mac_ctx,
				   struct bss_params *pAddBssParams,
				   tDot11fIEVHTCaps *vht_caps);

/**
 * lim_update_vhtcaps_assoc_resp_bw() - Update VHT capabilities from Assoc Rsp
 * @mac_ctx: Pointer to Global MAC structure
 * @pAddBssParams: Parameters required for add bss params
 * @vht_caps: VHT capabilities from association response
 *
 * Note: VHT rxMCSMap/txMCSMap (Supported MCS/NSS Set) is bandwidth-independent
 * (same MCS index 0-9 applies for 20/40/80/160 MHz). Channel bandwidth is
 * applied orthogonally during rate computation and via operational IEs
 * (e.g., VHT Operation IE).
 *
 * This function processes the VHT Capability IE from the AP's association
 * response frame. It validates the claimed bandwidth against the VHT
 * Supported MCS Set to ensure the AP has valid MCS support. The result
 * is stored in pAddBssParams->staContext.ap_max_ch_width.
 *
 * Return: true/false
 */
bool lim_update_vhtcaps_assoc_resp_bw(struct mac_context *mac_ctx,
				      struct bss_params *pAddBssParams,
				      tDot11fIEVHTCaps *vht_caps,
				      struct wlan_objmgr_peer *peer);

/**
 * lim_free_assoc_req_frm_buf() - free assoc request frame buffer
 * @assoc_req: pointer to tpSirAssocReq
 *
 * Return : void
 */
void lim_free_assoc_req_frm_buf(tpSirAssocReq assoc_req);

/**
 * lim_alloc_assoc_req_frm_buf() - allocate assoc request frame buffer
 * @assoc_req: pointer to tpSirAssocReq
 * @buf: pointer to assoc request frame
 * @mac_header_len: ieee80211 header length
 * @frame_len: payload length of assoc request frame
 */
bool lim_alloc_assoc_req_frm_buf(tpSirAssocReq assoc_req,
				 qdf_nbuf_t buf, uint32_t mac_header_len,
				 uint32_t frame_len);

#if (defined(CONNECTIVITY_DIAG_EVENT) && \
	defined(WLAN_FEATURE_ROAM_OFFLOAD))
/**
 * lim_clear_log_instance_id() - Clear log instance id
 *
 * @session: PE session entry
 *
 * Return: None
 */
void lim_clear_log_instance_id(struct pe_session *session);
#else
static inline void lim_clear_log_instance_id(struct pe_session *session)
{}
#endif

/**
 * lim_update_add_sta_cck_5g_support() - update add sta CCK RX/TX
 * @mac_ctx: mac context
 * @add_sta: pointer to tAddStaParams
 * @assoc_rsp: Assoc response
 * @session_entry: pe session
 *
 * Return: NA
 */
void
lim_update_add_sta_cck_5g_support(struct mac_context *mac_ctx,
				  tAddStaParams *add_sta, tpSirAssocRsp assoc_rsp,
				  struct pe_session *session_entry);

#ifdef WLAN_FEATURE_11BN_SMD
/**
 * lim_intersect_sta_ap_capabilities_smd() - Intersect STA and AP
 * capabilities for SMD
 * @mac_ctx: MAC context
 * @pe_session: PE session
 * @scan_entry: Scan cache entry for target AP
 * @link_caps: Output structure for intersected capabilities
 * @chan_freq: Channel frequency for the link
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
lim_intersect_sta_ap_capabilities_smd(
				struct mac_context *mac_ctx,
				struct pe_session *pe_session,
				struct scan_cache_entry *scan_entry,
				struct lim_intersected_link_caps *link_caps,
				qdf_freq_t chan_freq);
#endif

#endif /* __LIM_ASSOC_UTILS_H */
