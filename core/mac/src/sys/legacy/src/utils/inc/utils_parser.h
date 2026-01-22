/*
 * Copyright (c) 2011-2019 The Linux Foundation. All rights reserved.
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
 *
 * This file utils_parser.h contains the utility function protos
 * used internally by the parser
 * Author:        Chandra Modumudi
 * Date:          02/11/02
 * History:-
 * Date           Modified by    Modification Information
 * --------------------------------------------------------------------
 *
 */
#ifndef __UTILS_PARSE_H__
#define __UTILS_PARSE_H__

#include "qdf_types.h"
#include "sir_api.h"
#include "dot11f.h"
#include "utils_api.h"

#define EID_TRANSMIT_POWER_ENVELOPE 195

void convert_ssid(struct mac_context *, tSirMacSSid *, tDot11fIESSID *);
void convert_supp_rates(struct mac_context *, tSirMacRateSet *, tDot11fIESuppRates *);
void convert_fh_params(struct mac_context *, tSirMacFHParamSet *,
		       tDot11fIEFHParamSet *);
void convert_ext_supp_rates(struct mac_context *, tSirMacRateSet *,
			    tDot11fIEExtSuppRates *);
void convert_qos_caps(struct mac_context *, tSirMacQosCapabilityIE *,
		      tDot11fIEQOSCapsAp *);
void convert_qos_caps_station(struct mac_context *, tSirMacQosCapabilityStaIE *,
			      tDot11fIEQOSCapsStation *);
QDF_STATUS convert_wpa(struct mac_context *, tSirMacWpaInfo *, tDot11fIEWPA *);
QDF_STATUS convert_wpa_opaque(struct mac_context *, tSirMacWpaInfo *,
			      tDot11fIEWPAOpaque *);
QDF_STATUS convert_wapi_opaque(struct mac_context *, tSirMacWapiInfo *,
			       tDot11fIEWAPIOpaque *);
QDF_STATUS convert_rsn(struct mac_context *, tSirMacRsnInfo *, tDot11fIERSN *);
QDF_STATUS convert_rsn_opaque(struct mac_context *, tSirMacRsnInfo *,
			      tDot11fIERSNOpaque *);
void convert_power_caps(struct mac_context *, tSirMacPowerCapabilityIE *,
			tDot11fIEPowerCaps *);
void convert_supp_channels(struct mac_context *, tSirMacSupportedChannelIE *,
			   tDot11fIESuppChannels *);
void convert_cf_params(struct mac_context *, tSirMacCfParamSet *, tDot11fIECFParams *);
void convert_tim(struct mac_context *, tSirMacTim *, tDot11fIETIM *);
void convert_country(struct mac_context *, tSirCountryInformation *,
		     tDot11fIECountry *);
void convert_wmm_params(struct mac_context *, tSirMacEdcaParamSetIE *,
			tDot11fIEWMMParams *);
void convert_erp_info(struct mac_context *, tSirMacErpInfo *, tDot11fIEERPInfo *);
void convert_edca_param(struct mac_context *, tSirMacEdcaParamSetIE *,
			tDot11fIEEDCAParamSet *);
void convert_mu_edca_param(struct mac_context * mac_ctx,
			tSirMacEdcaParamSetIE *mu_edca,
			tDot11fIEmu_edca_param_set *ie);
void convert_tspec(struct mac_context *, struct mac_tspec_ie *,
		   tDot11fIETSPEC *);
QDF_STATUS convert_tclas(struct mac_context *, tSirTclasInfo *,
			 tDot11fIETCLAS *);
void convert_wmmtspec(struct mac_context *, struct mac_tspec_ie *,
		      tDot11fIEWMMTSPEC *);
QDF_STATUS convert_wmmtclas(struct mac_context *, tSirTclasInfo *,
			    tDot11fIEWMMTCLAS *);
void convert_ts_delay(struct mac_context *, tSirMacTsDelayIE *, tDot11fIETSDelay *);
void convert_schedule(struct mac_context *, tSirMacScheduleIE *, tDot11fIESchedule *);
void convert_wmm_schedule(struct mac_context *, tSirMacScheduleIE *,
			  tDot11fIEWMMSchedule *);
QDF_STATUS convert_wsc_opaque(struct mac_context *, tSirAddie *,
			      tDot11fIEWscIEOpaque *);
QDF_STATUS convert_p2p_opaque(struct mac_context *, tSirAddie *,
			      tDot11fIEP2PIEOpaque *);
QDF_STATUS convert_p2p2_opaque(struct mac_context *, tSirAddie *,
			       tDot11fIEP2P2IEOpaque *);
#ifdef WLAN_FEATURE_WFD
QDF_STATUS convert_wfd_opaque(struct mac_context *, tSirAddie *,
			      tDot11fIEWFDIEOpaque *);
#endif
void convert_qos_mapset_frame(struct mac_context *, struct qos_map_set *,
			      tDot11fIEQosMapSet *);

#ifdef WLAN_FEATURE_11BN_TEST_SAP

/**
 * populate_dot11f_assoc_probe_rsp_uhr_op_ie - Build UHR Operation IE
 * @mac_ctx: MAC context
 * @session: PE session containing uhr_op_ie configuration
 *
 * Builds the UHR Operation Extended IE into session->uhr_op_ie.data:
 *   [ID][LEN][ExtID][fields...]
 *
 * Notes:
 * - The function back-fills LEN at data[1] with (total_bytes - 2).
 * - Optional sub-IEs are appended based on <feature>_enabled flags.
 *
 * Return: total IE bytes written (including ID and LEN), or 0 on error.
 */
uint16_t
populate_dot11f_assoc_probe_rsp_uhr_op_ie(struct mac_context *mac_ctx,
					  struct pe_session *session);

/**
 * populate_dot11f_bcn_uhr_op_ie - Build UHR Operation IE for Beacon
 * @mac_ctx: MAC context
 * @session: PE session containing UHR OP IE configuration
 *
 * Builds the UHR Operation Extended IE for Beacon frames. The IE is
 * encoded as:
 *     [ID][LEN][ExtID][control][mcs_nss][...]
 *
 * Fields included:
 *  - UHR Operation Control (DPS/NPCA/DBE/P-EDCA flags + DBE BW)
 *  - Basic UHR MCS/NSS info (EHT layout as placeholder)
 *
 * The function writes into session->uhr_op_ie.data and back-fills the
 * IE length (data[1]) as (total_bytes - 2).
 *
 * Return: total bytes written including ID and LEN, or 0 on error.
 */
uint16_t
populate_dot11f_bcn_uhr_op_ie(struct mac_context *mac_ctx,
			      struct pe_session *session);
#else
static inline uint16_t
populate_dot11f_assoc_probe_rsp_uhr_op_ie(struct mac_context *mac_ctx,
					  struct pe_session *session)
{
	return 0;
}

static inline uint16_t
populate_dot11f_bcn_uhr_op_ie(struct mac_context *mac_ctx,
			      struct pe_session *session)
{
	return 0;
}
#endif
#endif
