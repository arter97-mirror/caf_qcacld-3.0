/*
 * Copyright (c) 2012-2021 The Linux Foundation. All rights reserved.
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

/**
 * DOC: wlan_hdd_nan.c
 *
 * WLAN Host Device Driver NAN API implementation
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <net/cfg80211.h>
#include <ani_global.h>
#include "sme_api.h"
#include "wma.h"
#include "wlan_hdd_main.h"
#include "wlan_hdd_nan.h"
#include "osif_sync.h"
#include <qca_vendor.h>
#include "cfg_nan_api.h"
#include "os_if_nan.h"
#include "spatial_reuse_api.h"
#include "wlan_nan_api.h"
#include "nan_ucfg_api.h"
#include "spatial_reuse_ucfg_api.h"
#include <cdp_txrx_ctrl.h>

#if defined(WLAN_FEATURE_NAN) && defined(FEATURE_WLAN_SUPPORT_NAN_STANDARD_MODE)
#ifdef WLAN_FEATURE_11AX
static bool hdd_nan_has_he_caps(struct hdd_context *hdd_ctx)
{
	return hdd_ctx->nan_caps.he.has_he;
}

static void hdd_nan_fill_wiphy_he_caps(struct hdd_context *hdd_ctx,
				       struct wiphy_nan_capa *nan_capa)
{
	nan_capa->phy.he = hdd_ctx->nan_caps.he;
}
#else
static inline bool hdd_nan_has_he_caps(struct hdd_context *hdd_ctx)
{
	return false;
}

static inline void hdd_nan_fill_wiphy_he_caps(struct hdd_context *hdd_ctx,
					      struct wiphy_nan_capa *nan_capa)
{
}
#endif

#ifdef WLAN_FEATURE_11BE
static bool hdd_nan_has_eht_caps(struct hdd_context *hdd_ctx)
{
	return hdd_ctx->nan_caps.eht.has_eht;
}
#else
static inline bool hdd_nan_has_eht_caps(struct hdd_context *hdd_ctx)
{
	return false;
}
#endif

#if defined(WLAN_FEATURE_11BE) && defined(CFG80211_NAN_EHT_SUPPORTED)
/*
 * wiphy_nan_capa.phy.eht is only present when the kernel has been
 * updated to include EHT support in struct wiphy_nan_capa. Guard with
 * CFG80211_NAN_EHT_SUPPORTED until that kernel patch is merged into
 * the target tree.
 */
static void hdd_nan_fill_wiphy_eht_caps(struct hdd_context *hdd_ctx,
					struct wiphy_nan_capa *nan_capa)
{
	nan_capa->phy.eht = hdd_ctx->nan_caps.eht;
}
#else
static inline void hdd_nan_fill_wiphy_eht_caps(struct hdd_context *hdd_ctx,
					       struct wiphy_nan_capa *nan_capa)
{
}
#endif

/**
 * hdd_nan_fill_wiphy_caps() - Fill NAN PHY capabilities into wiphy
 * @hdd_ctx: Pointer to hdd context
 * @nan_capa: Pointer to wiphy NAN capability structure to fill
 *
 * Copies the cached NAN PHY capabilities (HT/VHT/HE/EHT) from the HDD
 * context into the provided wiphy NAN capability structure.
 *
 * Return: None
 */
void hdd_nan_fill_wiphy_caps(struct hdd_context *hdd_ctx,
			     struct wiphy_nan_capa *nan_capa)
{
	if (!hdd_ctx || !nan_capa)
		return;

	/*
	 * Warn if caps have not been populated yet (e.g. if called before
	 * hdd_populate_nan_phy_caps() during early init or after SSR).
	 */
	if (!hdd_ctx->nan_caps.ht.ht_supported &&
	    !hdd_ctx->nan_caps.vht.vht_supported &&
	    !hdd_nan_has_he_caps(hdd_ctx) &&
	    !hdd_nan_has_eht_caps(hdd_ctx))
		hdd_warn("NAN PHY caps not yet populated; wiphy will advertise no NAN PHY caps");
	nan_capa->phy.ht = hdd_ctx->nan_caps.ht;
	nan_capa->phy.vht = hdd_ctx->nan_caps.vht;
	hdd_nan_fill_wiphy_he_caps(hdd_ctx, nan_capa);
	hdd_nan_fill_wiphy_eht_caps(hdd_ctx, nan_capa);
}
#endif

/**
 * wlan_hdd_nan_is_supported() - HDD NAN support query function
 * @hdd_ctx: Pointer to hdd context
 *
 * This function is called to determine if NAN is supported by the
 * driver and by the firmware.
 *
 * Return: true if NAN is supported by the driver and firmware
 */
bool wlan_hdd_nan_is_supported(struct hdd_context *hdd_ctx)
{
	return cfg_nan_get_enable(hdd_ctx->psoc) &&
		sme_is_feature_supported_by_fw(NAN);
}

/**
 * __wlan_hdd_cfg80211_nan_ext_request() - cfg80211 NAN extended request handler
 * @wiphy: driver's wiphy struct
 * @wdev: wireless device to which the request is targeted
 * @data: actual request data (netlink-encapsulated)
 * @data_len: length of @data
 *
 * Handles NAN Extended vendor commands, sends the command to NAN component
 * which parses and forwards the NAN requests.
 *
 * Return: 0 on success, negative errno on failure
 */
static int __wlan_hdd_cfg80211_nan_ext_request(struct wiphy *wiphy,
					       struct wireless_dev *wdev,
					       const void *data,
					       int data_len)
{
	int ret_val;
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct net_device *dev = wdev->netdev;
	struct hdd_adapter *adapter = WLAN_HDD_GET_PRIV_PTR(dev);
	enum scan_reject_states out_reason = SCAN_REJECT_DEFAULT;
	uint8_t conc_vdev_id;

	hdd_enter_dev(wdev->netdev);

	ret_val = wlan_hdd_validate_context(hdd_ctx);
	if (ret_val)
		return ret_val;

	if (QDF_GLOBAL_FTM_MODE == hdd_get_conparam()) {
		hdd_err_rl("Command not allowed in FTM mode");
		return -EPERM;
	}

	if (!wlan_hdd_nan_is_supported(hdd_ctx)) {
		hdd_debug_rl("NAN is not supported");
		return -EPERM;
	}

	if (hdd_is_connection_in_progress(&conc_vdev_id, &out_reason)) {
		if (out_reason == REASSOC_IN_PROGRESS ||
		    out_reason == NAN_ENABLE_DISABLE_IN_PROGRESS) {
			hdd_err("NAN command refused, reason %d", out_reason);
			return -EAGAIN;
		}
	}

	return os_if_process_nan_req(hdd_ctx->pdev, adapter->deflink->vdev_id,
				     data, data_len);
}

int wlan_hdd_cfg80211_nan_ext_request(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data,
				      int data_len)

{
	struct osif_psoc_sync *psoc_sync;
	int errno;

	errno = osif_psoc_sync_op_start(wiphy_dev(wiphy), &psoc_sync);
	if (errno)
		return errno;

	errno = __wlan_hdd_cfg80211_nan_ext_request(wiphy, wdev,
						    data, data_len);

	osif_psoc_sync_op_stop(psoc_sync);

	return errno;
}

void hdd_nan_concurrency_update(void)
{
	struct hdd_context *hdd_ctx = cds_get_context(QDF_MODULE_ID_HDD);
	int ret;

	hdd_enter();
	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret)
		return;

	wlan_twt_concurrency_update(hdd_ctx);
	hdd_exit();
}

#if defined(WLAN_FEATURE_NAN) && defined(FEATURE_WLAN_SUPPORT_NAN_STANDARD_MODE)
void hdd_nan_vdev_destroy(struct wlan_objmgr_psoc *psoc)
{
	struct wlan_objmgr_vdev *vdev;
	struct wlan_hdd_link_info *link_info;
	struct hdd_context *hdd_ctx;

	vdev = wlan_objmgr_get_vdev_by_opmode_from_psoc(
			psoc, QDF_NAN_DISC_MODE, WLAN_NAN_ID);
	if (!vdev) {
		hdd_err("NAN vdev not found");
		return;
	}

	hdd_ctx = cds_get_context(QDF_MODULE_ID_HDD);
	if (!hdd_ctx) {
		hdd_err("hdd_ctx is NULL");
		wlan_objmgr_vdev_release_ref(vdev, WLAN_NAN_ID);
		return;
	}

	link_info = hdd_get_link_info_by_vdev(hdd_ctx, wlan_vdev_get_id(vdev));
	if (!link_info) {
		hdd_err("Failed to find link_info");
		wlan_objmgr_vdev_release_ref(vdev, WLAN_NAN_ID);
		return;
	}

	wlan_objmgr_vdev_release_ref(vdev, WLAN_NAN_ID);
	hdd_vdev_destroy(link_info);
}
#endif

#ifdef WLAN_FEATURE_NAN
#ifdef WLAN_FEATURE_SR
void hdd_nan_sr_concurrency_update(struct nan_event_params *nan_evt)
{
	struct wlan_objmgr_vdev *sta_vdev = NULL;
	uint32_t conc_vdev_id = WLAN_INVALID_VDEV_ID;
	uint8_t sr_ctrl;
	bool is_sr_enabled = false;
	uint32_t sta_vdev_id = WLAN_INVALID_VDEV_ID;
	uint8_t sta_cnt, i;
	uint32_t conn_count;
	uint8_t non_srg_max_pd_offset = 0;
	uint8_t vdev_id_list[MAX_NUMBER_OF_CONC_CONNECTIONS] = {
							WLAN_INVALID_VDEV_ID};
	struct connection_info info[MAX_NUMBER_OF_CONC_CONNECTIONS] = {0};
	uint8_t mac_id;
	QDF_STATUS status;

	conn_count = policy_mgr_get_connection_info(nan_evt->psoc, info);
	if (!conn_count)
		return;
	sta_cnt = policy_mgr_get_mode_specific_conn_info(nan_evt->psoc, NULL,
							 vdev_id_list,
							 PM_STA_MODE);
	/*
	 * Get all active sta vdevs. STA + STA SR concurrency is not supported
	 * so break whenever a first sta with SR enabled is found.
	 */
	for (i = 0; i < sta_cnt; i++) {
		if (vdev_id_list[i] != WLAN_INVALID_VDEV_ID) {
			sta_vdev = wlan_objmgr_get_vdev_by_id_from_psoc(
						    nan_evt->psoc,
						    vdev_id_list[i],
						    WLAN_OSIF_ID);
			if (!sta_vdev) {
				nan_err("sta vdev invalid for vdev id %d",
					vdev_id_list[i]);
				continue;
			}
			ucfg_spatial_reuse_get_sr_config(
					sta_vdev, &sr_ctrl,
					&non_srg_max_pd_offset, &is_sr_enabled);
			if (is_sr_enabled) {
				sta_vdev_id = vdev_id_list[i];
				break;
			}
			wlan_objmgr_vdev_release_ref(sta_vdev, WLAN_OSIF_ID);
		}
	}
	if (sta_cnt && sta_vdev &&
	    (!(sr_ctrl & NON_SRG_PD_SR_DISALLOWED) ||
	    (sr_ctrl & SRG_INFO_PRESENT)) &&
	     is_sr_enabled) {
		if (nan_evt->evt_type == nan_event_id_enable_rsp) {
			wlan_vdev_mlme_set_sr_disable_due_conc(
					sta_vdev, true);
			wlan_spatial_reuse_osif_event(
						sta_vdev, SR_OPERATION_SUSPEND,
						SR_REASON_CODE_CONCURRENCY);
		}
		if (nan_evt->evt_type == nan_event_id_disable_ind ||
		    nan_evt->evt_type == nan_event_id_disable_rsp) {
			if (conn_count > 2) {
				status =
				policy_mgr_get_mac_id_by_session_id(
					nan_evt->psoc, sta_vdev_id,
					&mac_id);
				if (QDF_IS_STATUS_ERROR(status)) {
					hdd_err("get mac id failed");
					goto exit;
				}
				conc_vdev_id =
				policy_mgr_get_conc_vdev_on_same_mac(
					nan_evt->psoc, sta_vdev_id,
					mac_id);
				/*
				 * Don't enable SR, if concurrent vdev is not
				 * NAN and SR concurrency on same mac is not
				 * allowed.
				 */
				if (conc_vdev_id != WLAN_INVALID_VDEV_ID &&
				    !policy_mgr_sr_same_mac_conc_enabled(
				    nan_evt->psoc)) {
					hdd_debug("don't enable SR in SCC/MCC");
					goto exit;
				}
			}
			wlan_vdev_mlme_set_sr_disable_due_conc(sta_vdev, false);
			wlan_spatial_reuse_osif_event(
						sta_vdev, SR_OPERATION_RESUME,
						SR_REASON_CODE_CONCURRENCY);
		}
	}
exit:
	if (sta_vdev && is_sr_enabled)
		wlan_objmgr_vdev_release_ref(sta_vdev, WLAN_OSIF_ID);
}
#endif

#ifdef NDP_TX_BW_FLOW_CTRL
void hdd_ndp_update_peer_bw(uint8_t vdev_id, struct qdf_mac_addr *peer_mac,
			    enum phy_ch_width peer_bw)
{
	struct hdd_context *hdd_ctx = cds_get_context(QDF_MODULE_ID_HDD);
	void *soc = cds_get_context(QDF_MODULE_ID_SOC);
	struct wlan_hdd_link_info *link_info;
	struct hdd_station_ctx *sta_ctx;
	cdp_config_param_type val;
	enum cdp_peer_bw cdp_bw;
	uint8_t idx;

	if (!hdd_ctx)
		return;

	link_info = hdd_get_link_info_by_vdev(hdd_ctx, vdev_id);
	if (!link_info) {
		hdd_err("Invalid vdev");
		return;
	}

	sta_ctx = WLAN_HDD_GET_STATION_CTX_PTR(link_info);

	for (idx = 0; idx < MAX_PEERS; idx++) {
		if (qdf_is_macaddr_zero(&sta_ctx->conn_info.peer_macaddr[idx]))
			break;

		if (qdf_is_macaddr_equal(&sta_ctx->conn_info.peer_macaddr[idx],
					 peer_mac)) {
			hdd_debug("Update NDP peer " QDF_MAC_ADDR_FMT " bandwidth:%u",
				  QDF_MAC_ADDR_REF(peer_mac->bytes), peer_bw);

			cdp_bw = hdd_convert_ch_width_to_cdp_peer_bw(sta_ctx->conn_info.peer_bw[idx]);
			link_info->adapter->ndp_peer_bitmap[cdp_bw] &=
							~BIT(idx ? idx - 1 : 0);

			sta_ctx->conn_info.peer_bw[idx] = peer_bw;
			cdp_bw = hdd_convert_ch_width_to_cdp_peer_bw(peer_bw);
			link_info->adapter->ndp_peer_bitmap[cdp_bw] |=
							BIT(idx ? idx - 1 : 0);

			val.cdp_peer_param_bw = cdp_bw;
			cdp_txrx_set_peer_param(soc, vdev_id, peer_mac->bytes,
						CDP_CONFIG_PEER_BW, val);
			break;
		}
	}
}
#endif

#if defined(FEATURE_WLAN_SUPPORT_NAN_STANDARD_MODE)

#if defined(CONFIG_BAND_6GHZ) && \
	(defined(CFG80211_6GHZ_BAND_SUPPORTED) || \
	 (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)))
static bool hdd_nan_is_6g_enabled(struct hdd_context *hdd_ctx)
{
	return !!(hdd_ctx->iftype_data_6g);
}
#else
static bool hdd_nan_is_6g_enabled(struct hdd_context *hdd_ctx)
{
	return false;
}
#endif

static u8 hdd_nan_map_mpdu_density_to_code(u32 dens_us)
{
	u32 q;

	/* ieee80211_sta_ht_cap.ampdu_density encoding:
	 * 0: 0us, 1: 0.25us, 2: 0.5us, 3: 1us, 4: 2us, 5: 4us, 6: 8us, 7: 16us
	 */
	if (dens_us == 0)
		return IEEE80211_HT_MPDU_DENSITY_NONE;
	/* work in quarter-microsecond units */
	q = dens_us * 4;
	if (q <= 1)
		return IEEE80211_HT_MPDU_DENSITY_0_25;
	if (q <= 2)
		return IEEE80211_HT_MPDU_DENSITY_0_5;
	if (q <= 4)
		return IEEE80211_HT_MPDU_DENSITY_1;
	if (q <= 8)
		return IEEE80211_HT_MPDU_DENSITY_2;
	if (q <= 16)
		return IEEE80211_HT_MPDU_DENSITY_4;
	if (q <= 32)
		return IEEE80211_HT_MPDU_DENSITY_8;
	return IEEE80211_HT_MPDU_DENSITY_16;
}

#ifdef WLAN_FEATURE_11AX
/**
 * hdd_nan_render_he_caps() - Render the NAN component's HE PHY capability
 * result into the kernel-facing ieee80211_sta_he_cap structure
 * @hdd_ctx: HDD context
 * @caps: NAN PHY capability result from the NAN component
 *
 * Return: None
 */
static void hdd_nan_render_he_caps(struct hdd_context *hdd_ctx,
				   struct nan_phy_caps *caps)
{
	struct ieee80211_sta_he_cap *he = &hdd_ctx->nan_caps.he;

	he->has_he = caps->he_supported;
	if (!he->has_he) {
		qdf_mem_zero(he, sizeof(*he));
		return;
	}

	qdf_mem_copy(he->he_cap_elem.mac_cap_info,
		     caps->he_mac_cap_info,
		     sizeof(he->he_cap_elem.mac_cap_info));
	qdf_mem_copy(he->he_cap_elem.phy_cap_info,
		     caps->he_phy_cap_info,
		     sizeof(he->he_cap_elem.phy_cap_info));
	/* he_mcs_nss_supp fields are __le16 */
	he->he_mcs_nss_supp.rx_mcs_80 =
		cpu_to_le16(caps->he_rx_mcs_map_lt_80);
	he->he_mcs_nss_supp.tx_mcs_80 =
		cpu_to_le16(caps->he_tx_mcs_map_lt_80);
	he->he_mcs_nss_supp.rx_mcs_160 =
		cpu_to_le16(caps->he_rx_mcs_map_160);
	he->he_mcs_nss_supp.tx_mcs_160 =
		cpu_to_le16(caps->he_tx_mcs_map_160);
	he->he_mcs_nss_supp.rx_mcs_80p80 =
		cpu_to_le16(caps->he_rx_mcs_map_80p80);
	he->he_mcs_nss_supp.tx_mcs_80p80 =
		cpu_to_le16(caps->he_tx_mcs_map_80p80);
	/* PPE thresholds not available here; advertise none */
	qdf_mem_zero(he->ppe_thres, sizeof(he->ppe_thres));
}
#else
static inline void hdd_nan_render_he_caps(struct hdd_context *hdd_ctx,
					  struct nan_phy_caps *caps)
{
}
#endif

#ifdef WLAN_FEATURE_11BE
/**
 * hdd_nan_render_eht_caps() - Render the NAN component's EHT PHY capability
 * result into the kernel-facing ieee80211_sta_eht_cap structure
 * @hdd_ctx: HDD context
 * @caps: NAN PHY capability result from the NAN component
 *
 * Return: None
 */
static void hdd_nan_render_eht_caps(struct hdd_context *hdd_ctx,
				    struct nan_phy_caps *caps)
{
	struct ieee80211_sta_eht_cap *eht = &hdd_ctx->nan_caps.eht;

	eht->has_eht = caps->eht_supported;
	if (!eht->has_eht) {
		qdf_mem_zero(eht, sizeof(*eht));
		return;
	}

	qdf_mem_copy(eht->eht_cap_elem.mac_cap_info,
		     caps->eht_mac_cap_info,
		     sizeof(eht->eht_cap_elem.mac_cap_info));
	qdf_mem_copy(eht->eht_cap_elem.phy_cap_info,
		     caps->eht_phy_cap_info,
		     sizeof(eht->eht_cap_elem.phy_cap_info));
	qdf_mem_zero(&eht->eht_mcs_nss_supp, sizeof(eht->eht_mcs_nss_supp));
	qdf_mem_copy(&eht->eht_mcs_nss_supp,
		     caps->eht_mcs_nss_supp,
		     QDF_MIN(caps->eht_mcs_nss_supp_len,
			     (uint32_t)sizeof(eht->eht_mcs_nss_supp)));
	/* PPE thresholds: Not advertised for merged NAN PHY */
	qdf_mem_zero(eht->eht_ppe_thres, sizeof(eht->eht_ppe_thres));
}
#else
static inline void hdd_nan_render_eht_caps(struct hdd_context *hdd_ctx,
					   struct nan_phy_caps *caps)
{
}
#endif

/**
 * hdd_populate_nan_phy_caps() - Populate NAN PHY capabilities (HT/VHT/HE/EHT)
 * @hdd_ctx: HDD context
 * @cfg: effective target config (FW intersected with host)
 *
 * Asks the NAN component to compute the FW∩host∩band-enablement HT/VHT/HE
 * capability intersection, then renders that OS-agnostic result into the
 * kernel-facing ieee80211_sta_ht_cap/vht_cap/he_cap structures cached in
 * hdd_ctx->nan_caps for use when advertising via cfg80211/mac80211.
 *
 * Return: None
 */
void hdd_populate_nan_phy_caps(struct hdd_context *hdd_ctx,
			       struct wma_tgt_cfg *cfg)
{
	struct ieee80211_sta_ht_cap *ht;
	struct ieee80211_sta_vht_cap *vht;
	struct nan_phy_caps caps;
	bool enable_2g, enable_5g, enable_6g;
	uint32_t band_capability = 0;
	QDF_STATUS status;
	int i;

	if (!hdd_ctx || !cfg)
		return;

	/*
	 * Determine enabled NAN bands from the configured band capability
	 * bitmap. hdd_is_2g_supported()/hdd_is_5g_supported() cannot be
	 * reused here: they encode "not restricted to the other single
	 * band" (curr_band), so for every possible curr_band value at
	 * least one of them is true, making enable_2g || enable_5g always
	 * true regardless of actual per-band enablement.
	 */
	status = ucfg_mlme_get_band_capability(hdd_ctx->psoc, &band_capability);
	if (QDF_IS_STATUS_ERROR(status))
		hdd_err("Failed to get MLME band capability");

	enable_2g = !!(band_capability & BIT(REG_BAND_2G));
	enable_5g = !!(band_capability & BIT(REG_BAND_5G));
	enable_6g = hdd_nan_is_6g_enabled(hdd_ctx);

	ucfg_nan_set_phy_target_cfg(hdd_ctx->psoc, cfg, hdd_ctx->num_rf_chains,
				    enable_2g, enable_5g, enable_6g);
	ucfg_nan_get_phy_caps(hdd_ctx->psoc, &caps);

	/* Zero the cache first, then take pointers into it (Issue #7:
	 * avoids confusing pattern where pointers appear stale after zero).
	 */
	qdf_mem_zero(&hdd_ctx->nan_caps, sizeof(hdd_ctx->nan_caps));

	ht = &hdd_ctx->nan_caps.ht;
	vht = &hdd_ctx->nan_caps.vht;

	ht->ht_supported = caps.ht_supported;
	if (ht->ht_supported) {
		/* ht->cap is u16 (native endian), not __le16 */
		if (caps.ht_ldpc)
			ht->cap |= IEEE80211_HT_CAP_LDPC_CODING;
		if (caps.ht_sgi_20)
			ht->cap |= IEEE80211_HT_CAP_SGI_20;
		if (caps.ht_sgi_40)
			ht->cap |= IEEE80211_HT_CAP_SGI_40;
		if (caps.ht_rx_stbc)
			ht->cap |= IEEE80211_HT_CAP_RX_STBC;
		if (caps.ht_tx_stbc)
			ht->cap |= IEEE80211_HT_CAP_TX_STBC;

		/* A-MPDU factor: use a conservative default
		 * if host policy doesn't provide
		 */
		ht->ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
		/* A-MPDU density: derive from effective
		 * mpdu_density (microseconds)
		 */
		ht->ampdu_density =
			hdd_nan_map_mpdu_density_to_code(caps.ht_mpdu_density);

		/* HT MCS: set one byte per RF chain */
		qdf_mem_zero(&ht->mcs, sizeof(ht->mcs));
		for (i = 0; i < hdd_ctx->num_rf_chains &&
		     i < IEEE80211_HT_MCS_MASK_LEN; i++)
			ht->mcs.rx_mask[i] = 0xFF;

		ht->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
		ht->mcs.rx_highest = cpu_to_le16(150 * hdd_ctx->num_rf_chains);
	} else {
		qdf_mem_zero(ht, sizeof(*ht));
	}

	vht->vht_supported = caps.vht_supported;
	if (vht->vht_supported) {
		/* vht->cap is u32 (native endian), not __le32 */
		vht->cap = 0;
		if (caps.vht_short_gi_80)
			vht->cap |= IEEE80211_VHT_CAP_SHORT_GI_80;
		if (caps.vht_short_gi_160)
			vht->cap |= IEEE80211_VHT_CAP_SHORT_GI_160;
		if (caps.vht_rx_ldpc)
			vht->cap |= IEEE80211_VHT_CAP_RXLDPC;
		if (caps.vht_tx_stbc)
			vht->cap |= IEEE80211_VHT_CAP_TXSTBC;
		if (caps.vht_rx_stbc)
			vht->cap |= IEEE80211_VHT_CAP_RXSTBC_4 &
				    IEEE80211_VHT_CAP_RXSTBC_MASK;
		if (caps.vht_su_bformer)
			vht->cap |= IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE;
		if (caps.vht_su_bformee)
			vht->cap |= IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE;

		qdf_mem_zero(&vht->vht_mcs, sizeof(vht->vht_mcs));
		/* rx_mcs_map / tx_mcs_map are __le16 */
		vht->vht_mcs.rx_mcs_map = cpu_to_le16(caps.vht_rx_mcs_map);
		vht->vht_mcs.tx_mcs_map = cpu_to_le16(caps.vht_tx_mcs_map);
		/* rx_highest/tx_highest left 0; not bounded here */
	} else {
		qdf_mem_zero(vht, sizeof(*vht));
	}

	hdd_nan_render_he_caps(hdd_ctx, &caps);
	hdd_nan_render_eht_caps(hdd_ctx, &caps);

	hdd_debug("NAN PHY caps: HT=%d cap=0x%x VHT=%d cap=0x%x HE=%d EHT=%d",
		  ht->ht_supported, ht->cap,
		  vht->vht_supported, vht->cap,
		  hdd_nan_has_he_caps(hdd_ctx), hdd_nan_has_eht_caps(hdd_ctx));
}
#endif
#endif
