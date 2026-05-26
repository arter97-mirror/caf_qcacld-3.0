/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

/*
 * DOC: contains Simultaneous Multi Link Device roaming related functionality
 */
#include <wlan_cmn.h>
#include <wlan_cm_public_struct.h>
#include <wlan_cm_roam_public_struct.h>
#include "wlan_mlo_mgr_cmn.h"
#include "wlan_mlo_mgr_main.h"
#include "wlan_mlo_mgr_roam.h"
#include "wlan_mlo_mgr_public_structs.h"
#include "wlan_mlo_mgr_sta.h"
#include <../../core/src/wlan_cm_roam_i.h>
#include "wlan_cm_roam_api.h"
#include "wlan_cm_tgt_if_tx_api.h"
#include "wlan_mlme_vdev_mgr_interface.h"
#include <include/wlan_mlme_cmn.h>
#include <wlan_cm_api.h>
#include <utils_mlo.h>
#include <wlan_mlo_mgr_peer.h>
#include "wlan_mlo_link_force.h"
#include "wlan_scan_api.h"
#include "lim_types.h"
#include <wlan_smd_roam.h>
#include "wlan_cm_roam_offload.h"

#ifdef WLAN_FEATURE_11BN_SMD
static inline QDF_STATUS
smd_update_channel_freq(struct wlan_objmgr_psoc *psoc,
			struct wlan_mlo_link_recfg_req *recfg_req)
{
	struct wlan_objmgr_pdev *pdev;
	struct scan_cache_entry *scan_entry;
	struct wlan_mlo_link_recfg_bss_info *link;
	struct mlo_mgr_context *g_mlo_ctx;
	QDF_STATUS status = QDF_STATUS_SUCCESS;
	uint8_t i;

	if (!recfg_req) {
		mlo_err("recfg_req null");
		return QDF_STATUS_E_INVAL;
	}

	g_mlo_ctx = wlan_objmgr_get_mlo_ctx();
	if (!g_mlo_ctx) {
		mlo_err("Global MLO ctx NULL");
		return QDF_STATUS_E_INVAL;
	}

	pdev = wlan_objmgr_get_pdev_by_id(psoc, 0, WLAN_LINK_RECFG_ID);
	if (!pdev) {
		mlo_err("Invalid pdev");
		return QDF_STATUS_E_INVAL;
	}

	link = &recfg_req->add_link_info.link[0];
	for (i = 0; i < recfg_req->add_link_info.num_links &&
	     i < WLAN_MAX_ML_BSS_LINKS; i++) {
		scan_entry = wlan_scan_get_entry_by_bssid(pdev,
							  &link[i].ap_link_addr);
		if (!scan_entry) {
			mlo_debug("add link " QDF_MAC_ADDR_FMT " scan entry not found",
				  QDF_MAC_ADDR_REF(link[i].ap_link_addr.bytes));
			status = QDF_STATUS_E_INVAL;
			break;
		}
		link[i].freq = scan_entry->channel.chan_freq;
		link[i].link_id = scan_entry->ml_info.self_link_id;
		mlo_debug("SMD Roam Add: freq %d link id %d " QDF_MAC_ADDR_FMT "",
			  link[i].freq, link[i].link_id,
			  QDF_MAC_ADDR_REF(link[i].ap_link_addr.bytes));
		util_scan_free_cache_entry(scan_entry);
	}

	wlan_objmgr_pdev_release_ref(pdev, WLAN_LINK_RECFG_ID);
	return status;
}

static struct mlo_link_info *
smd_link_recfg_find_link_info_with_active_vdev(
	struct wlan_objmgr_psoc *psoc,
	struct wlan_mlo_dev_context *mlo_dev_ctx,
	struct wlan_mlo_link_recfg_bss_info *link_add,
	struct wlan_mlo_link_recfg_bss_info **standby_accepted_link)
{
	struct wlan_objmgr_vdev *vdev;
	struct mlo_link_info *link_info;
	uint8_t *vdev_mac;
	uint8_t j;

	for (j = 0; j < WLAN_MAX_ML_BSS_LINKS; j++) {
		link_info = &mlo_dev_ctx->sta_ctx->links_info[j];

		if (!qdf_is_macaddr_equal(&link_add->self_link_addr,
					  &link_info->link_addr)) {
			mlo_err("link %d info self " QDF_MAC_ADDR_FMT " not equal ADD_LINK: " QDF_MAC_ADDR_FMT "",
				link_info->link_id,
				QDF_MAC_ADDR_REF(link_info->link_addr.bytes),
				QDF_MAC_ADDR_REF(link_add->self_link_addr.bytes));
			continue;
		}

		if (link_info->vdev_id != link_add->vdev_id) {
			mlo_err("Link add vdev id not same as link info");
			continue;
		}

		if (link_info->vdev_id == WLAN_INVALID_VDEV_ID) {
			if (standby_accepted_link && !*standby_accepted_link)
				*standby_accepted_link = link_add;
			continue;
		}

		vdev = wlan_objmgr_get_vdev_by_id_from_psoc(
					psoc, link_info->vdev_id,
					WLAN_LINK_RECFG_ID);
		if (!vdev) {
			mlo_err("Invalid VDEV id %d", link_info->vdev_id);
			continue;
		}

		if (!cm_is_vdev_connected(vdev) && !cm_is_vdev_roaming(vdev)) {
			wlan_objmgr_vdev_release_ref(vdev, WLAN_LINK_RECFG_ID);
			continue;
		}

		vdev_mac = wlan_vdev_mlme_get_linkaddr(vdev);
		if (!qdf_is_macaddr_equal(&link_info->link_addr,
					  (struct qdf_mac_addr *)vdev_mac)) {
			mlo_err("vdev %d MAC address not equal " QDF_MAC_ADDR_FMT " link info self " QDF_MAC_ADDR_FMT "",
				link_info->vdev_id,
				QDF_MAC_ADDR_REF(vdev_mac),
				QDF_MAC_ADDR_REF(link_info->link_addr.bytes));
			wlan_objmgr_vdev_release_ref(vdev, WLAN_LINK_RECFG_ID);
			continue;
		}

		wlan_objmgr_vdev_release_ref(vdev, WLAN_LINK_RECFG_ID);
		return link_info;
	}

	return NULL;
}

/**
 * smd_alloc_copy_roam_sync_ind() - Deep copy roam sync indication into recfg_ctx
 * @recfg_ctx: Link reconfiguration context
 * @sync_ind: Source roam sync indication from WMI
 *
 * sync_ind is freed by target_if_cm_roam_sync_event() after roam_sync_event()
 * returns, regardless of status. This function makes a deep copy into
 * recfg_ctx->cached_sync_ind so the Link Recfg SM can use it asynchronously.
 *
 * Deep copy handles:
 *   - struct body: full qdf_mem_copy
 *   - ric_tspec_data: allocated and copied if ric_data_len > 0
 *   - add_bss_params: set to NULL (not needed by SMD link switches)
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise
 */
static QDF_STATUS
smd_alloc_copy_roam_sync_ind(struct mlo_link_recfg_context *recfg_ctx,
			     struct roam_offload_synch_ind *sync_ind)
{
	recfg_ctx->cached_sync_ind = qdf_mem_malloc(sizeof(*sync_ind));
	if (!recfg_ctx->cached_sync_ind) {
		mlo_err("SMD: failed to alloc cached_sync_ind");
		return QDF_STATUS_E_NOMEM;
	}

	qdf_mem_copy(recfg_ctx->cached_sync_ind, sync_ind, sizeof(*sync_ind));

	/* Deep copy ric_tspec_data if present */
	recfg_ctx->cached_sync_ind->ric_tspec_data = NULL;
	if (sync_ind->ric_tspec_data && sync_ind->ric_data_len) {
		recfg_ctx->cached_sync_ind->ric_tspec_data =
				qdf_mem_malloc(sync_ind->ric_data_len);
		if (!recfg_ctx->cached_sync_ind->ric_tspec_data) {
			mlo_err("SMD: failed to alloc ric_tspec_data copy");
			qdf_mem_free(recfg_ctx->cached_sync_ind);
			recfg_ctx->cached_sync_ind = NULL;
			return QDF_STATUS_E_NOMEM;
		}
		qdf_mem_copy(recfg_ctx->cached_sync_ind->ric_tspec_data,
			     sync_ind->ric_tspec_data, sync_ind->ric_data_len);
	}

	/* add_bss_params has no associated length field and is not
	 * needed by SMD link switches — set to NULL to prevent use
	 * of a dangling pointer after the original sync_ind is freed.
	 */
	recfg_ctx->cached_sync_ind->add_bss_params = NULL;

	return QDF_STATUS_SUCCESS;
}

/**
 * smd_free_cached_sync_ind() - Free the deep copy of roam sync indication
 * @recfg_ctx: Link reconfiguration context
 *
 * Frees all memory allocated by smd_alloc_copy_roam_sync_ind():
 * first the ric_tspec_data buffer (if any), then the struct itself.
 * Safe to call even if cached_sync_ind is NULL.
 */
static void
smd_free_cached_sync_ind(struct mlo_link_recfg_context *recfg_ctx)
{
	if (!recfg_ctx->cached_sync_ind)
		return;

	if (recfg_ctx->cached_sync_ind->ric_tspec_data) {
		qdf_mem_free(recfg_ctx->cached_sync_ind->ric_tspec_data);
		recfg_ctx->cached_sync_ind->ric_tspec_data = NULL;
	}

	qdf_mem_free(recfg_ctx->cached_sync_ind);
	recfg_ctx->cached_sync_ind = NULL;
}

/**
 * smd_validate_repurpose_smd_addr() - Validate SMD address in vdev
 * repurpose request
 * @recfg_ctx: Link reconfiguration context
 * @mlo_dev_ctx: MLO device context
 *
 * Compares the smd_addr in recfg_ctx->vdev_repurpose_req[0] against the
 * smd_identifier stored in mlo_dev_ctx->smd_ctx. Logs an error and returns
 * failure if they do not match.
 *
 * Return: QDF_STATUS_SUCCESS if addresses match, error otherwise
 */
static QDF_STATUS
smd_validate_repurpose_smd_addr(struct mlo_link_recfg_context *recfg_ctx,
				struct wlan_mlo_dev_context *mlo_dev_ctx)
{
	uint8_t i;

	if (!recfg_ctx || !mlo_dev_ctx) {
		mlo_err("SMD: Invalid parameters");
		return QDF_STATUS_E_INVAL;
	}

	if (!recfg_ctx->num_vdev_repurpose_req) {
		mlo_err("SMD: No vdev repurpose requests");
		return QDF_STATUS_E_INVAL;
	}

	if (!mlo_dev_ctx->smd_ctx) {
		mlo_err("SMD: smd_ctx is NULL");
		return QDF_STATUS_E_INVAL;
	}

	for (i = 0; i < recfg_ctx->num_vdev_repurpose_req &&
	     i < WLAN_MAX_ML_BSS_LINKS; i++) {
		if (!qdf_is_macaddr_equal(
				&recfg_ctx->vdev_repurpose_req[i].smd_addr,
				&mlo_dev_ctx->smd_ctx->smd_identifier)) {
			mlo_err("SMD: vdev_repurpose_req[%u] smd_addr "
				QDF_MAC_ADDR_FMT " != smd_identifier "
				QDF_MAC_ADDR_FMT,
				i,
				QDF_MAC_ADDR_REF(recfg_ctx->vdev_repurpose_req[i].smd_addr.bytes),
				QDF_MAC_ADDR_REF(mlo_dev_ctx->smd_ctx->smd_identifier.bytes));
			return QDF_STATUS_E_INVAL;
		}
	}

	return QDF_STATUS_SUCCESS;
}

bool
smd_link_recfg_has_active_vdev_for_add_link(
				struct mlo_link_recfg_context *recfg_ctx,
				struct mlo_link_recfg_state_req *req,
				struct wlan_mlo_link_switch_req *link_sw_req)
{
	struct wlan_objmgr_psoc *psoc;
	struct wlan_mlo_link_recfg_bss_info *link_add;
	struct wlan_objmgr_vdev *vdev;
	uint8_t i, j;
	struct mlo_link_info *link_info;
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct wlan_mlo_link_recfg_bss_info *link_add_reject = NULL;
	struct wlan_mlo_link_recfg_bss_info *link_add_accept = NULL;

	if (!req->add_link_info.num_links) {
		mlo_err("unexpected add link num 0");
		return false;
	}

	mlo_dev_ctx = mlo_link_recfg_get_mlo_ctx(recfg_ctx);
	if (!mlo_dev_ctx) {
		mlo_err("mlo_ctx null");
		return false;
	}

	psoc = mlo_link_recfg_get_psoc(recfg_ctx);
	if (!psoc) {
		mlo_err("psoc null");
		return false;
	}

	/* find an added link which has active vdev, trigger link switch
	 * disconnect and reconnect.
	 */
	for (i = 0; i < req->add_link_info.num_links; i++) {
		link_add = &req->add_link_info.link[i];
		if (link_add->status_code != STATUS_SUCCESS) {
			mlo_debug("link id %d add reject status code %d",
				  link_add->link_id,
				  link_add->status_code);
			link_add_reject = link_add;
			continue;
		}

		/* Find the link info from mlo mgr for the added link.
		 * The self link address are same for Target AP accepted link
		 * and current AP link.
		 * here only find the link info which has "connected" vdev
		 * (on an old deleted link), and then trigger link switch
		 * by host with reason MLO_LINK_SWITCH_REASON_HOST_ADD_LINK.
		 */
		//TODO : Del link info is updated
		//Find the active link to bring down via link switch ,
		//check for self link address match. (add link self link address and )
		// LS_SMD_LNK_REMOVE_BIT set bit 1 , to indicate link removal.

		link_info = smd_link_recfg_find_link_info_with_active_vdev(
						psoc,
						mlo_dev_ctx,
						link_add,
						&link_add_accept);
		if (!link_info) {
			mlo_debug("no find link info for add link self addr " QDF_MAC_ADDR_FMT "",
				  QDF_MAC_ADDR_REF(link_add->self_link_addr.bytes));
			continue;
		}
		link_add->vdev_id = link_info->vdev_id;
		mlo_debug("assign active vdev %d curr self link addr: " QDF_MAC_ADDR_FMT " for add link %d freq %d",
			  link_info->vdev_id,
			  QDF_MAC_ADDR_REF(link_info->link_addr.bytes),
			  link_add->link_id,
			  link_add->freq);
		mlo_debug("old link id %d flag 0x%x on vdev %d ",
			  link_info->link_id,
			  (uint32_t)link_info->link_status_flags,
			  link_info->vdev_id);
		link_sw_req->vdev_id = link_add->vdev_id;
		link_sw_req->curr_ieee_link_id = link_info->link_id;
		link_sw_req->new_ieee_link_id = link_add->link_id;
		link_sw_req->new_primary_freq = link_add->freq;
		link_sw_req->new_phymode = 0;
		link_sw_req->reason = MLO_LINK_SWITCH_REASON_HOST_ADD_LINK;
		link_sw_req->smd_lnk_sw_trigger = true;
		link_sw_req->tgt_ap_link_addr = link_add->ap_link_addr;
		return true;
	}
	/* Check link reject case, for example L1 L2(deleted) -> L1 L3 L4,
	 * L3 is rejected, L4 is accepted. use vdev previously assigned
	 * for L2 to connect to L4. Need mac address change for the vdev by
	 * link switch.
	 */
	if (link_add_reject && link_add_accept) {
		for (j = 0; j < WLAN_MAX_ML_BSS_LINKS; j++) {
			link_info = &mlo_dev_ctx->sta_ctx->links_info[j];

			if (link_info->vdev_id == WLAN_INVALID_VDEV_ID)
				continue;

			if (!qdf_is_macaddr_equal(
					&link_add_reject->self_link_addr,
					 &link_info->link_addr))
				continue;

			vdev = wlan_objmgr_get_vdev_by_id_from_psoc(
						psoc, link_info->vdev_id,
						WLAN_LINK_RECFG_ID);
			if (!vdev) {
				mlo_err("Invalid VDEV id %d",
					link_info->vdev_id);
				continue;
			}

			if (!cm_is_vdev_disconnected(vdev)) {
				wlan_objmgr_vdev_release_ref(
						vdev, WLAN_LINK_RECFG_ID);
				continue;
			}
			wlan_objmgr_vdev_release_ref(vdev, WLAN_LINK_RECFG_ID);
			break;
		}
		if (j == WLAN_MAX_ML_BSS_LINKS) {
			mlo_debug("no find link info for rej self link add " QDF_MAC_ADDR_FMT "",
				  QDF_MAC_ADDR_REF(link_add_reject->self_link_addr.bytes));
			goto end;
		}
		link_add_accept->vdev_id = link_info->vdev_id;
		mlo_debug("link rej, assign active vdev %d curr self link addr: " QDF_MAC_ADDR_FMT " for add link %d freq %d",
			  link_info->vdev_id,
			  QDF_MAC_ADDR_REF(link_info->link_addr.bytes),
			  link_add_accept->link_id,
			  link_add_accept->freq);
		mlo_debug("old link id %d flag 0x%x on vdev %d ",
			  link_info->link_id,
			  (uint32_t)link_info->link_status_flags,
			  link_info->vdev_id);
		link_sw_req->vdev_id = link_add_accept->vdev_id;
		link_sw_req->curr_ieee_link_id = link_info->link_id;
		link_sw_req->new_ieee_link_id = link_add_accept->link_id;
		link_sw_req->new_primary_freq = link_add_accept->freq;
		link_sw_req->new_phymode = 0;
		link_sw_req->reason = MLO_LINK_SWITCH_REASON_HOST_ADD_LINK;
		link_sw_req->smd_lnk_sw_trigger = true;
		link_sw_req->tgt_ap_link_addr = link_add_accept->ap_link_addr;
		return true;
	}

end:
	return false;
}

/**
 * smd_cleanup_curr_ap_link() - Prepare link switch to bring down del link
 *                               during SMD cleanup_vdev scenario
 * @recfg_ctx: Link reconfiguration context
 * @req: Link reconfiguration state request
 * @link_sw_req: Link switch request to be populated
 *
 * Handles the multi-link to single-link SMD roaming case where add_link_info is
 * empty but del_link_info identifies a link to disconnect. Matches each del_link
 * entry against sta_ctx->links_info[] by link_id and ap_link_addr and prepares a
 * disconnect-only link switch request with
 * MLO_LINK_SWITCH_REASON_SMD_ROAM_REMOVE_LINK.
 *
 * Return: true if a matching active link is found and link_sw_req is populated,
 *         false otherwise
 */
bool
smd_cleanup_curr_ap_link(struct mlo_link_recfg_context *recfg_ctx,
			 struct mlo_link_recfg_state_req *req,
			 struct wlan_mlo_link_switch_req *link_sw_req)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct mlo_link_info *link_info;
	struct wlan_mlo_link_recfg_bss_info *del_link;
	uint8_t i, j;

	if (!recfg_ctx || !req || !link_sw_req)
		return false;

	/* Only applies when there is nothing to add but something to remove */
	if (req->add_link_info.num_links || !req->del_link_info.num_links)
		return false;

	mlo_dev_ctx = recfg_ctx->ml_dev;
	if (!mlo_dev_ctx || !mlo_dev_ctx->sta_ctx)
		return false;

	for (i = 0; i < req->del_link_info.num_links; i++) {
		del_link = &req->del_link_info.link[i];

		for (j = 0; j < WLAN_MAX_ML_BSS_LINKS; j++) {
			link_info = &mlo_dev_ctx->sta_ctx->links_info[j];

			if (link_info->link_id != del_link->link_id)
				continue;

			if (!qdf_is_macaddr_equal(&link_info->ap_link_addr,
						  &del_link->ap_link_addr))
				continue;

			if (link_info->vdev_id == WLAN_INVALID_VDEV_ID)
				continue;

			qdf_mem_zero(link_sw_req, sizeof(*link_sw_req));
			link_sw_req->vdev_id            = link_info->vdev_id;
			link_sw_req->curr_ieee_link_id  = del_link->link_id;
			link_sw_req->new_ieee_link_id   = WLAN_INVALID_LINK_ID;
			link_sw_req->new_primary_freq   = 0;
			link_sw_req->new_phymode        = 0;
			link_sw_req->reason             =
				MLO_LINK_SWITCH_REASON_SMD_ROAM_REMOVE_LINK;
			link_sw_req->smd_lnk_sw_trigger = true;

			mlo_debug("SMD cleanup: vdev_id=%d link_id=%d BSSID=" QDF_MAC_ADDR_FMT,
				  link_sw_req->vdev_id,
				  link_sw_req->curr_ieee_link_id,
				  QDF_MAC_ADDR_REF(del_link->ap_link_addr.bytes));
			return true;
		}
	}

	mlo_debug("SMD cleanup: no matching active link found for del_link");
	return false;
}

/*
 * smd_is_vdev_idle_for_link_addition() - Check if vdev is idle for link add
 * @vdev: Pointer to vdev object
 *
 * This function checks if the given vdev is in IDLE state and can be used
 * for adding a new link during SMD roaming without requiring disconnect.
 *
 * Return: true if vdev is idle and suitable for link addition, false otherwise
 */
static bool smd_is_vdev_idle_for_link_addition(struct wlan_objmgr_vdev *vdev)
{
	struct wlan_objmgr_peer *peer;
	enum wlan_vdev_state vdev_state;

	if (!vdev) {
		mlo_err("Invalid vdev");
		return false;
	}

	/* Check vdev state - should be INIT (IDLE/DISCONNECTED) */
	vdev_state = wlan_vdev_mlme_get_state(vdev);
	if (vdev_state != WLAN_VDEV_S_INIT) {
		mlo_debug("Vdev not in INIT state: %d", vdev_state);
		return false;
	}

	/* Check if there's an active peer association */
	peer = wlan_objmgr_vdev_try_get_bsspeer(vdev, WLAN_MLO_MGR_ID);
	if (peer) {
		wlan_objmgr_peer_release_ref(peer, WLAN_MLO_MGR_ID);
		mlo_debug("Vdev has active peer, not idle");
		return false;
	}

	/* Check if link switch is already in progress */
	if (mlo_mgr_is_link_switch_in_progress(vdev)) {
		mlo_debug("Link switch already in progress");
		return false;
	}

	mlo_debug("Vdev %d is idle and suitable for link addition",
		  wlan_vdev_get_id(vdev));
	return true;
}

/*
 * smd_link_recfg_has_idle_vdev_for_add_link() - Check if idle vdev available for link add
 * @recfg_ctx: Link reconfiguration context
 * @req: Link reconfiguration request
 * @link_sw_req: Link switch request to be filled
 *
 * This function checks if there's an idle vdev that can be used for adding
 * a new link during SMD roaming. If found, it prepares the link switch request
 * with MLO_LINK_SWITCH_REASON_SMD_ROAM_ADD_LINK reason.
 *
 * Return: true if idle vdev found and link switch request prepared, false otherwise
 */
bool
smd_link_recfg_has_idle_vdev_for_add_link(
				struct mlo_link_recfg_context *recfg_ctx,
				struct mlo_link_recfg_state_req *req,
				struct wlan_mlo_link_switch_req *link_sw_req)
{
	struct wlan_objmgr_psoc *psoc;
	struct wlan_mlo_link_recfg_bss_info *link_add;
	struct wlan_objmgr_vdev *vdev;
	uint8_t i;
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	bool found_idle_vdev = false;

	if (!req || !link_sw_req) {
		mlo_err("Invalid parameters");
		return false;
	}

	if (!req->add_link_info.num_links) {
		mlo_err("unexpected add link num 0");
		return false;
	}

	mlo_dev_ctx = mlo_link_recfg_get_mlo_ctx(recfg_ctx);
	if (!mlo_dev_ctx) {
		mlo_err("mlo_ctx null");
		return false;
	}

	psoc = mlo_link_recfg_get_psoc(recfg_ctx);
	if (!psoc) {
		mlo_err("psoc null");
		return false;
	}

	/* Search for an idle vdev that can be used for link addition */
	for (i = 0; i < req->add_link_info.num_links; i++) {
		link_add = &req->add_link_info.link[i];

		if (link_add->status_code != STATUS_SUCCESS) {
			mlo_debug("link id %d add reject status code %d",
					link_add->link_id,
					link_add->status_code);
			continue;
		}

		/* Check if this link has an idle vdev assigned */
		if (link_add->vdev_id == WLAN_INVALID_VDEV_ID)
			continue;

		vdev = wlan_objmgr_get_vdev_by_id_from_psoc(
				psoc, link_add->vdev_id,
				WLAN_LINK_RECFG_ID);
		if (!vdev) {
			mlo_err("Invalid VDEV id %d", link_add->vdev_id);
			continue;
		}

		/* Check if this vdev is idle (disconnected) and suitable
		 * for link addition.
		 */
		if (!cm_is_vdev_disconnected(vdev) ||
		    !smd_is_vdev_idle_for_link_addition(vdev)) {
			wlan_objmgr_vdev_release_ref(vdev, WLAN_LINK_RECFG_ID);
			continue;
		}

		wlan_objmgr_vdev_release_ref(vdev, WLAN_LINK_RECFG_ID);

		/* Found an idle vdev */
		ml_link_recfg_sm_lock_acquire(mlo_dev_ctx);
		recfg_ctx->curr_recfg_req.join_pending_vdev_id = link_add->vdev_id;
		ml_link_recfg_sm_lock_release(mlo_dev_ctx);
		mlo_info("Set join_pending_vdev_id %d for idle vdev link addition",
				link_add->vdev_id);

		found_idle_vdev = true;
		break;
	}

	if (!found_idle_vdev) {
		mlo_debug("No idle vdev found for link addition");
		return false;
	}

	mlo_debug("Idle vdev %d found for add link %d freq %d",
			link_add->vdev_id,
			link_add->link_id,
			link_add->freq);

	/* Fill link switch request for idle vdev */
	link_sw_req->vdev_id = link_add->vdev_id;
	link_sw_req->curr_ieee_link_id = WLAN_INVALID_LINK_ID; /* Idle vdev has no current link */
	link_sw_req->new_ieee_link_id = link_add->link_id;
	link_sw_req->new_primary_freq = link_add->freq;
	link_sw_req->new_phymode = 0;
	link_sw_req->reason = MLO_LINK_SWITCH_REASON_SMD_ROAM_ADD_LINK;
	link_sw_req->smd_lnk_sw_trigger = true;
	link_sw_req->tgt_ap_link_addr = link_add->ap_link_addr;

	mlo_info("Idle vdev link switch: vdev %d, new link_id %d, freq %d, reason %d",
			link_sw_req->vdev_id,
			link_sw_req->new_ieee_link_id,
			link_sw_req->new_primary_freq,
			link_sw_req->reason);

	return true;
}

static struct mlo_link_info *
smd_find_current_ap_link_info(struct wlan_objmgr_vdev *vdev, uint8_t del_vdev_id)
{
	struct mlo_link_info *link_info = NULL;
	uint8_t i;

	if (!vdev) {
		mlo_err("Vdev is NULL");
		return NULL;
	}

	if (del_vdev_id == WLAN_INVALID_VDEV_ID) {
		mlo_err("Vdev Link vdev id is INVALID");
		return NULL;
	}

	link_info = mlo_mgr_get_ap_link(vdev);
	for (i = 0; i < WLAN_MAX_ML_BSS_LINKS; i++) {
		if (link_info->vdev_id == del_vdev_id)
			return link_info;

		link_info++;
	}

	return NULL;
}

/**
 * smd_is_ap_link_in_prepared_targets() - Check if AP link exists in prepared targets
 * @smd_ctx: SMD context
 * @ap_link_addr: AP link address to check
 *
 * This function checks if the given AP link address exists in any of the
 * prepared target BSS contexts stored in the SMD context.
 *
 * Return: true if AP link found in any prepared target, false otherwise
 */
static bool
smd_is_ap_link_in_prepared_targets(struct smd_context *smd_ctx,
				    struct qdf_mac_addr *ap_link_addr)
{
	uint8_t i, j;
	struct wlan_mlo_sta *target_bss_ctx;
	struct mlo_link_info *link_info;

	if (!smd_ctx || !ap_link_addr)
		return false;

	/* Iterate through all prepared targets */
	for (i = 0; i < smd_ctx->num_prepared && i < SMD_MAX_PREPARED_TARGETS; i++) {
		if (!smd_ctx->prepared_targets[i].prepared)
			continue;

		target_bss_ctx = smd_ctx->prepared_targets[i].target_bss_ctx;
		if (!target_bss_ctx)
			continue;

		/* Check all links in this target BSS */
		for (j = 0; j < WLAN_MAX_ML_BSS_LINKS; j++) {
			link_info = &target_bss_ctx->links_info[j];

			if (qdf_is_macaddr_equal(&link_info->ap_link_addr,
						 ap_link_addr)) {
				mlo_debug("SMD: AP link " QDF_MAC_ADDR_FMT " found in prepared target %d",
					  QDF_MAC_ADDR_REF(ap_link_addr->bytes), i);
				return true;
			}
		}
	}

	mlo_debug("SMD: AP link " QDF_MAC_ADDR_FMT " NOT found in prepared targets",
		  QDF_MAC_ADDR_REF(ap_link_addr->bytes));
	return false;
}

/**
 * smd_handle_multi_to_multi_link_roaming() - Handle multi-link to
 * multi-link roaming during SMD exec phase
 * @vdev: vdev object
 * @smd_ctx: SMD context
 * @recfg_ctx: Link reconfiguration context
 * @recfg_req: Link reconfiguration request to be updated
 *
 * This function handles multi-link to multi-link roaming scenario by:
 * 1. Copying vdev_repurpose_req to add_link_info (filtered by bringup_vdev flag)
 * 2. Verifying AP links exist in prepared targets
 * 3. Building delete link info based on current AP link information
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise
 */
static QDF_STATUS
smd_handle_multi_to_multi_link_roaming(
	struct wlan_objmgr_vdev *vdev,
	struct smd_context *smd_ctx,
	struct mlo_link_recfg_context *recfg_ctx,
	struct wlan_mlo_link_recfg_req *recfg_req)
{
	struct mlo_link_info *curr_link_info = NULL;
	QDF_STATUS status = QDF_STATUS_SUCCESS;
	uint8_t i, idx;

	if (!vdev || !smd_ctx || !recfg_ctx || !recfg_req) {
		mlo_err("SMD: Invalid parameters for multi-to-multi roaming");
		return QDF_STATUS_E_INVAL;
	}

	/* Check if we have prepared targets */
	if (smd_ctx->num_prepared == 0 ||
	    !smd_ctx->prepared_targets[0].prepared ||
	    !smd_ctx->prepared_targets[0].target_bss_ctx) {
		mlo_debug("SMD: No prepared targets available");
		return QDF_STATUS_E_INVAL;
	}

	/* Copy vdev_repurpose_req to add_link_info and verify in prepared targets */
	for (i = 0, idx = 0; i < recfg_ctx->num_vdev_repurpose_req &&
	     idx < WLAN_MAX_ML_BSS_LINKS; i++) {
		/* Only add links with bringup_vdev flag set */
		if (!recfg_ctx->vdev_repurpose_req[i].bringup_vdev) {
			mlo_debug("SMD: Skipping vdev_id=%u (bringup_vdev=0) BSSID=" QDF_MAC_ADDR_FMT,
				  recfg_ctx->vdev_repurpose_req[i].vdev_id,
				  QDF_MAC_ADDR_REF(recfg_ctx->vdev_repurpose_req[i].bssid.bytes));
			continue;
		}

		/* Verify this AP link exists in prepared targets */
		if (!smd_is_ap_link_in_prepared_targets(smd_ctx,
							&recfg_ctx->vdev_repurpose_req[i].bssid)) {
			mlo_err("SMD: AP link " QDF_MAC_ADDR_FMT " NOT found in prepared targets - ABORT",
				QDF_MAC_ADDR_REF(recfg_ctx->vdev_repurpose_req[i].bssid.bytes));
			return QDF_STATUS_E_INVAL;
		}

		qdf_copy_macaddr(&recfg_req->add_link_info.link[idx].ap_link_addr,
				 &recfg_ctx->vdev_repurpose_req[i].bssid);
		recfg_req->add_link_info.link[idx].vdev_id = recfg_ctx->vdev_repurpose_req[i].vdev_id;
		recfg_req->add_link_info.num_links += 1;
		recfg_req->add_link_info.link[idx].priority_index = i;

		mlo_debug("SMD: Add Target Link Priority %u: vdev_id=%u BSSID=" QDF_MAC_ADDR_FMT " MLD=" QDF_MAC_ADDR_FMT,
			  i,
			  recfg_ctx->vdev_repurpose_req[i].vdev_id,
			  QDF_MAC_ADDR_REF(recfg_ctx->vdev_repurpose_req[i].bssid.bytes),
			  QDF_MAC_ADDR_REF(recfg_ctx->vdev_repurpose_req[i].mld_addr.bytes));
		mlo_debug("SMD: Flags: bringup=%u cleanup=%u inactive_link_pre_stop=%u priority_index=%u",
			  recfg_ctx->vdev_repurpose_req[i].bringup_vdev,
			  recfg_ctx->vdev_repurpose_req[i].cleanup_vdev,
			  recfg_ctx->vdev_repurpose_req[i].inactive_link_pre_stop,
			  recfg_req->add_link_info.link[idx].priority_index);
		idx++;
	}

	/* Build delete link info based on vdev_id */
	for (i = 0, idx = 0; i < recfg_ctx->num_vdev_repurpose_req &&
	     idx < WLAN_MAX_ML_BSS_LINKS; i++) {
		curr_link_info = smd_find_current_ap_link_info(vdev,
							       recfg_ctx->vdev_repurpose_req[i].vdev_id);
		if (!curr_link_info) {
			mlo_err("SMD: Link info not found for vdev id %d",
				recfg_ctx->vdev_repurpose_req[i].vdev_id);
			continue;
		}

		/* Skip if AP link addresses match (same link, not to be deleted) */
		if (qdf_is_macaddr_equal(&curr_link_info->ap_link_addr,
					 &recfg_ctx->vdev_repurpose_req[i].bssid)) {
			mlo_debug("SMD: AP link address match for vdev %d, BSSID=" QDF_MAC_ADDR_FMT " - skip delete",
				  recfg_ctx->vdev_repurpose_req[i].vdev_id,
				  QDF_MAC_ADDR_REF(curr_link_info->ap_link_addr.bytes));
			continue;
		}

		recfg_req->del_link_info.link[idx].vdev_id = recfg_ctx->vdev_repurpose_req[i].vdev_id;
		recfg_req->del_link_info.link[idx].link_id = curr_link_info->link_id;
		qdf_copy_macaddr(&recfg_req->del_link_info.link[idx].self_link_addr,
				 &curr_link_info->link_addr);
		qdf_copy_macaddr(&recfg_req->del_link_info.link[idx].ap_link_addr,
				 &curr_link_info->ap_link_addr);

		mlo_debug("SMD: Delete link_id=%d freq=%d BSSID=" QDF_MAC_ADDR_FMT " vdev_id=%d",
			  curr_link_info->link_id,
			  curr_link_info->chan_freq,
			  QDF_MAC_ADDR_REF(curr_link_info->ap_link_addr.bytes),
			  curr_link_info->vdev_id);

		recfg_req->del_link_info.num_links += 1;
		idx++;
	}

	return status;
}

/**
 * smd_handle_multi_to_single_link_roaming() - Handle multi-link to single-link roaming
 * @vdev: vdev object
 * @smd_ctx: SMD context
 * @recfg_ctx: Link reconfiguration context
 * @recfg_req: Link reconfiguration request to be updated
 *
 * This function detects multi-to-single link roaming scenario by:
 * 1. Counting active links in the target BSS (must be exactly 1)
 * 2. Verifying that none of the vdev repurpose requests have bringup_vdev flag set
 * If both conditions are met, it identifies the vdev marked for cleanup and updates the recfg_req:
 * - Populates del_link_info with cleanup_vdev link information
 * - Clears add_link_info (memset to 0)
 *
 * Return: true if multi-to-single roaming detected and handled, false otherwise
 */
static bool
smd_handle_multi_to_single_link_roaming(
	struct wlan_objmgr_vdev *vdev,
	struct smd_context *smd_ctx,
	struct mlo_link_recfg_context *recfg_ctx,
	struct wlan_mlo_link_recfg_req *recfg_req)
{
	struct wlan_mlo_sta *target_bss_ctx = NULL;
	struct mlo_link_info *link_info = NULL;
	struct mlo_link_info *cleanup_link_info = NULL;
	uint8_t i;
	uint8_t target_link_count = 0;
	uint8_t cleanup_vdev_id = WLAN_INVALID_VDEV_ID;
	bool has_bringup_vdev = false;

	if (!vdev || !smd_ctx || !recfg_ctx || !recfg_req) {
		mlo_err("SMD: Invalid parameters for multi-to-single check");
		return false;
	}

	/* Check if we have prepared targets */
	if (smd_ctx->num_prepared == 0 ||
	    !smd_ctx->prepared_targets[0].prepared ||
	    !smd_ctx->prepared_targets[0].target_bss_ctx) {
		mlo_debug("SMD: No prepared targets available");
		return false;
	}

	target_bss_ctx = smd_ctx->prepared_targets[0].target_bss_ctx;

	/* Count active links in target BSS */
	for (i = 0; i < WLAN_MAX_ML_BSS_LINKS; i++) {
		link_info = &target_bss_ctx->links_info[i];
		if (!qdf_is_macaddr_zero(&link_info->ap_link_addr) &&
		    link_info->link_id != WLAN_INVALID_LINK_ID) {
			target_link_count++;
		}
	}

	/* Check if target has exactly one link */
	if (target_link_count != 1) {
		mlo_debug("SMD: Not multi-to-single roaming, target has %d links",
			  target_link_count);
		return false;
	}

	/* Check if any vdev repurpose request has bringup_vdev flag set */
	for (i = 0; i < recfg_ctx->num_vdev_repurpose_req; i++) {
		if (recfg_ctx->vdev_repurpose_req[i].bringup_vdev) {
			has_bringup_vdev = true;
			mlo_debug("SMD: Found bringup_vdev=1 for vdev_id=%u",
				  recfg_ctx->vdev_repurpose_req[i].vdev_id);
			break;
		}
	}

	/* Multi-to-single roaming requires: target_link_count == 1 AND no bringup_vdev */
	if (has_bringup_vdev) {
		mlo_debug("SMD: Not multi-to-single roaming, has bringup_vdev set");
		return false;
	}

	mlo_debug("SMD: Single/Multi-link to Single-link roaming detected");

	/* Find vdev marked for cleanup (cleanup_vdev == 1) */
	for (i = 0; i < recfg_ctx->num_vdev_repurpose_req; i++) {
		if (recfg_ctx->vdev_repurpose_req[i].cleanup_vdev) {
			cleanup_vdev_id = recfg_ctx->vdev_repurpose_req[i].vdev_id;
			mlo_debug("SMD: Found cleanup vdev_id=%d", cleanup_vdev_id);
			break;
		}
	}

	if (cleanup_vdev_id == WLAN_INVALID_VDEV_ID) {
		mlo_err("SMD: No cleanup vdev found in multi-to-single roaming");
		return false;
	}

	/* Get current link info for cleanup vdev */
	cleanup_link_info = smd_find_current_ap_link_info(vdev, cleanup_vdev_id);
	if (!cleanup_link_info) {
		mlo_err("SMD: Link info not found for cleanup vdev_id=%d",
			cleanup_vdev_id);
		return false;
	}

	/* Clear add_link_info */
	qdf_mem_zero(&recfg_req->add_link_info,
		     sizeof(struct wlan_mlo_link_recfg_info));
	mlo_debug("SMD: Cleared add_link_info for multi-to-single roaming");

	/* Populate del_link_info with cleanup vdev link information */
	recfg_req->del_link_info.link[0].vdev_id = cleanup_vdev_id;
	recfg_req->del_link_info.link[0].link_id = cleanup_link_info->link_id;
	qdf_copy_macaddr(&recfg_req->del_link_info.link[0].self_link_addr,
			 &cleanup_link_info->link_addr);
	qdf_copy_macaddr(&recfg_req->del_link_info.link[0].ap_link_addr,
			 &cleanup_link_info->ap_link_addr);
	recfg_req->del_link_info.num_links = 1;

	mlo_debug("SMD: Populated del_link_info - vdev_id=%d link_id=%d freq=%d BSSID=" QDF_MAC_ADDR_FMT,
		  cleanup_vdev_id,
		  cleanup_link_info->link_id,
		  cleanup_link_info->chan_freq,
		  QDF_MAC_ADDR_REF(cleanup_link_info->ap_link_addr.bytes));

	return true;
}

/**
 * smd_populate_add_link_info() - Populate add_link_info from vdev_repurpose_req
 * @recfg_ctx: Link reconfiguration context
 * @recfg_req: Link reconfiguration request to be updated
 *
 * This helper function populates the add_link_info structure in recfg_req
 * by iterating through vdev_repurpose_req array in recfg_ctx. Only entries
 * with bringup_vdev flag set to 1 are added. For each entry, it copies the
 * BSSID, vdev_id, and priority information, and logs debug messages about
 * the target link configuration.
 *
 * Return: None
 */
static void
smd_populate_add_link_info(struct mlo_link_recfg_context *recfg_ctx,
			    struct wlan_mlo_link_recfg_req *recfg_req)
{
	uint8_t i, idx;

	for (i = 0, idx = 0; i < recfg_ctx->num_vdev_repurpose_req &&
	     idx < WLAN_MAX_ML_BSS_LINKS; i++) {
		/* Only add links with bringup_vdev flag set */
		if (!recfg_ctx->vdev_repurpose_req[i].bringup_vdev) {
			mlo_debug("SMD: Skipping vdev_id=%u (bringup_vdev=0)",
				  recfg_ctx->vdev_repurpose_req[i].vdev_id);
			continue;
		}

		qdf_copy_macaddr(&recfg_req->add_link_info.link[idx].ap_link_addr,
				 &recfg_ctx->vdev_repurpose_req[i].bssid);

		recfg_req->add_link_info.link[idx].vdev_id = recfg_ctx->vdev_repurpose_req[i].vdev_id;
		recfg_req->add_link_info.num_links += 1;
		recfg_req->add_link_info.link[idx].priority_index = i;
		mlo_debug("SMD: Add Target Link Priority %u: vdev_id=%u BSSID=" QDF_MAC_ADDR_FMT " MLD=" QDF_MAC_ADDR_FMT,
			  i,
			  recfg_ctx->vdev_repurpose_req[i].vdev_id,
			  QDF_MAC_ADDR_REF(recfg_ctx->vdev_repurpose_req[i].bssid.bytes),
			  QDF_MAC_ADDR_REF(recfg_ctx->vdev_repurpose_req[i].mld_addr.bytes));
		mlo_debug("Flags: bringup: %u cleanup: %u,inactive_link_pre_stop: %u, priority_index: %u",
			  recfg_ctx->vdev_repurpose_req[i].bringup_vdev,
			  recfg_ctx->vdev_repurpose_req[i].cleanup_vdev,
			  recfg_ctx->vdev_repurpose_req[i].inactive_link_pre_stop,
			  recfg_req->add_link_info.link[idx].priority_index);
		idx++;
	}
}

/**
 * smd_roam_prep_sl_to_sl_ml_handler() - Handle single-link to single/multi-link roaming
 * @vdev: VDEV object
 * @recfg_ctx: Link reconfiguration context
 * @recfg_req: Link reconfiguration request to be populated
 *
 * This function handles the single-link to single-link or single-link to
 * multi-link roaming scenario during SMD roaming preparation. It checks if:
 * 1. Current associated links count is exactly 1
 * 2. Number of vdev repurpose requests is greater than 1
 * 3. At least one vdev repurpose request has bringup_vdev flag set
 *
 * If all conditions are met, it uses smd_populate_add_link_info() to populate
 * the add_link_info structure with target AP links that have bringup_vdev=1,
 * while keeping del_link_info empty since idle vdevs are available.
 *
 * Return: QDF_STATUS_SUCCESS if scenario detected and handled,
 *         QDF_STATUS_E_AGAIN if conditions not met
 */
static QDF_STATUS
smd_roam_prep_sl_to_sl_ml_handler(
	struct wlan_objmgr_vdev *vdev,
	struct mlo_link_recfg_context *recfg_ctx,
	struct wlan_mlo_link_recfg_req *recfg_req)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct mlo_link_info *link_info;
	struct wlan_objmgr_psoc *psoc;
	struct wlan_objmgr_vdev *link_vdev;
	uint8_t i;
	uint8_t curr_active_links = 0;
	bool has_bringup_vdev = false;

	if (!vdev || !recfg_ctx || !recfg_req) {
		mlo_err("SMD: Invalid parameters for SL to SL/ML handler");
		return QDF_STATUS_E_INVAL;
	}

	mlo_dev_ctx = vdev->mlo_dev_ctx;
	if (!mlo_dev_ctx) {
		mlo_err("SMD: MLO dev context is NULL");
		return QDF_STATUS_E_INVAL;
	}

	psoc = wlan_vdev_get_psoc(vdev);
	if (!psoc) {
		mlo_err("SMD: PSOC is NULL");
		return QDF_STATUS_E_INVAL;
	}

	/* Step 1: Count current active links */
	for (i = 0; i < WLAN_MAX_ML_BSS_LINKS; i++) {
		link_info = &mlo_dev_ctx->sta_ctx->links_info[i];

		if (link_info->vdev_id == WLAN_INVALID_VDEV_ID)
			continue;

		if (qdf_is_macaddr_zero(&link_info->ap_link_addr))
			continue;

		if (link_info->link_id == WLAN_INVALID_LINK_ID)
			continue;

		link_vdev = wlan_objmgr_get_vdev_by_id_from_psoc(
					psoc, link_info->vdev_id,
					WLAN_MLO_MGR_ID);
		if (!link_vdev)
			continue;

		if (cm_is_vdev_connected(link_vdev) ||
		    cm_is_vdev_roaming(link_vdev))
			curr_active_links++;

		wlan_objmgr_vdev_release_ref(link_vdev, WLAN_MLO_MGR_ID);
	}

	mlo_debug("SMD: Current active links count: %u", curr_active_links);

	/* Check if current associated links == 1 */
	if (curr_active_links != 1) {
		mlo_debug("SMD: Not SL to SL/ML scenario, active links: %u",
			  curr_active_links);
		return QDF_STATUS_E_AGAIN;
	}

	/* Step 2: Check if num_vdev_repurpose_req > 1 */
	if (recfg_ctx->num_vdev_repurpose_req < 1) {
		mlo_debug("SMD: num_vdev_repurpose_req=%u, not SL to ML scenario",
			  recfg_ctx->num_vdev_repurpose_req);
		return QDF_STATUS_E_AGAIN;
	}

	/* Check if any vdev repurpose request has bringup_vdev flag set */
	for (i = 0; i < recfg_ctx->num_vdev_repurpose_req; i++) {
		if (recfg_ctx->vdev_repurpose_req[i].bringup_vdev) {
			has_bringup_vdev = true;
			mlo_debug("SMD: Found bringup_vdev=1 at index %u", i);
			break;
		}
	}

	if (!has_bringup_vdev) {
		mlo_debug("SMD: No bringup_vdev found, not SL to SL/ML scenario");
		return QDF_STATUS_E_AGAIN;
	}

	mlo_debug("SMD: Single-link to Single/Multi-link roaming detected");

	/* Step 3: Use common helper to populate add_link_info
	 *  (filters by bringup_vdev)
	 */
	smd_populate_add_link_info(recfg_ctx, recfg_req);

	/* Step 4: Keep del_link_info empty (already zeroed by caller) */
	mlo_debug("SMD: del_link_info kept empty (idle vdevs available)");

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS
smd_roam_store_key(struct mlo_link_recfg_context *ctx,
		   struct mlo_link_recfg_state_req *req)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	uint8_t i;
	struct mlo_link_info *link_info;
	struct wlan_objmgr_vdev *vdev = NULL;
	struct wlan_mlo_link_recfg_bss_info *add_link;
	struct wlan_objmgr_psoc *psoc = NULL;
	QDF_STATUS status = QDF_STATUS_SUCCESS;

	if (!req || !ctx)
		return QDF_STATUS_E_NULL_VALUE;

	if (!req->add_link_info.num_links)
		return QDF_STATUS_SUCCESS;

	mlo_dev_ctx = mlo_link_recfg_get_mlo_ctx(ctx);
	if (!mlo_dev_ctx) {
		mlo_err("mlo_ctx null");
		return QDF_STATUS_E_INVAL;
	}

	psoc = mlo_link_recfg_get_psoc(ctx);
	if (!psoc) {
		mlo_err("psoc is null");
		return QDF_STATUS_E_INVAL;
	}

	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(
				psoc, ctx->curr_recfg_req.vdev_id,
				WLAN_LINK_RECFG_ID);
	if (!vdev) {
		mlo_err("Invalid link recfg VDEV %d",
			ctx->curr_recfg_req.vdev_id);
		return QDF_STATUS_E_INVAL;
	}

	for (i = 0; i < req->add_link_info.num_links; i++) {
		add_link = &req->add_link_info.link[i];
		if (add_link->status_code != STATUS_SUCCESS) {
			mlo_debug("link id %d add with failure status code %d",
				  add_link->link_id,
				  add_link->status_code);
			continue;
		}

		link_info = smd_get_prepared_ap_link_info(vdev,
							  &add_link->ap_link_addr);
		if (!link_info) {
			mlo_debug("unexpected link info null for link id %d ap link mac " QDF_MAC_ADDR_FMT "",
				  add_link->link_id,
				  QDF_MAC_ADDR_REF(add_link->ap_link_addr.bytes));
			wlan_objmgr_vdev_release_ref(vdev, WLAN_LINK_RECFG_ID);
			return QDF_STATUS_E_INVAL;
		}

		status = mlo_link_recfg_save_unicast_key(ctx, vdev,
							 &link_info->link_addr,
							 &link_info->ap_link_addr,
							 link_info->link_id);
		if (QDF_IS_STATUS_ERROR(status))
			mlo_err("link unicast key save failed");
		else
			mlo_debug("unicast key saved for link id %d ap link mac " QDF_MAC_ADDR_FMT "",
				  link_info->link_id,
				  QDF_MAC_ADDR_REF(link_info->link_addr.bytes));
	}

	wlan_objmgr_vdev_release_ref(vdev, WLAN_LINK_RECFG_ID);
	return status;
}

/**
 * smd_roam_prep_ml_to_sl_ml_handler() - Handle multi-link to single/multi-link roaming
 * @vdev: VDEV object
 * @recfg_ctx: Link reconfiguration context
 * @recfg_req: Link reconfiguration request to be populated
 *
 * This function handles the multi-link to single-link or multi-link to
 * multi-link roaming scenario during SMD roaming preparation. It checks if:
 * 1. Current associated links count is greater than 1
 * 2. Number of vdev repurpose requests is >= 1
 * 3. At least one vdev repurpose request has bringup_vdev flag set
 *
 * If all conditions are met, it uses smd_populate_add_link_info() to populate
 * the add_link_info structure with target AP links that have bringup_vdev=1.
 *
 * Return: QDF_STATUS_SUCCESS if scenario detected and handled,
 *         QDF_STATUS_E_AGAIN if conditions not met
 */
static QDF_STATUS
smd_roam_prep_ml_to_sl_ml_handler(
	struct wlan_objmgr_vdev *vdev,
	struct mlo_link_recfg_context *recfg_ctx,
	struct wlan_mlo_link_recfg_req *recfg_req)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct mlo_link_info *link_info;
	struct wlan_objmgr_psoc *psoc;
	struct wlan_objmgr_vdev *link_vdev;
	uint8_t i, idx;
	uint8_t curr_active_links = 0;
	bool has_bringup_vdev = false;

	if (!vdev || !recfg_ctx || !recfg_req) {
		mlo_err("SMD: Invalid parameters for ML to SL/ML handler");
		return QDF_STATUS_E_INVAL;
	}

	mlo_dev_ctx = vdev->mlo_dev_ctx;
	if (!mlo_dev_ctx) {
		mlo_err("SMD: MLO dev context is NULL");
		return QDF_STATUS_E_INVAL;
	}

	psoc = wlan_vdev_get_psoc(vdev);
	if (!psoc) {
		mlo_err("SMD: PSOC is NULL");
		return QDF_STATUS_E_INVAL;
	}

	/* Step 1: Count current active links */
	for (i = 0; i < WLAN_MAX_ML_BSS_LINKS; i++) {
		link_info = &mlo_dev_ctx->sta_ctx->links_info[i];

		if (link_info->vdev_id == WLAN_INVALID_VDEV_ID)
			continue;

		if (qdf_is_macaddr_zero(&link_info->ap_link_addr))
			continue;

		if (link_info->link_id == WLAN_INVALID_LINK_ID)
			continue;

		link_vdev = wlan_objmgr_get_vdev_by_id_from_psoc(
					psoc, link_info->vdev_id,
					WLAN_MLO_MGR_ID);
		if (!link_vdev)
			continue;

		if (cm_is_vdev_connected(link_vdev) || cm_is_vdev_roaming(link_vdev))
			curr_active_links++;

		wlan_objmgr_vdev_release_ref(link_vdev, WLAN_MLO_MGR_ID);
	}

	mlo_debug("SMD: Current active links count: %u", curr_active_links);

	/* Check if current associated links > 1 */
	if (curr_active_links <= 1) {
		mlo_debug("SMD: Not ML to SL/ML scenario, active links: %u",
			  curr_active_links);
		return QDF_STATUS_E_AGAIN;
	}

	/* Step 2: Check if num_vdev_repurpose_req >= 1 */
	if (recfg_ctx->num_vdev_repurpose_req < 1) {
		mlo_debug("SMD: num_vdev_repurpose_req=%u, invalid",
			  recfg_ctx->num_vdev_repurpose_req);
		return QDF_STATUS_E_AGAIN;
	}

	/* Check if any vdev repurpose request has bringup_vdev flag set */
	for (i = 0; i < recfg_ctx->num_vdev_repurpose_req; i++) {
		if (recfg_ctx->vdev_repurpose_req[i].bringup_vdev) {
			has_bringup_vdev = true;
			mlo_debug("SMD: Found bringup_vdev=1 at index %u", i);
			break;
		}
	}

	if (!has_bringup_vdev) {
		mlo_debug("SMD: No bringup_vdev found, not ML to SL/ML scenario");
		return QDF_STATUS_E_AGAIN;
	}

	mlo_debug("SMD: Multi-link to Single/Multi-link roaming detected");

	/* Step 3: Use common helper to populate add_link_info (filters by bringup_vdev) */
	smd_populate_add_link_info(recfg_ctx, recfg_req);

	/* Step 4: Populate del_link_info from vdev_repurpose_req */
	/* SMD vdev repurpose req is populated by priority
	 * copy to delete link info.
	 * Example:
	 * index 0 will be deleted first, if AP accepts the index 0 add link
	 */
	for (i = 0, idx = 0; i < recfg_ctx->num_vdev_repurpose_req &&
	     idx < WLAN_MAX_ML_BSS_LINKS; i++) {
		link_info = smd_find_current_ap_link_info(vdev,
							  recfg_ctx->vdev_repurpose_req[i].vdev_id);
		if (!link_info) {
			mlo_err("SMD: Link info not found for vdev id %d",
				recfg_ctx->vdev_repurpose_req[i].vdev_id);
			continue;
		}

		/* Skip if AP link addresses match (same link, not to be deleted) */
		if (qdf_is_macaddr_equal(&link_info->ap_link_addr,
					 &recfg_ctx->vdev_repurpose_req[i].bssid)) {
			mlo_debug("SMD: AP link address match for vdev %d, BSSID=" QDF_MAC_ADDR_FMT " - skip delete",
				  recfg_ctx->vdev_repurpose_req[i].vdev_id,
				  QDF_MAC_ADDR_REF(link_info->ap_link_addr.bytes));
			continue;
		}

		recfg_req->del_link_info.link[idx].vdev_id = recfg_ctx->vdev_repurpose_req[i].vdev_id;
		recfg_req->del_link_info.link[idx].link_id = link_info->link_id;
		qdf_copy_macaddr(&recfg_req->del_link_info.link[idx].self_link_addr,
				 &link_info->link_addr);
		qdf_copy_macaddr(&recfg_req->del_link_info.link[idx].ap_link_addr,
				 &link_info->ap_link_addr);

		mlo_debug("SMD: Delete link_id=%d freq=%d BSSID=" QDF_MAC_ADDR_FMT " vdev_id=%d",
			  link_info->link_id,
			  link_info->chan_freq,
			  QDF_MAC_ADDR_REF(link_info->ap_link_addr.bytes),
			  link_info->vdev_id);

		recfg_req->del_link_info.num_links += 1;
		idx++;
	}

	return QDF_STATUS_SUCCESS;
}

/**
 * smd_send_roam_start_status_cmd() - Send SMD roam start status to firmware
 * @recfg_ctx: Link reconfiguration context
 * @req: Link reconfiguration state request whose add_link_info has been
 *       updated with per-link PREP response status codes by
 *       mlo_link_recfg_update_state_req_from_rsp() (i.e. tran->req)
 * @prep_status: Overall ST Prep phase result; one of enum smd_prep_status.
 *               SMD_PREP_STATUS_SUCCESS instructs FW to proceed with ST Exec.
 *
 * This function sends the host's response to firmware's SMD start notification.
 * It includes the SMD transition IE from AP, per-link PREP response status,
 * and KCK for per AP-MLD PTK mode.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise
 */
static QDF_STATUS
smd_send_roam_start_status_cmd(struct mlo_link_recfg_context *recfg_ctx,
			       struct mlo_link_recfg_state_req *req,
			       enum smd_prep_status prep_status)
{
	struct wlan_objmgr_psoc *psoc;
	struct wlan_objmgr_vdev *vdev;
	struct wlan_cm_roam_tx_ops *tx_ops;
	struct wlan_roam_smd_start_status_params params = {0};
	struct wlan_mlo_link_recfg_info *add_link_info;
	QDF_STATUS status;
	uint8_t vdev_id;
	uint8_t i;

	if (!recfg_ctx || !req) {
		mlo_err("recfg_ctx %pK or req %pK is null", recfg_ctx, req);
		return QDF_STATUS_E_INVAL;
	}

	psoc = mlo_link_recfg_get_psoc(recfg_ctx);
	if (!psoc) {
		mlo_err("psoc is null");
		return QDF_STATUS_E_NULL_VALUE;
	}

	vdev_id = recfg_ctx->curr_recfg_req.vdev_id;
	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(psoc, vdev_id,
						    WLAN_MLO_MGR_ID);
	if (!vdev) {
		mlo_err("vdev is null for id %d", vdev_id);
		return QDF_STATUS_E_NULL_VALUE;
	}

	tx_ops = wlan_cm_roam_get_tx_ops_from_vdev(vdev);
	if (!tx_ops || !tx_ops->send_smd_roam_start_status_cmd) {
		mlo_err("TX ops not registered");
		wlan_objmgr_vdev_release_ref(vdev, WLAN_MLO_MGR_ID);
		return QDF_STATUS_E_INVAL;
	}

	params.vdev_id = vdev_id;
	params.status = prep_status;

	/* Populate SMD transition IE received from FW */
	if (recfg_ctx->smd_transition_ie.ie_len) {
		params.smd_transition_ie_len =
				recfg_ctx->smd_transition_ie.ie_len;
		params.smd_transition_ie =
				recfg_ctx->smd_transition_ie.ie_data;
	}

	/* Populate per-link PREP response status from add_link_info.
	 * Status codes are written into tran->req by
	 * mlo_link_recfg_update_state_req_from_rsp() before this call.
	 */
	add_link_info = &req->add_link_info;
	params.num_prep_status = add_link_info->num_links;
	for (i = 0; i < add_link_info->num_links &&
	     i < WLAN_MAX_ML_BSS_LINKS; i++) {
		params.prep_status_list[i].ieee_link_id =
				add_link_info->link[i].link_id;
		params.prep_status_list[i].status =
				add_link_info->link[i].status_code;
	}

	/* Copy KCK for per AP-MLD PTK mode if provided */
	if (add_link_info->kck_len > 0) {
		params.kck_len = add_link_info->kck_len;
		params.kck = add_link_info->kck;
	}

	mlo_debug("Sending SMD start status: vdev_id=%u status=%u ie_len=%u num_prep_status=%u kck_len=%u",
		  params.vdev_id, params.status, params.smd_transition_ie_len,
		  params.num_prep_status, params.kck_len);

	status = tx_ops->send_smd_roam_start_status_cmd(psoc, &params);
	if (QDF_IS_STATUS_ERROR(status))
		mlo_err("Failed to send SMD roam start status cmd, status: %d",
			status);

	wlan_objmgr_vdev_release_ref(vdev, WLAN_MLO_MGR_ID);
	return status;
}

/**
 * smd_roam_cleanup_ies() - Free prepared target BSS contexts in smd_ctx
 *                          and the SMD-specific fields of recfg_ctx.
 * @recfg_ctx: Link reconfiguration context
 *
 * Walks smd_ctx->prepared_targets[], frees each target_bss_ctx and its
 * per-link link_chan_info allocations, then resets the smd_ctx roaming
 * state fields. Always follows up with smd_link_recfg_ctx_cleanup() so
 * recfg_ctx's own SMD fields (cached_sync_ind, vdev_repurpose_req[], etc.)
 * are cleaned up too, regardless of whether smd_ctx is present.
 * Safe to call on both success and failure paths.
 */
static void
smd_roam_cleanup_ies(struct mlo_link_recfg_context *recfg_ctx)
{
	struct smd_context *smd_ctx;
	struct wlan_mlo_sta *tgt;
	uint8_t i, k;

	if (!recfg_ctx || !recfg_ctx->ml_dev)
		return;

	smd_ctx = recfg_ctx->ml_dev->smd_ctx;
	if (smd_ctx) {
		for (i = 0; i < SMD_MAX_PREPARED_TARGETS; i++) {
			tgt = smd_ctx->prepared_targets[i].target_bss_ctx;
			if (!tgt)
				continue;
			for (k = 0; k < WLAN_MAX_ML_BSS_LINKS; k++) {
				qdf_mem_free(tgt->links_info[k].link_chan_info);
				tgt->links_info[k].link_chan_info = NULL;
			}
			qdf_mem_free(tgt);
			smd_ctx->prepared_targets[i].target_bss_ctx = NULL;
			smd_ctx->prepared_targets[i].prepared = false;
		}
		smd_ctx->num_prepared = 0;
		smd_ctx->active_target_idx = 0;
		smd_ctx->smd_roaming_in_progress = false;
		smd_ctx->st_prep_in_progress = false;
		smd_ctx->st_exec_in_progress = false;
	}

	smd_link_recfg_ctx_cleanup(recfg_ctx);
}

QDF_STATUS smd_fw_roam_start(struct wlan_objmgr_vdev *vdev)
{
	struct wlan_objmgr_psoc *psoc;
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct mlo_link_recfg_context *recfg_ctx;
	struct wlan_mlo_link_recfg_req recfg_req = {0};
	QDF_STATUS status;

	if (!vdev) {
		mlo_err("Vdev is NULL");
		return QDF_STATUS_E_INVAL;
	}

	psoc = wlan_vdev_get_psoc(vdev);
	if (!psoc) {
		mlo_err("PSOC is NULL");
		return QDF_STATUS_E_INVAL;
	}

	mlo_dev_ctx = vdev->mlo_dev_ctx;
	if (!mlo_dev_ctx) {
		mlo_err("MLO dev context is NULL");
		return QDF_STATUS_E_INVAL;
	}

	recfg_ctx = mlo_dev_ctx->link_recfg_ctx;
	if (!recfg_ctx) {
		mlo_err("Link recfg context is NULL");
		return QDF_STATUS_E_INVAL;
	}

	/* Check if vdev is in roaming state */
	if (!wlan_cm_is_vdev_roaming(vdev)) {
		mlme_err("SMD: vdev %d is NOT in roaming state",
			 wlan_vdev_get_id(vdev));
		return QDF_STATUS_E_FAILURE;
	}

	if (smd_roam_in_progress(recfg_ctx) &&
	    mlo_is_link_recfg_in_progress(vdev)) {
		mlo_err("SMD roam in progress, legacy ROAM_START received, aborting link recfg SM");
		mlo_link_recfg_sm_deliver_event(mlo_dev_ctx,
						WLAN_LINK_RECFG_SM_EV_DISCONNECT_IND,
						0, NULL);
		return QDF_STATUS_SUCCESS;
	}

	/* Validate VDEV repurpose TLVs */
	if (!recfg_ctx->num_vdev_repurpose_req) {
		mlo_err("SMD: No VDEV repurpose requests");
		return QDF_STATUS_E_INVAL;
	}

	status = smd_validate_repurpose_smd_addr(recfg_ctx, mlo_dev_ctx);
	if (QDF_IS_STATUS_ERROR(status)) {
		mlo_err("SMD: SMD address does not match");
		return QDF_STATUS_E_INVAL;
	}

	mlo_debug("SMD: Roaming started, num_vdev_repurpose_req=%u",
		  recfg_ctx->num_vdev_repurpose_req);

	qdf_mem_zero(&recfg_req, sizeof(struct wlan_mlo_link_recfg_req));
	qdf_copy_macaddr(&recfg_req.add_link_info.mld_addr,
			 &recfg_ctx->vdev_repurpose_req[0].mld_addr);

	qdf_copy_macaddr(&recfg_req.add_link_info.smd_addr,
			 &recfg_ctx->vdev_repurpose_req[0].smd_addr);

	/* Try single-link to single/multi-link handler first */
	status = smd_roam_prep_sl_to_sl_ml_handler(vdev, recfg_ctx, &recfg_req);
	if (QDF_IS_STATUS_SUCCESS(status)) {
		mlo_debug("SMD: Single-link handler succeeded");
		goto end;
	}

	/* Try multi-link to single/multi-link handler */
	status = smd_roam_prep_ml_to_sl_ml_handler(vdev, recfg_ctx, &recfg_req);
	if (QDF_IS_STATUS_ERROR(status)) {
		mlo_debug("SMD: Multi-link handler failure");
		goto exit;
	}

end:
	mlo_debug("SMD: Stored target AP link bitmap in link recfg ctx: 0x%x",
		  recfg_ctx->tgt_ap_link_bitmap);

	recfg_req.vdev_id = wlan_vdev_get_id(vdev);
	recfg_req.is_user_req = false;  /* SMD roaming is FW-initiated */
	recfg_req.is_fw_ind_received = true; /* This is from FW roam event */
	recfg_req.st_prep_link_recfg = true; /* This is for ST Prep req */
	recfg_req.st_exec_link_recfg = false;

	status = smd_update_channel_freq(psoc, &recfg_req);
	if (QDF_IS_STATUS_ERROR(status)) {
		mlo_err("failed to find link freq for fw link recfg ind event");
		goto exit;
	}

	status = mlo_link_recfg_sm_deliver_event(
				mlo_dev_ctx,
				WLAN_LINK_RECFG_SM_EV_SMD_ROAM_START,
				sizeof(recfg_req), &recfg_req);
exit:
	return status;
}

QDF_STATUS smd_start_link_recfg(struct wlan_objmgr_vdev *vdev,
				struct roam_offload_roam_event *roam_event)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct mlo_link_recfg_context *recfg_ctx;

	if (!vdev) {
		mlo_err("Invalid vdev");
		return QDF_STATUS_E_INVAL;
	}

	mlo_dev_ctx = vdev->mlo_dev_ctx;
	if (!mlo_dev_ctx) {
		mlo_err("MLO dev context is NULL");
		return QDF_STATUS_E_INVAL;
	}

	recfg_ctx = mlo_dev_ctx->link_recfg_ctx;
	if (!recfg_ctx) {
		mlo_err("Link recfg context is NULL");
		return QDF_STATUS_E_INVAL;
	}

	mlo_debug("Posting WLAN_LINK_RECFG_EV_SMD_ROAM_START to Link Recfg SM");

	/* Post event to Link Reconfiguration State Machine
	 * This will trigger:
	 * 1. smd_create_link_recfg_transition_list()
	 * 2. mlo_smd_link_recfg_assign_self_link_addr()
	 * 3. Transition to START state
	 * 4. Post WLAN_LINK_RECFG_EV_XMIT_REQ
	 */

	return QDF_STATUS_SUCCESS;
}

QDF_STATUS
smd_uhr_link_recfg_send_request_frame(
		struct mlo_link_recfg_context *recfg_ctx,
		struct mlo_link_recfg_state_req *req)
{
	struct wlan_action_frame_args args;
	struct link_recfg_tx_result tx_result;
	struct wlan_objmgr_vdev *vdev = NULL;
	struct wlan_objmgr_peer *peer = NULL;
	QDF_STATUS status, qdf_status;
	uint8_t vdev_id;

	if (!req) {
		mlo_err("Link recfg req is null");
		return QDF_STATUS_E_NULL_VALUE;
	}

	if (!recfg_ctx) {
		mlo_err("Link recfg ctx is null");
		return QDF_STATUS_E_NULL_VALUE;
	}

	args.category = ACTION_CATEGORY_PROTECTED_UHR;
	args.action = UHR_LINK_RECONFIG_REQUEST;
	args.arg1 = mlo_link_recfg_dialog_token(recfg_ctx, req);
	args.arg2 = UHR_TYPECODE_ST_PREPARATION;

	peer = wlan_objmgr_get_peer_by_mac(recfg_ctx->psoc,
					   (uint8_t *)&req->peer_mac,
					   WLAN_MLO_MGR_ID);
	if (!peer) {
		mlo_err("Peer is null");
		return QDF_STATUS_E_NULL_VALUE;
	}

	vdev = wlan_peer_get_vdev(peer);
	if (!vdev) {
		mlo_err("Vdev is null");
		wlan_objmgr_peer_release_ref(peer, WLAN_MLO_MGR_ID);
		return QDF_STATUS_E_NULL_VALUE;
	}
	vdev_id = wlan_vdev_get_id(vdev);
	status = lim_send_uhr_link_recfg_st_prep_req_frame(vdev_id,
						      (uint8_t *)&req->peer_mac,
						      &args, req);

	if (QDF_IS_STATUS_ERROR(status)) {
		mlo_err("Failed to send Link Reconfiguration action request frame");
		tx_result.status = status;
		mlo_link_recfg_sm_deliver_event(vdev->mlo_dev_ctx,
						WLAN_LINK_RECFG_SM_EV_XMIT_STATUS,
						sizeof(struct link_recfg_tx_result),
						&tx_result);
	} else {
		qdf_status = qdf_mc_timer_start(
				&recfg_ctx->link_recfg_rsp_timer,
				LINK_RECFG_RSP_TIMEOUT);
		if (QDF_IS_STATUS_ERROR(qdf_status))
			mlo_err("Failed to start the timer");
	}

	wlan_objmgr_peer_release_ref(peer, WLAN_MLO_MGR_ID);
	return status;
}

QDF_STATUS
smd_link_recfg_assign_self_link_addr(
			struct mlo_link_recfg_context *recfg_ctx,
			struct wlan_mlo_link_recfg_req *recfg_req,
			uint32_t del_link_set,
			uint32_t *first_del_link_set_no_common)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct mlo_link_info *link_info;
	uint8_t i;
	struct wlan_objmgr_psoc *psoc;
	uint8_t idx;
	uint32_t allocated_bitmap;
	struct wlan_mlo_link_recfg_bss_info *link_add;
	struct wlan_objmgr_vdev *vdev;

	if (!recfg_ctx || !recfg_req) {
		mlo_err("Invalid recfg context or req");
		return QDF_STATUS_E_INVAL;
	}

	if (!recfg_req->add_link_info.num_links)
		return QDF_STATUS_SUCCESS;

	mlo_dev_ctx = mlo_link_recfg_get_mlo_ctx(recfg_ctx);
	if (!mlo_dev_ctx) {
		mlo_err("mlo_ctx null");
		return QDF_STATUS_E_INVAL;
	}

	psoc = mlo_link_recfg_get_psoc(recfg_ctx);
	if (!psoc) {
		mlo_err("psoc null");
		return QDF_STATUS_E_INVAL;
	}

	link_add = &recfg_req->add_link_info.link[0];
	allocated_bitmap = 0;
	idx = 0;

	/* 1.select the idle vdev's self mac as first choice
	 * for target AP add link's self link addr.
	 * for example: L1 -> L2 L3
	 */
	for (i = 0; i < WLAN_MAX_ML_BSS_LINKS &&
	     idx < recfg_req->add_link_info.num_links; i++) {
		link_info = &mlo_dev_ctx->sta_ctx->links_info[i];
		if (allocated_bitmap & (1 << i))
			continue;

		if (link_info->vdev_id == WLAN_INVALID_VDEV_ID)
			continue;

		vdev = wlan_objmgr_get_vdev_by_id_from_psoc(
					psoc, link_info->vdev_id,
					WLAN_LINK_RECFG_ID);
		if (!vdev) {
			mlo_err("Invalid VDEV id %d", link_info->vdev_id);
			continue;
		}

		/* todo: add validate vdev mac with link info link_add */
		wlan_objmgr_vdev_release_ref(vdev, WLAN_LINK_RECFG_ID);

		if (qdf_is_macaddr_zero(&link_info->ap_link_addr) ||
		    link_info->link_id == WLAN_INVALID_LINK_ID) {
			link_add[idx].self_link_addr = link_info->link_addr;
			link_add[idx].vdev_id = link_info->vdev_id;
			mlo_debug("assign idle self link addr: " QDF_MAC_ADDR_FMT " for add link %d freq %d vdev %d",
				  QDF_MAC_ADDR_REF(link_info->link_addr.bytes),
				  link_add[idx].link_id,
				  link_add[idx].freq,
				  link_info->vdev_id);
			mlo_debug("old link id %d flag 0x%x on vdev %d ",
				  link_info->link_id,
				  (uint32_t)link_info->link_status_flags,
				  link_info->vdev_id);
			idx++;
			allocated_bitmap |= 1 << i;
		}
	}

	/* 2.For SMD roaming FW indicates preferred vdev id for added links,
	 *  validate it and use it.
	 * for example:
	 * L1 L2 -> L3 L4
	 * FW may indicate vdev id (from L1 or L2) for L3. here assign vdev
	 * to L3.
	 * The link will be deleted firstly. use the vdev on the deleted
	 * link to connect to new L3.
	 */
	for (i = 0; i < WLAN_MAX_ML_BSS_LINKS &&
	     idx < recfg_req->add_link_info.num_links; i++) {
		/* only checking the first one is enough */
		if (link_add[idx].vdev_id == WLAN_INVALID_VDEV_ID)
			break;
		link_info = &mlo_dev_ctx->sta_ctx->links_info[i];
		if (allocated_bitmap & (1 << i))
			continue;
		if (link_info->vdev_id != link_add[idx].vdev_id)
			continue;

		vdev = wlan_objmgr_get_vdev_by_id_from_psoc(
					psoc, link_info->vdev_id,
					WLAN_MLO_MGR_ID);
		if (!vdev) {
			mlo_err("Invalid VDEV id %d", link_info->vdev_id);
			continue;
		}

		if (!cm_is_vdev_connected(vdev)) {
			wlan_objmgr_vdev_release_ref(vdev, WLAN_MLO_MGR_ID);
			continue;
		}

		wlan_objmgr_vdev_release_ref(vdev, WLAN_MLO_MGR_ID);

		if (qdf_atomic_test_bit(
				LS_F_AP_REMOVAL_BIT,
				&link_info->link_status_flags) ||
		    del_link_set & (1 << link_info->link_id)) {
			link_add[idx].self_link_addr = link_info->link_addr;
			mlo_debug("fw preferred vdev %d for add link %d",
				  link_add[idx].vdev_id,
				  link_add[idx].link_id);
			mlo_debug("assign active self link addr: " QDF_MAC_ADDR_FMT " for add link %d freq %d vdev %d",
				  QDF_MAC_ADDR_REF(link_info->link_addr.bytes),
				  link_add[idx].link_id,
				  link_add[idx].freq,
				  link_info->vdev_id);
			mlo_debug("current AP link id %d flag 0x%x on vdev %d ",
				  link_info->link_id,
				  (uint32_t)link_info->link_status_flags,
				  link_info->vdev_id);
			if (!*first_del_link_set_no_common) {
				*first_del_link_set_no_common |=
					1 << link_info->link_id;
				mlo_debug("select link %d to delete first",
					  link_info->link_id);
			}

			idx++;
			allocated_bitmap |= 1 << i;
			break;
		}
	}

	return QDF_STATUS_SUCCESS;
}

QDF_STATUS
smd_create_link_recfg_transition_list(struct mlo_link_recfg_context *recfg_ctx,
				      struct wlan_mlo_link_recfg_req *recfg_req)
{
	uint32_t curr_link_set = 0, add_link_set = 0, del_link_set = 0;
	uint8_t curr_link_num = 0, add_link_num = 0, del_link_num = 0;
	uint8_t curr_standby_num = 0;
	uint32_t curr_standby_set = 0;
	uint32_t del_link_set_no_common = 0;
	struct mlo_link_recfg_state_tran *next = &recfg_ctx->sm.state_list[0];
	QDF_STATUS status;

	if (!recfg_req || !recfg_ctx) {
		mlo_err("Invalid recfg req or recfg ctx");
		return QDF_STATUS_E_INVAL;
	}

	mlo_link_recfg_get_link_bitmap(recfg_ctx,
				       recfg_req,
				       &add_link_set,
				       &add_link_num,
				       &del_link_set,
				       &del_link_num,
				       &curr_link_set,
				       &curr_link_num,
				       &curr_standby_set,
				       &curr_standby_num);

	/* alloc self mac for link add */
	status = smd_link_recfg_assign_self_link_addr(recfg_ctx,
						      recfg_req,
						      del_link_set,
						      &del_link_set_no_common);

	if (QDF_IS_STATUS_ERROR(status)) {
		mlo_err("fail to assign self link for added links status %d",
			status);
		return status;
	}

	/* create transition flow */
	recfg_ctx->sm.curr_state_idx = -1;
	recfg_ctx->macaddr_updating_vdev_id = WLAN_INVALID_VDEV_ID;
	qdf_zero_macaddr(&recfg_ctx->old_macaddr_updating_vdev);

	qdf_mem_zero(&recfg_ctx->sm.state_list[0],
		     sizeof(recfg_ctx->sm.state_list[0]) *
		     MAX_RECFG_TRANSITION);
	recfg_req->recfg_type = link_recfg_undefined;
	recfg_req->join_pending_vdev_id = WLAN_INVALID_VDEV_ID;

	if (recfg_req->add_link_info.num_links &&
	    recfg_req->st_prep_link_recfg) {
		/* Send ST Prep Request to add Target AP links */
		mlo_debug("Send ST Prep Request to add Target AP links");
		recfg_req->recfg_type = link_recfg_st_prep_add_link;
		next->state = WLAN_LINK_RECFG_S_XMIT_REQ;
		next->event = WLAN_LINK_RECFG_SM_EV_XMIT_REQ;
		next->req.del_link_info = recfg_req->del_link_info;
		next->req.add_link_info = recfg_req->add_link_info;
		next->req.recfg_type = recfg_req->recfg_type;
		status =
		smd_roam_link_recfg_set_tx_link_addr(recfg_ctx,
						     recfg_req,
						     &next->req,
						     curr_link_set);
		if (QDF_IS_STATUS_ERROR(status)) {
			mlo_err("fail to set tx frame link addr status %d",
				status);
			return status;
		}
		next->abort_handler = NULL;
		next++;
		next->state = WLAN_LINK_RECFG_S_DEL_LINK;
		next->event = WLAN_LINK_RECFG_SM_EV_DEL_LINK;
		next->req.del_link_info = recfg_req->del_link_info;
		next->abort_handler = NULL;
		next++;
		next->state = WLAN_LINK_RECFG_S_ADD_LINK;
		next->event = WLAN_LINK_RECFG_SM_EV_SMD_ADD_LINK;
		next->req.add_link_info = recfg_req->add_link_info;
		next->abort_handler = NULL;
		next++;
		next->state = WLAN_LINK_RECFG_S_WAIT;
		next->event = WLAN_LINK_RECFG_SM_EV_WAIT_SMD_EXEC;
		next->req.del_link_info = recfg_req->del_link_info;
		next->req.add_link_info = recfg_req->add_link_info;
		next->abort_handler = NULL;
	} else if ((recfg_req->add_link_info.num_links ||
		recfg_req->del_link_info.num_links) &&
		recfg_req->st_exec_link_recfg) {
		/* Handle ST Exec Request to add Target AP links */
		mlo_debug("Handle ST Exec Request to add target AP link/del current AP link");
		recfg_req->recfg_type = link_recfg_st_exec_add_link;
		next->req.recfg_type = recfg_req->recfg_type;
		next->state = WLAN_LINK_RECFG_S_DEL_LINK;
		next->event = WLAN_LINK_RECFG_SM_EV_DEL_LINK;
		next->req.del_link_info = recfg_req->del_link_info;
		next->abort_handler = NULL;
		next++;
		next->state = WLAN_LINK_RECFG_S_ADD_LINK;
		next->event = WLAN_LINK_RECFG_SM_EV_SMD_ADD_LINK;
		next->req.add_link_info = recfg_req->add_link_info;
		next->req.del_link_info = recfg_req->del_link_info;
		next->abort_handler = NULL;
		next++;
		next->state = WLAN_LINK_RECFG_S_COMPLETED;
		next->event = WLAN_LINK_RECFG_SM_EV_SMD_ROAM_COMPLETED;
		next->req.del_link_info = recfg_req->del_link_info;
		next->req.add_link_info = recfg_req->add_link_info;
		next->abort_handler = NULL;
	}

	status = mlo_link_recfg_tranistion_to_next_state(recfg_ctx);
	if (QDF_IS_STATUS_ERROR(status))
		mlo_err("start trans failed status %d", status);

	return QDF_STATUS_SUCCESS;
}

bool
smd_roam_in_progress(struct mlo_link_recfg_context *recfg_ctx)
{
	if (!recfg_ctx) {
		mlo_err("Invalid recfg context");
		return false;
	}

	return recfg_ctx->smd_roam_in_progress;
}

bool
smd_roam_exec_in_progress(struct wlan_mlo_dev_context *mlo_dev_ctx)
{
	struct mlo_link_recfg_context *recfg_ctx;

	if (!mlo_dev_ctx || !mlo_dev_ctx->link_recfg_ctx)
		return false;

	recfg_ctx = mlo_dev_ctx->link_recfg_ctx;

	return recfg_ctx->st_exec_in_progress;
}

struct wlan_mlo_link_recfg_bss_info *
smd_find_first_accepted_link(struct mlo_link_recfg_context *recfg_ctx,
			     struct mlo_link_recfg_state_tran *tran)
{
	uint8_t i;
	struct wlan_mlo_link_recfg_bss_info *link;

	if (!recfg_ctx || !tran) {
		mlo_err("Invalid parameters: recfg_ctx=%pK tran=%pK",
			recfg_ctx, tran);
		return NULL;
	}

	for (i = 0; i < tran->req.add_link_info.num_links &&
	     i < WLAN_MAX_ML_BSS_LINKS; i++) {
		link = &tran->req.add_link_info.link[i];

		mlo_debug("Link[%u]: link_id=%u status_code=%u",
			  i, link->link_id, link->status_code);

		if (link->status_code == STATUS_SUCCESS) {
			mlo_debug("Found accepted link at index %u: link_id=%u freq=%u vdev_id=%u",
				  i, link->link_id, link->freq, link->vdev_id);
			return link;
		}
	}

	mlo_debug("No accepted links found (all links rejected)");
	return NULL;
}

static QDF_STATUS
smd_update_del_link_info(
	struct mlo_link_recfg_context *recfg_ctx,
	struct wlan_mlo_link_recfg_bss_info *bss_info,
	struct mlo_link_recfg_state_tran *tran)
{
	struct wlan_objmgr_vdev *vdev = NULL;
	struct wlan_objmgr_psoc *psoc = NULL;
	QDF_STATUS status = QDF_STATUS_SUCCESS;
	struct mlo_link_info *link_info = NULL;
	struct wlan_mlo_link_recfg_bss_info matched_link;
	uint8_t i;

	if (!bss_info || !tran || !recfg_ctx)
		return QDF_STATUS_E_NULL_VALUE;

	psoc = mlo_link_recfg_get_psoc(recfg_ctx);
	if (!psoc) {
		mlo_err("psoc is null");
		return QDF_STATUS_E_INVAL;
	}

	if (!tran->req.del_link_info.num_links) {
		mlo_err("Delete num links is 0");
		return QDF_STATUS_E_INVAL;
	}

	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(
				psoc, recfg_ctx->curr_recfg_req.vdev_id,
				WLAN_LINK_RECFG_ID);
	if (!vdev) {
		mlo_err("Invalid link recfg VDEV %d",
			recfg_ctx->curr_recfg_req.vdev_id);
		status = QDF_STATUS_E_INVAL;
		goto end;
	}

	for (i = 0; i < tran->req.del_link_info.num_links &&
	     i < WLAN_MAX_ML_BSS_LINKS; i++) {
		if (tran->req.del_link_info.link[i].vdev_id != bss_info->vdev_id)
			continue;

		matched_link = tran->req.del_link_info.link[i];
		qdf_mem_zero(&tran->req.del_link_info,
			     sizeof(struct wlan_mlo_link_recfg_info));
		tran->req.del_link_info.link[0] = matched_link;
		tran->req.del_link_info.num_links = 1;
		mlo_debug("Update del link info vdev id %d at index %d",
			  matched_link.vdev_id, i);
		goto end;
	}

	/* No match found in del_link_info — fall back to current AP link lookup */
	link_info = smd_find_current_ap_link_info(vdev, bss_info->vdev_id);
	if (!link_info) {
		mlo_err("Link info not found for vdev_id %d", bss_info->vdev_id);
		status = QDF_STATUS_E_INVAL;
		goto end;
	}

	qdf_mem_zero(&tran->req.del_link_info,
		     sizeof(struct wlan_mlo_link_recfg_info));
	tran->req.del_link_info.link[0].vdev_id = link_info->vdev_id;
	qdf_copy_macaddr(&tran->req.del_link_info.link[0].self_link_addr,
			 &link_info->link_addr);
	qdf_copy_macaddr(&tran->req.del_link_info.link[0].ap_link_addr,
			 &link_info->ap_link_addr);
	tran->req.del_link_info.num_links = 1;
	mlo_debug("Update del link info vdev id %d (fallback)", link_info->vdev_id);

end:
	if (vdev)
		wlan_objmgr_vdev_release_ref(vdev, WLAN_LINK_RECFG_ID);
	return status;
}

QDF_STATUS
smd_st_prep_response_received(struct mlo_link_recfg_context *recfg_ctx,
			      struct mlo_link_recfg_state_tran *tran)
{
	QDF_STATUS status = QDF_STATUS_SUCCESS;
	struct wlan_mlo_link_recfg_bss_info *bss_info = NULL;

	if (!recfg_ctx || !tran)
		return QDF_STATUS_E_INVAL;

	mlo_debug("RX response success");

	/* propagate link add status code from ap to "add link" state
	 * request.
	 */
	mlo_link_recfg_update_state_req_from_rsp(recfg_ctx, tran);

	status = smd_add_prepared_target_links_in_smd_ctx(recfg_ctx,
							  &tran->req);
	if (QDF_IS_STATUS_ERROR(status)) {
		mlo_err("SMD prepared target update failed");
		return status;
	}

	bss_info = smd_find_first_accepted_link(recfg_ctx, tran);
	if (bss_info)
		smd_update_del_link_info(recfg_ctx, bss_info, tran);
	/* Same PTK case, TODO add check for same ptk vs diff ptk */
	smd_roam_store_key(recfg_ctx, &tran->req);

	return status;
}

void
smd_link_recfg_complete(struct mlo_link_recfg_context *recfg_ctx,
			bool success)
{
	QDF_STATUS status;
	struct wlan_objmgr_psoc *psoc;
	struct mlo_link_recfg_state_tran *tran;

	if (!recfg_ctx) {
		mlo_err("recfg_ctx is null");
		return;
	}

	psoc = mlo_link_recfg_get_psoc(recfg_ctx);
	if (!psoc) {
		mlo_err("psoc is null");
		return;
	}

	if (!success) {
		mlo_err("SMD: link recfg completed with failure, aborting");
		if (recfg_ctx->curr_recfg_req.st_prep_link_recfg) {
			tran = mlo_link_recfg_get_curr_tran_req(recfg_ctx);
			if (tran)
				smd_send_roam_start_status_cmd(recfg_ctx,
							       &tran->req,
							       SMD_PREP_STATUS_VDEV_REPURPOSE_FAIL);
		}
		smd_roam_cleanup_ies(recfg_ctx);
		return;
	}

	/* All link switches complete — deliver ROAM_DONE to assoc vdev */
	status = smd_exec_complete(psoc, recfg_ctx);
	if (QDF_IS_STATUS_ERROR(status))
		mlo_err("SMD: smd_exec_complete failed: %d", status);

	smd_roam_cleanup_ies(recfg_ctx);
}

void
smd_link_recfg_del_link_completed(struct mlo_link_recfg_context *recfg_ctx)
{
	/* handle link del completed */

	/* transition to next state */
	mlo_link_recfg_tranistion_to_next_state(recfg_ctx);
}

QDF_STATUS
smd_host_link_switch_validate_request(struct wlan_objmgr_vdev *vdev,
				      struct wlan_mlo_link_switch_req *req)
{
	if (req->reason == MLO_LINK_SWITCH_REASON_SMD_ROAM_REMOVE_LINK) {
		/* Disconnect-only: new_ieee_link_id is intentionally INVALID.
		 * Only curr_ieee_link_id needs to be valid.
		 */
		if (req->curr_ieee_link_id == WLAN_INVALID_LINK_ID) {
			mlo_err("REMOVE_LINK: invalid curr link id %d",
				req->curr_ieee_link_id);
			return QDF_STATUS_E_INVAL;
		}
		mlo_debug("REMOVE_LINK: curr_link_id %d, skipping new_link validation",
			  req->curr_ieee_link_id);
		return QDF_STATUS_SUCCESS;
	}

	if (req->reason == MLO_LINK_SWITCH_REASON_SMD_ROAM_ADD_LINK &&
	    req->curr_ieee_link_id == WLAN_INVALID_LINK_ID) {
		/* Idle vdev: curr_link_id is invalid (no current link) */
		if (req->new_ieee_link_id >= WLAN_MAX_ML_BSS_LINKS) {
			mlo_err("SMD_ADD_LINK: invalid new link id %d",
				req->new_ieee_link_id);
			return QDF_STATUS_E_INVAL;
		}
		mlo_debug("SMD_ADD_LINK idle vdev: new_link_id %d",
			  req->new_ieee_link_id);
		return QDF_STATUS_SUCCESS;
	}

	if (req->curr_ieee_link_id >= WLAN_INVALID_LINK_ID ||
	    req->new_ieee_link_id >= WLAN_INVALID_LINK_ID) {
		mlo_err("Invalid link params, curr link id %d, new link id %d",
			req->curr_ieee_link_id, req->new_ieee_link_id);
		return QDF_STATUS_E_INVAL;
	}

	if (wlan_vdev_get_id(vdev) != req->vdev_id) {
		mlo_err("Invalid vdev params, curr id %d, req id %d",
			wlan_vdev_get_id(vdev), req->vdev_id);
		return QDF_STATUS_E_INVAL;
	}

	if (mlo_mgr_is_link_switch_in_progress(vdev)) {
		mlo_err("Link switch already in progress");
		return QDF_STATUS_E_INVAL;
	}

	if (!req->smd_lnk_sw_trigger) {
		mlo_err("Not SMD Link switch trigger");
		return QDF_STATUS_E_INVAL;
	}

	return QDF_STATUS_SUCCESS;
}

struct mlo_link_info *
smd_get_prepared_ap_link_info(struct wlan_objmgr_vdev *vdev,
			      struct qdf_mac_addr *ap_link_addr)
{
	struct smd_context *smd_ctx;
	struct wlan_mlo_sta *target_bss_ctx;
	struct mlo_link_info *link_info;
	uint8_t target_iter;
	uint8_t link_info_iter;

	if (!vdev || !vdev->mlo_dev_ctx || !ap_link_addr ||
	    qdf_is_macaddr_zero(ap_link_addr))
		return NULL;

	smd_ctx = vdev->mlo_dev_ctx->smd_ctx;
	if (!smd_ctx) {
		mlo_err("SMD: smd_ctx is NULL");
		return NULL;
	}

	mlo_debug("SMD: searching " QDF_MAC_ADDR_FMT " in %u prepared targets",
		  QDF_MAC_ADDR_REF(ap_link_addr->bytes),
		  smd_ctx->num_prepared);

	for (target_iter = 0;
	     target_iter < smd_ctx->num_prepared &&
	     target_iter < SMD_MAX_PREPARED_TARGETS;
	     target_iter++) {
		if (!smd_ctx->prepared_targets[target_iter].prepared)
			continue;

		target_bss_ctx =
			smd_ctx->prepared_targets[target_iter].target_bss_ctx;
		if (!target_bss_ctx)
			continue;

		for (link_info_iter = 0; link_info_iter < WLAN_MAX_ML_BSS_LINKS;
		     link_info_iter++) {
			link_info = &target_bss_ctx->links_info[link_info_iter];
			mlo_debug("SMD: target[%u] link[%u] ap_addr " QDF_MAC_ADDR_FMT,
				  target_iter, link_info_iter,
				  QDF_MAC_ADDR_REF(link_info->ap_link_addr.bytes));
			if (qdf_is_macaddr_equal(&link_info->ap_link_addr,
						 ap_link_addr))
				return link_info;
		}
	}

	return NULL;
}

QDF_STATUS
smd_roam_prep_complete(struct mlo_link_recfg_context *recfg_ctx,
			   struct mlo_link_recfg_state_req *req)
{
	return smd_send_roam_start_status_cmd(recfg_ctx, req,
					      SMD_PREP_STATUS_SUCCESS);
}
struct mlo_link_info *
smd_get_prep_ap_link_info(struct wlan_objmgr_vdev *vdev,
			  struct wlan_mlo_link_switch_req *req)
{
	return smd_get_prepared_ap_link_info(vdev, &req->tgt_ap_link_addr);
}

QDF_STATUS smd_fw_roam_sync(struct wlan_objmgr_vdev *vdev,
			    struct roam_offload_synch_ind *sync_ind)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct mlo_link_recfg_context *recfg_ctx;
	QDF_STATUS status;

	if (!vdev || !sync_ind) {
		mlo_err("SMD: Invalid parameters");
		return QDF_STATUS_E_NULL_VALUE;
	}

	if (!sync_ind->num_vdev_repurpose_req) {
		mlo_debug("SMD: No vdev repurpose requests");
		return QDF_STATUS_SUCCESS;
	}

	mlo_dev_ctx = vdev->mlo_dev_ctx;
	if (!mlo_dev_ctx) {
		mlo_err("SMD: MLO dev context is NULL");
		return QDF_STATUS_E_INVAL;
	}

	recfg_ctx = mlo_dev_ctx->link_recfg_ctx;
	if (!recfg_ctx) {
		mlo_err("SMD: Link recfg context is NULL");
		return QDF_STATUS_E_INVAL;
	}

	/* Deep copy sync_ind into recfg_ctx — sync_ind is freed by the
	 * WMI event handler after roam_sync_event() returns.
	 */
	if (QDF_IS_STATUS_ERROR(smd_alloc_copy_roam_sync_ind(recfg_ctx,
							     sync_ind))) {
		mlo_err("SMD: failed to copy sync_ind");
		return QDF_STATUS_E_NOMEM;
	}

	recfg_ctx->smd_roam_in_progress = true;
	recfg_ctx->st_exec_in_progress = true;
	recfg_ctx->num_vdev_repurpose_req = sync_ind->num_vdev_repurpose_req;
	qdf_mem_copy(&recfg_ctx->vdev_repurpose_req,
		     &sync_ind->vdev_repurpose_req,
		     sizeof(struct smd_vdev_repurpose_req) *
		     recfg_ctx->num_vdev_repurpose_req);

	mlo_debug("SMD: Data copied, num_vdev_repurpose_req=%u, returning E_PENDING",
		  recfg_ctx->num_vdev_repurpose_req);

	status = smd_validate_repurpose_smd_addr(recfg_ctx, mlo_dev_ctx);
	if (QDF_IS_STATUS_ERROR(status)) {
		mlo_err("SMD: SMD address does not match");
		smd_free_cached_sync_ind(recfg_ctx);
		return QDF_STATUS_E_INVAL;
	}

	/* recfg_req will be built from recfg_ctx in smd_trigger_link_recfg_sm()
	 * after the CM lock is released by the caller.
	 */
	return QDF_STATUS_E_PENDING;
}

QDF_STATUS
smd_add_prepared_target_links_in_smd_ctx(
					struct mlo_link_recfg_context *recfg_ctx,
					struct mlo_link_recfg_state_req *req)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct smd_context *smd_ctx;
	struct wlan_mlo_sta *target_bss_ctx = NULL;
	struct mlo_link_info *link_info = NULL;
	struct wlan_objmgr_vdev *vdev = NULL;
	struct wlan_objmgr_psoc *psoc = NULL;
	struct wlan_mlo_link_recfg_bss_info *add_link;
	struct wlan_objmgr_pdev *pdev = NULL;
	QDF_STATUS status = QDF_STATUS_SUCCESS;
	struct scan_cache_entry *scan_entry;
	struct wlan_channel channel;
	uint8_t i;
	uint8_t link_idx;

	if (!req || !recfg_ctx) {
		mlo_err("Invalid parameters");
		return QDF_STATUS_E_NULL_VALUE;
	}

	if (!req->add_link_info.num_links) {
		mlo_debug("Add links is 0");
		return QDF_STATUS_SUCCESS;
	}

	psoc = mlo_link_recfg_get_psoc(recfg_ctx);
	if (!psoc) {
		mlo_err("psoc is null");
		return QDF_STATUS_E_INVAL;
	}

	pdev = wlan_objmgr_get_pdev_by_id(psoc, 0, WLAN_LINK_RECFG_ID);
	if (!pdev) {
		mlo_err("Invalid pdev");
		status = QDF_STATUS_E_INVAL;
		goto end;
	}

	mlo_dev_ctx = recfg_ctx->ml_dev;
	if (!mlo_dev_ctx) {
		mlo_err("MLO dev context is NULL");
		status = QDF_STATUS_E_INVAL;
		goto end;
	}

	smd_ctx = mlo_dev_ctx->smd_ctx;
	if (!smd_ctx) {
		mlo_err("SMD context is NULL");
		status = QDF_STATUS_E_INVAL;
		goto end;
	}

	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(
				psoc, recfg_ctx->curr_recfg_req.vdev_id,
				WLAN_LINK_RECFG_ID);
	if (!vdev) {
		mlo_err("Invalid link recfg VDEV %d",
			recfg_ctx->curr_recfg_req.vdev_id);
		status = QDF_STATUS_E_INVAL;
		goto end;
	}

	/* Check if we have space in prepared_targets array */
	if (smd_ctx->num_prepared >= SMD_MAX_PREPARED_TARGETS) {
		mlo_err("SMD prepared targets array is full: %d",
			smd_ctx->num_prepared);
		status = QDF_STATUS_E_NOMEM;
		goto end;
	}

	/* Allocate target BSS context if not already present */
	if (!smd_ctx->prepared_targets[smd_ctx->num_prepared].target_bss_ctx) {
		target_bss_ctx = qdf_mem_malloc(sizeof(struct wlan_mlo_sta));
		if (!target_bss_ctx) {
			mlo_err("Failed to allocate target BSS context");
			status = QDF_STATUS_E_NOMEM;
			goto end;
		}
		smd_ctx->prepared_targets[smd_ctx->num_prepared].target_bss_ctx =
			target_bss_ctx;
	} else {
		target_bss_ctx =
			smd_ctx->prepared_targets[smd_ctx->num_prepared].target_bss_ctx;
	}

	/* Update added links with status code SUCCESS in target_bss_ctx->links_info */
	for (i = 0, link_idx = 0; i < req->add_link_info.num_links &&
	     i < WLAN_MAX_ML_BSS_LINKS && link_idx < WLAN_MAX_ML_BSS_LINKS;
	     i++) {
		add_link = &req->add_link_info.link[i];

		/* Check link add status_code before update link info */
		if (add_link->status_code != STATUS_SUCCESS) {
			mlo_debug("link id %d add with failure status code %d",
				  add_link->link_id,
				  add_link->status_code);
			continue;
		}

		/* Get scan entry for the added link */
		scan_entry = wlan_scan_get_entry_by_bssid(pdev,
							  &add_link->ap_link_addr);
		if (!scan_entry) {
			mlo_debug("add link " QDF_MAC_ADDR_FMT " scan entry not found",
				  QDF_MAC_ADDR_REF(add_link->ap_link_addr.bytes));
			continue;
		}

		/* Prepare channel information */
		qdf_mem_zero(&channel, sizeof(channel));
		channel.ch_freq = scan_entry->channel.chan_freq;
		channel.ch_ieee = wlan_reg_freq_to_chan(pdev, channel.ch_freq);
		channel.ch_phymode = scan_entry->phy_mode;
		channel.ch_cfreq1 = scan_entry->channel.cfreq0;
		channel.ch_cfreq2 = scan_entry->channel.cfreq1;
		channel.ch_width =
			wlan_mlme_get_ch_width_from_phymode(scan_entry->phy_mode);

		if (channel.ch_width == CH_WIDTH_20MHZ)
			channel.ch_cfreq1 = channel.ch_freq;

		/* Update link info in target BSS context using separate index */
		link_info = &target_bss_ctx->links_info[link_idx];

		link_info->link_chan_info =
			qdf_mem_malloc(sizeof(*link_info->link_chan_info));
		if (!link_info->link_chan_info) {
			mlo_err("link_chan_info alloc failed for link %d",
				link_idx);
			util_scan_free_cache_entry(scan_entry);
			status = QDF_STATUS_E_NOMEM;
			goto end;
		}
		qdf_mem_copy(link_info->link_chan_info, &channel,
			     sizeof(struct wlan_channel));

		qdf_copy_macaddr(&link_info->ap_link_addr,
				 &add_link->ap_link_addr);
		qdf_copy_macaddr(&link_info->link_addr,
				 &add_link->self_link_addr);

		/* Links beyond the 2 active vdevs (0 and 1) are standby links —
		 * they are accepted by the AP but have no vdev assigned yet.
		 * Mark them WLAN_INVALID_VDEV_ID so the host treats them as
		 * standby links (no TX path, no CM state) until a link switch
		 * activates them. smd_roam_update_sta_ctx_links() propagates
		 * these entries into sta_ctx->links_info[] post-roam.
		 */
		link_info->vdev_id = (link_idx < 2) ? add_link->vdev_id :
						      WLAN_INVALID_VDEV_ID;
		link_info->link_id = add_link->link_id;
		link_info->chan_freq = add_link->freq;
		link_info->link_status_code = STATUS_SUCCESS;
		link_info->cnx_tx_nss = add_link->cap_tx_nss;
		link_info->cnx_rx_nss = add_link->cap_rx_nss;
		link_info->is_link_active = false;
		link_info->link_status_flags = 0;

		mlo_debug("Updated target BSS link[%d]: link_id=%d vdev_id=%d freq=%d "
			  "AP=" QDF_MAC_ADDR_FMT " Self=" QDF_MAC_ADDR_FMT " NSS=%dx%d",
			  link_idx, link_info->link_id, link_info->vdev_id,
			  link_info->chan_freq,
			  QDF_MAC_ADDR_REF(link_info->ap_link_addr.bytes),
			  QDF_MAC_ADDR_REF(link_info->link_addr.bytes),
			  link_info->cnx_tx_nss, link_info->cnx_rx_nss);

		util_scan_free_cache_entry(scan_entry);
		link_idx++;
	}

	/* Update prepared target information */
	smd_ctx->prepared_targets[smd_ctx->num_prepared].prepared = true;
	smd_ctx->prepared_targets[smd_ctx->num_prepared].prep_timestamp =
		qdf_get_system_timestamp();

	/* Increment num_prepared counter */
	smd_ctx->num_prepared++;
	mlo_debug("SMD context updated: num_prepared=%d",
		  smd_ctx->num_prepared);

end:
	if (QDF_IS_STATUS_ERROR(status)) {
		/* Free allocated target_bss_ctx on error */
		if (target_bss_ctx &&
		    smd_ctx->prepared_targets[smd_ctx->num_prepared].target_bss_ctx == target_bss_ctx) {
			uint8_t k;

			for (k = 0; k < WLAN_MAX_ML_BSS_LINKS; k++) {
				qdf_mem_free(target_bss_ctx->links_info[k].link_chan_info);
				target_bss_ctx->links_info[k].link_chan_info = NULL;
			}
			qdf_mem_free(target_bss_ctx);
			smd_ctx->prepared_targets[smd_ctx->num_prepared].target_bss_ctx = NULL;
		}
	}

	if (vdev)
		wlan_objmgr_vdev_release_ref(vdev, WLAN_LINK_RECFG_ID);
	if (pdev)
		wlan_objmgr_pdev_release_ref(pdev, WLAN_LINK_RECFG_ID);
	return status;
}

QDF_STATUS
smd_roam_link_recfg_set_tx_link_addr(
			struct mlo_link_recfg_context *recfg_ctx,
			struct wlan_mlo_link_recfg_req *recfg_req,
			struct mlo_link_recfg_state_req *req,
			uint32_t candidate_link_set)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct mlo_link_info *link_info;
	uint8_t i;
	QDF_STATUS status = QDF_STATUS_SUCCESS;
	struct wlan_objmgr_pdev *pdev;
	struct wlan_objmgr_psoc *psoc;
	struct qdf_mac_addr standby_link_peer_mac;

	qdf_zero_macaddr(&standby_link_peer_mac);

	/* decide which link will be used to send action frame */
	mlo_dev_ctx = mlo_link_recfg_get_mlo_ctx(recfg_ctx);
	if (!mlo_dev_ctx) {
		mlo_err("mlo_ctx null");
		return QDF_STATUS_E_INVAL;
	}

	psoc = mlo_link_recfg_get_psoc(recfg_ctx);
	if (!psoc) {
		mlo_err("psoc null");
		return QDF_STATUS_E_INVAL;
	}

	pdev = wlan_objmgr_get_pdev_by_id(psoc, 0, WLAN_LINK_RECFG_ID);
	if (!pdev) {
		mlo_err("Invalid pdev");
		return QDF_STATUS_E_INVAL;
	}

	if (!smd_roam_in_progress(recfg_ctx)) {
		mlo_err("SMD roaming is not in progress");
		wlan_objmgr_pdev_release_ref(pdev, WLAN_LINK_RECFG_ID);
		return QDF_STATUS_E_INVAL;
	}

	for (i = 0; i < WLAN_MAX_ML_BSS_LINKS; i++) {
		link_info = &mlo_dev_ctx->sta_ctx->links_info[i];
		if (qdf_is_macaddr_zero(&link_info->ap_link_addr))
			continue;

		mlo_err("[DBG] vdev id %d link id %d, candidate_link_set %d",
			link_info->vdev_id, link_info->link_id, candidate_link_set);

		if (link_info->link_id == WLAN_INVALID_LINK_ID)
			continue;

		if (qdf_atomic_test_bit(
				LS_F_AP_REMOVAL_BIT,
				&link_info->link_status_flags)) {
			mlo_debug("skip ap link addr: " QDF_MAC_ADDR_FMT " link flag 0x%x",
				  QDF_MAC_ADDR_REF(req->peer_mac.bytes),
				  (uint32_t)link_info->link_status_flags);
			continue;
		}

		if (link_info->vdev_id == WLAN_INVALID_VDEV_ID) {
			if ((1 << link_info->link_id) & candidate_link_set)
				standby_link_peer_mac =
					link_info->ap_link_addr;
			continue;
		}

		if (!cm_is_vdevid_roaming(pdev, link_info->vdev_id)) {
			mlo_debug("vdevid is not roaming %d", link_info->vdev_id);
			continue;
		}

		if ((1 << link_info->link_id) & candidate_link_set) {
			req->peer_mac = link_info->ap_link_addr;
			mlo_debug("selected tx ap link addr: " QDF_MAC_ADDR_FMT "",
				  QDF_MAC_ADDR_REF(req->peer_mac.bytes));
			break;
		}
	}

	if (i == WLAN_MAX_ML_BSS_LINKS) {
		if (!qdf_is_macaddr_zero(&standby_link_peer_mac)) {
			req->peer_mac = standby_link_peer_mac;
			mlo_debug("selected tx ap link addr: " QDF_MAC_ADDR_FMT " - standby",
				  QDF_MAC_ADDR_REF(req->peer_mac.bytes));
		} else {
			status = QDF_STATUS_E_INVAL;
			mlo_debug("no found valid peer mac");
		}
	}

	if (pdev)
		wlan_objmgr_pdev_release_ref(pdev, WLAN_LINK_RECFG_ID);

	return status;
}

bool
smd_is_roaming_in_progress(struct wlan_objmgr_vdev *vdev)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx;

	if (!vdev)
		return false;

	mlo_dev_ctx = vdev->mlo_dev_ctx;
	if (!mlo_dev_ctx || !mlo_dev_ctx->link_recfg_ctx)
		return false;

	return smd_roam_in_progress(mlo_dev_ctx->link_recfg_ctx);
}

void
smd_roam_link_recfg_abort(struct wlan_objmgr_vdev *vdev)
{
	struct mlo_link_recfg_context *recfg_ctx;

	if (!vdev || !vdev->mlo_dev_ctx || !vdev->mlo_dev_ctx->link_recfg_ctx)
		return;

	recfg_ctx = vdev->mlo_dev_ctx->link_recfg_ctx;

	if (!smd_roam_in_progress(recfg_ctx))
		return;

	if (wlan_vdev_mlme_is_mlo_vdev(vdev) &&
	    mlo_is_link_recfg_in_progress(vdev)) {
		mlo_link_recfg_sm_deliver_event(
			recfg_ctx->ml_dev,
			WLAN_LINK_RECFG_SM_EV_DISCONNECT_IND,
			0, NULL);
	}
}

QDF_STATUS smd_trigger_link_recfg_sm(struct wlan_objmgr_vdev *vdev)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct mlo_link_recfg_context *recfg_ctx;
	struct smd_context *smd_ctx;
	struct wlan_objmgr_psoc *psoc;
	struct wlan_mlo_link_recfg_req recfg_req = {0};
	QDF_STATUS status;

	if (!vdev)
		return QDF_STATUS_E_NULL_VALUE;

	mlo_dev_ctx = vdev->mlo_dev_ctx;
	if (!mlo_dev_ctx || !mlo_dev_ctx->link_recfg_ctx) {
		mlo_err("SMD: mlo_dev_ctx or recfg_ctx is NULL");
		return QDF_STATUS_E_INVAL;
	}

	recfg_ctx = mlo_dev_ctx->link_recfg_ctx;
	smd_ctx = mlo_dev_ctx->smd_ctx;
	if (!smd_ctx) {
		mlo_err("SMD: smd_ctx is NULL");
		return QDF_STATUS_E_INVAL;
	}

	psoc = wlan_vdev_get_psoc(vdev);
	if (!psoc) {
		mlo_err("SMD: psoc is NULL");
		return QDF_STATUS_E_INVAL;
	}

	/* Build recfg_req from the vdev_repurpose_req already stored in
	 * recfg_ctx by smd_fw_roam_sync().
	 */
	if (smd_handle_multi_to_single_link_roaming(vdev, smd_ctx, recfg_ctx,
						    &recfg_req)) {
		mlo_debug("SMD: Multi-to-single link roaming scenario");
	} else {
		status = smd_handle_multi_to_multi_link_roaming(vdev, smd_ctx,
								recfg_ctx,
								&recfg_req);
		if (QDF_IS_STATUS_ERROR(status)) {
			mlo_err("SMD: multi-link roaming setup failed %d", status);
			goto fail;
		}
	}

	recfg_req.vdev_id = wlan_vdev_get_id(vdev);
	recfg_req.is_user_req = false;
	recfg_req.is_fw_ind_received = true;
	recfg_req.st_prep_link_recfg = false;
	recfg_req.st_exec_link_recfg = true;

	status = smd_update_channel_freq(psoc, &recfg_req);
	if (QDF_IS_STATUS_ERROR(status)) {
		mlo_err("SMD: Failed to find link freq %d", status);
		goto fail;
	}

	mlo_debug("SMD: Triggering Link Reconfiguration SM (CM lock released)");
	return mlo_link_recfg_sm_deliver_event(
				mlo_dev_ctx,
				WLAN_LINK_RECFG_SM_EV_SMD_ROAM_START,
				sizeof(recfg_req), &recfg_req);

fail:
	recfg_ctx->smd_roam_in_progress = false;
	recfg_ctx->st_exec_in_progress = false;
	smd_free_cached_sync_ind(recfg_ctx);
	return status;
}

bool smd_handle_cm_roam_sync_pending(struct wlan_objmgr_vdev *vdev)
{
	QDF_STATUS trigger_status;
	struct wlan_mlo_dev_context *mlo_dev_ctx;

	if (!vdev)
		return false;

	mlo_dev_ctx = vdev->mlo_dev_ctx;
	if (!mlo_dev_ctx)
		return false;

	if (!smd_is_roaming_in_progress(vdev) ||
	    !smd_roam_exec_in_progress(mlo_dev_ctx))
		return false;

	trigger_status = smd_trigger_link_recfg_sm(vdev);
	if (QDF_IS_STATUS_ERROR(trigger_status)) {
		mlo_err("SMD: trigger_link_recfg_sm failed: %d",
			trigger_status);
		return false;
	}

	return true;
}

/**
 * smd_find_new_assoc_vdev() - Find the vdev to receive EV_SMD_EXEC_COMPLETE.
 * @mlo_dev_ctx: MLO device context
 * @recfg_ctx: link reconfig context
 * @psoc: psoc pointer
 *
 * CASE 1 (ML→ML / SL→ML / ML→SL):
 *   curr_recfg_req.vdev_id reconnected as assoc vdev and entered
 *   CONNECTED/SMD_ROAM_SYNC via T4. Return it directly.
 *
 * CASE 2 (SL→SL):
 *   curr_recfg_req.vdev_id is in INIT (disconnected, cleaned up).
 *   The new assoc vdev is the OTHER vdev — it entered CONNECTED/SMD_ROAM_SYNC
 *   at T4 during Phase 3 (idle-vdev direct connect with set_mlo_vdev only).
 *   Search all MLD vdevs for one in CONNECTED/SMD_ROAM_SYNC.
 *
 * Return: vdev pointer (caller must release ref), NULL if not found.
 */
static struct wlan_objmgr_vdev *
smd_find_new_assoc_vdev(struct wlan_mlo_dev_context *mlo_dev_ctx,
			struct mlo_link_recfg_context *recfg_ctx,
			struct wlan_objmgr_psoc *psoc)
{
	struct wlan_objmgr_vdev *exec_vdev, *vdev;
	uint8_t exec_vdev_id = recfg_ctx->curr_recfg_req.vdev_id;
	struct mlo_link_info *link_info;
	uint8_t i;

	exec_vdev = wlan_objmgr_get_vdev_by_id_from_psoc(psoc, exec_vdev_id,
							 WLAN_MLO_MGR_ID);

	/* CASE 1: exec vdev is already in CONNECTED/SMD_ROAM_SYNC (ML→ML) */
	if (exec_vdev && cm_is_vdev_smd_roam_sync_in_progress(exec_vdev))
		return exec_vdev;

	if (exec_vdev)
		wlan_objmgr_vdev_release_ref(exec_vdev, WLAN_MLO_MGR_ID);

	/* CASE 2: SL→SL — search for the other vdev in SMD_ROAM_SYNC */
	if (!mlo_dev_ctx->sta_ctx)
		return NULL;

	for (i = 0; i < WLAN_MAX_ML_BSS_LINKS; i++) {
		link_info = &mlo_dev_ctx->sta_ctx->links_info[i];

		if (link_info->vdev_id == WLAN_INVALID_VDEV_ID ||
		    link_info->vdev_id == exec_vdev_id)
			continue;

		vdev = wlan_objmgr_get_vdev_by_id_from_psoc(psoc,
							    link_info->vdev_id,
							    WLAN_MLO_MGR_ID);
		if (!vdev)
			continue;

		if (cm_is_vdev_smd_roam_sync_in_progress(vdev))
			return vdev;

		wlan_objmgr_vdev_release_ref(vdev, WLAN_MLO_MGR_ID);
	}

	mlo_debug("SMD: could not find assoc vdev in SMD_ROAM_SYNC");
	return NULL;
}

/**
 * smd_remove_roam_cmd() - Remove the original roam command from serialization.
 * @cm_ctx: Connection manager context
 *
 * Looks up the first pending roam command for the vdev associated with
 * @cm_ctx and removes it from the serialization queue. Safe to call when
 * no roam command is present.
 */
void smd_remove_roam_cmd(struct cnx_mgr *cm_ctx)
{
	struct cm_roam_req *roam_req;
	wlan_cm_id cm_id = CM_ID_INVALID;

	roam_req = cm_get_first_roam_command(cm_ctx->vdev);
	if (roam_req)
		cm_id = roam_req->cm_id;
	if (cm_id != CM_ID_INVALID)
		cm_remove_cmd(cm_ctx, &cm_id);
}

QDF_STATUS
smd_exec_complete(struct wlan_objmgr_psoc *psoc,
			struct mlo_link_recfg_context *recfg_ctx)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct wlan_objmgr_vdev *target_vdev;
	struct roam_offload_synch_ind *sync_ind;
	uint32_t sync_ind_len;
	QDF_STATUS status = QDF_STATUS_SUCCESS;
	struct wlan_roam_synch_complete_params sync_params = {};
	uint8_t i;

	if (!recfg_ctx) {
		mlo_err("SMD: recfg_ctx is NULL");
		return QDF_STATUS_E_NULL_VALUE;
	}

	mlo_dev_ctx = recfg_ctx->ml_dev;
	if (!mlo_dev_ctx) {
		mlo_err("SMD: mlo_dev_ctx is NULL");
		return QDF_STATUS_E_INVAL;
	}

	sync_ind = recfg_ctx->cached_sync_ind;
	sync_ind_len = sync_ind ? sizeof(*sync_ind) : 0;

	recfg_ctx->smd_roam_in_progress = false;
	recfg_ctx->st_exec_in_progress = false;

	sync_params.vdev_id = recfg_ctx->curr_recfg_req.vdev_id;
	for (i = 0; i < recfg_ctx->num_vdev_repurpose_req &&
	     i < WLAN_MAX_ML_BSS_LINKS; i++) {
		sync_params.vdev_repurpose_resp[i].vdev_id =
			recfg_ctx->vdev_repurpose_req[i].vdev_id;
		sync_params.vdev_repurpose_resp[i].status =
			QDF_STATUS_SUCCESS;
		sync_params.num_vdev_repurpose_resp++;
	}

	target_vdev = smd_find_new_assoc_vdev(mlo_dev_ctx, recfg_ctx, psoc);
	if (!target_vdev) {
		mlo_err("SMD: no assoc vdev in SMD_ROAM_SYNC for exec complete");
		return QDF_STATUS_E_FAILURE;
	}

	/* Update all sta_ctx links (active + standby) from target_bss_ctx
	 * while it is still valid, then free it before sending the roam
	 * sync complete event to FW.
	 */
	smd_roam_update_sta_ctx_links(target_vdev);

	/*
	 * Deliver EV_SMD_EXEC_COMPLETE — the CM handler sends
	 * WMI_ROAM_SYNCH_COMPLETE to FW via
	 * wlan_cm_tgt_send_roam_sync_complete_cmd().  Delivery is synchronous
	 * so the stack pointer for sync_params remains valid throughout.
	 */
	mlo_debug("SMD: delivering EV_SMD_EXEC_COMPLETE to vdev %d",
		  wlan_vdev_get_id(target_vdev));

	status = cm_sm_deliver_event(target_vdev,
				     WLAN_CM_SM_EV_SMD_EXEC_COMPLETE,
				     sizeof(sync_params), &sync_params);

	if (QDF_IS_STATUS_ERROR(status))
		mlo_err("CM SMD Exec complete evt delivery failed");

	smd_roam_cleanup_ies(recfg_ctx);
	wlan_objmgr_vdev_release_ref(target_vdev, WLAN_MLO_MGR_ID);
	return status;
}

bool wlan_is_smd_roam_sync(struct roam_offload_synch_ind *sync_ind)
{
	return sync_ind && sync_ind->num_vdev_repurpose_req > 0;
}

QDF_STATUS wlan_smd_roam_sync_status(QDF_STATUS status)
{
	if (status == QDF_STATUS_E_PENDING)
		return QDF_STATUS_SUCCESS;
	return status;
}

void smd_roam_update_sta_ctx_links(struct wlan_objmgr_vdev *vdev)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct smd_context *smd_ctx;
	struct wlan_mlo_sta *target_bss_ctx;
	struct mlo_link_info *src_link;
	struct mlo_link_info *dst_link;
	struct wlan_channel *chan_buf;
	uint8_t i;

	if (!vdev)
		return;

	mlo_dev_ctx = vdev->mlo_dev_ctx;
	if (!mlo_dev_ctx || !mlo_dev_ctx->sta_ctx) {
		mlo_err("SMD: mlo_dev_ctx or sta_ctx NULL for vdev %d",
			wlan_vdev_get_id(vdev));
		return;
	}

	smd_ctx = mlo_dev_ctx->smd_ctx;
	if (!smd_ctx) {
		mlo_err("SMD: smd_ctx NULL for vdev %d",
			wlan_vdev_get_id(vdev));
		return;
	}

	target_bss_ctx =
		smd_ctx->prepared_targets[smd_ctx->active_target_idx].target_bss_ctx;
	if (!target_bss_ctx) {
		mlo_err("SMD: target_bss_ctx NULL at active_target_idx=%d",
			smd_ctx->active_target_idx);
		return;
	}

	/*
	 * Reset all sta_ctx link slots, then copy directly from target_bss_ctx.
	 * link_chan_info pointers are heap-allocated and owned by sta_ctx —
	 * preserve them across the reset and copy the channel content in-place.
	 */
	for (i = 0; i < WLAN_MAX_ML_BSS_LINKS; i++) {
		dst_link = &mlo_dev_ctx->sta_ctx->links_info[i];
		src_link = &target_bss_ctx->links_info[i];

		/* Stash and restore the owned channel buffer pointer */
		chan_buf = dst_link->link_chan_info;

		qdf_mem_zero(dst_link, sizeof(*dst_link));
		dst_link->link_chan_info = chan_buf;

		dst_link->vdev_id        = src_link->vdev_id;
		dst_link->link_id        = src_link->link_id;
		dst_link->chan_freq       = src_link->chan_freq;
		dst_link->cnx_tx_nss     = src_link->cnx_tx_nss;
		dst_link->cnx_rx_nss     = src_link->cnx_rx_nss;
		dst_link->link_status_code = src_link->link_status_code;
		qdf_copy_macaddr(&dst_link->link_addr, &src_link->link_addr);
		qdf_copy_macaddr(&dst_link->ap_link_addr, &src_link->ap_link_addr);

		if (src_link->link_chan_info && dst_link->link_chan_info)
			qdf_mem_copy(dst_link->link_chan_info,
				     src_link->link_chan_info,
				     sizeof(struct wlan_channel));

		if (!qdf_is_macaddr_zero(&dst_link->ap_link_addr))
			mlo_debug("SMD: links_info[%d] vdev=%d link_id=%d freq=%d AP="
				  QDF_MAC_ADDR_FMT " self=" QDF_MAC_ADDR_FMT,
				  i, dst_link->vdev_id, dst_link->link_id,
				  dst_link->chan_freq,
				  QDF_MAC_ADDR_REF(dst_link->ap_link_addr.bytes),
				  QDF_MAC_ADDR_REF(dst_link->link_addr.bytes));
	}
}

/**
 * smd_roam_update_deflink() - Update adapter->deflink after SMD roam.
 * @vdev: New assoc vdev receiving EV_SMD_EXEC_COMPLETE
 *
 * For cross-vdev SMD roams (e.g. SL→SL where vdev0 disconnects and vdev1
 * becomes the new assoc vdev), adapter->deflink still points to the old
 * vdev's link_info. The LFR3 path fixes this in mlo_cm_roam_sync_cb()
 * before entering the SMD async path (which returns E_PENDING and skips
 * that code), so SMD must do it here instead.
 *
 * Guarded by is_cross_vdev_roam — non-cross-vdev SMD roams (ML→ML) leave
 * the assoc vdev unchanged and deflink is already correct.
 */
void smd_roam_update_deflink(struct wlan_objmgr_vdev *vdev)
{
	struct mlo_mgr_context *g_mlo_ctx = wlan_objmgr_get_mlo_ctx();

	if (!vdev)
		return;

	if (!wlan_cm_is_cross_vdev_roaming(vdev))
		return;

	if (!g_mlo_ctx || !g_mlo_ctx->osif_ops ||
	    !g_mlo_ctx->osif_ops->mlo_roam_osif_update_deflink)
		return;

	g_mlo_ctx->osif_ops->mlo_roam_osif_update_deflink(vdev,
							   wlan_vdev_get_id(vdev));
}

/**
 * smd_abort_roam_sync() - Abort SMD roaming on DISCONNECT_REQ.
 * @vdev: vdev in CONNECTED/SMD_ROAM_SYNC
 *
 * Called on T6 before transitioning to DISCONNECTING. Cancels pending
 * SMD operations. The disconnect flow handles actual peer teardown.
 */
void smd_abort_roam_sync(struct wlan_objmgr_vdev *vdev)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct mlo_link_recfg_context *recfg_ctx;

	if (!vdev)
		return;

	mlo_dev_ctx = vdev->mlo_dev_ctx;
	if (!mlo_dev_ctx || !mlo_dev_ctx->link_recfg_ctx)
		return;

	recfg_ctx = mlo_dev_ctx->link_recfg_ctx;
	recfg_ctx->smd_roam_in_progress = false;
	recfg_ctx->st_exec_in_progress = false;
	smd_free_cached_sync_ind(recfg_ctx);

	mlo_debug("SMD: roam aborted for vdev %d", wlan_vdev_get_id(vdev));
}

void smd_abort_link_recfg(struct mlo_link_recfg_context *recfg_ctx)
{
	if (!recfg_ctx)
		return;

	/*
	 * Free cached_sync_ind before transitioning to ABORT so that a stale
	 * WMI_ROAM_SYNCH_COMPLETE is not sent if the FW roam sync event still
	 * arrives after this point.
	 *
	 * Do NOT clear smd_roam_in_progress here. The flag must remain set so
	 * that S_ABORT/EV_COMPLETED routes to smd_link_recfg_complete(false),
	 * which clears smd_roam_in_progress and st_exec_in_progress atomically
	 * as part of the normal abort completion path.
	 * smd_free_cached_sync_ind() is idempotent (NULL-checks the pointer),
	 * so the second call inside smd_link_recfg_complete(false) is safe.
	 */
	smd_free_cached_sync_ind(recfg_ctx);

	mlo_debug("SMD: link recfg aborted, cached_sync_ind freed");
}

uint32_t
smd_get_roam_sync_timeout(struct wlan_objmgr_vdev *vdev)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx;

	if (!vdev)
		return FW_ROAM_SYNC_TIMEOUT;

	mlo_dev_ctx = vdev->mlo_dev_ctx;
	if (mlo_dev_ctx && mlo_dev_ctx->link_recfg_ctx &&
	    smd_roam_in_progress(mlo_dev_ctx->link_recfg_ctx))
		return FW_SMD_ROAM_SYNC_TIMEOUT;

	return FW_ROAM_SYNC_TIMEOUT;
}

/**
 * smd_roam_link_switch_disconnect_done() - Handle link switch disconnect
 *                                          completion during SMD roaming
 * @vdev: vdev on which the link switch disconnect completed
 * @mlo_dev_ctx: MLO device context
 *
 * Handles post-disconnect processing for SMD-initiated link switches.
 * For REMOVE_LINK: advances FSM directly to COMPLETE_SUCCESS (no new link).
 * For HOST_ADD_LINK: looks up the prepared target AP link info and proceeds
 * to MAC address change / connect via mlo_mgr_link_switch_decide_mac_addr_change().
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
smd_roam_link_switch_disconnect_done(struct wlan_objmgr_vdev *vdev,
				     struct wlan_mlo_dev_context *mlo_dev_ctx)
{
	QDF_STATUS status;
	struct mlo_link_info *new_link_info;
	struct wlan_mlo_link_switch_req *req = &mlo_dev_ctx->link_ctx->last_req;

	if (req->reason == MLO_LINK_SWITCH_REASON_SMD_ROAM_REMOVE_LINK) {
		/* Disconnect-only: no new link to set up.
		 * FSM transitions DISCONNECT_CURR_LINK -> COMPLETE_SUCCESS.
		 */
		mlo_debug("VDEV %d REMOVE_LINK disconnect done, completing",
			  req->vdev_id);
		status = mlo_mgr_link_switch_trans_next_state(mlo_dev_ctx);
		if (QDF_IS_STATUS_ERROR(status))
			mlo_mgr_remove_link_switch_cmd(vdev);
		return status;
	}

	new_link_info = smd_get_prepared_ap_link_info(vdev,
						      &req->tgt_ap_link_addr);
	if (!new_link_info) {
		mlo_err("VDEV %d SMD new link not found in mlo dev ctx",
			req->vdev_id);
		mlo_mgr_remove_link_switch_cmd(vdev);
		return QDF_STATUS_E_INVAL;
	}

	status = mlo_mgr_link_switch_trans_next_state(mlo_dev_ctx);
	if (QDF_IS_STATUS_ERROR(status)) {
		mlo_mgr_remove_link_switch_cmd(vdev);
		return status;
	}

	return mlo_mgr_link_switch_decide_mac_addr_change(vdev, req,
							  new_link_info);
}

/**
 * smd_roam_link_switch_start_connect() - Trigger link switch connect during SMD roaming
 * @vdev: vdev on which the HOST_ADD_LINK link switch was requested
 *
 * Called when a link switch with reason MLO_LINK_SWITCH_REASON_HOST_ADD_LINK
 * arrives and smd_is_roaming_in_progress() is true.  Delegates to
 * mlo_mgr_link_switch_start_connect() which drives the CM connect for the
 * new link using the cached connect request.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise
 */
QDF_STATUS
smd_roam_link_switch_start_connect(struct wlan_objmgr_vdev *vdev)
{
	QDF_STATUS status = QDF_STATUS_E_INVAL;
	struct wlan_cm_connect_req conn_req = {0};
	struct mlo_link_info *mlo_link_info;
	struct wlan_mlo_sta *sta_ctx;
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct wlan_mlo_link_switch_req *req;
	struct mlo_link_recfg_context *recfg_ctx;

	if (!vdev) {
		mlo_err("SMD: vdev is NULL");
		return QDF_STATUS_E_INVAL;
	}

	mlo_err("VDEV %d link switch connect request",
		wlan_vdev_get_id(vdev));

	mlo_dev_ctx = vdev->mlo_dev_ctx;
	if (!mlo_dev_ctx) {
		mlo_err("SMD: ML dev ctx is NULL");
		return QDF_STATUS_E_INVAL;
	}

	recfg_ctx = mlo_dev_ctx->link_recfg_ctx;
	if (!recfg_ctx) {
		mlo_err("SMD: link recfg ctx is NULL");
		return QDF_STATUS_E_INVAL;
	}

	if (!mlo_dev_ctx->link_ctx) {
		mlo_err("SMD: link ctx is NULL");
		return QDF_STATUS_E_INVAL;
	}
	req = &mlo_dev_ctx->link_ctx->last_req;

	if (!smd_is_roaming_in_progress(vdev)) {
		mlo_err("SMD: roaming not in progress for vdev %d",
			wlan_vdev_get_id(vdev));
		return QDF_STATUS_SUCCESS;
	}

	mlo_link_info = smd_get_prepared_ap_link_info(vdev, &req->tgt_ap_link_addr);
	if (!mlo_link_info) {
		mlo_err("SMD: AP link info not found for "QDF_MAC_ADDR_FMT,
			QDF_MAC_ADDR_REF(req->tgt_ap_link_addr.bytes));
		return QDF_STATUS_SUCCESS;
	}

		/* Select correct vdev based on link switch reason */
	if (req->reason == MLO_LINK_SWITCH_REASON_SMD_ROAM_ADD_LINK) {
		/* For idle vdev SMD roam, use the vdev passed in (idle vdev) */
		mlo_debug("SMD_ADD_LINK: Using idle vdev %d for connect",
			  wlan_vdev_get_id(vdev));
		wlan_vdev_mlme_set_mlo_vdev(vdev);
		wlan_vdev_mlme_set_mlo_link_vdev(vdev);
	}
	sta_ctx = mlo_dev_ctx->sta_ctx;
	copied_conn_req_lock_acquire(sta_ctx);
	if (sta_ctx->copied_conn_req) {
		qdf_mem_copy(&conn_req, sta_ctx->copied_conn_req,
			     sizeof(struct wlan_cm_connect_req));
	} else {
		copied_conn_req_lock_release(sta_ctx);
		goto out;
	}
	copied_conn_req_lock_release(sta_ctx);

	conn_req.vdev_id = req->vdev_id;
	conn_req.source = CM_MLO_LINK_SWITCH_CONNECT;
	wlan_vdev_set_link_id(vdev, req->new_ieee_link_id);

	conn_req.chan_freq = req->new_primary_freq;
	conn_req.link_id = req->new_ieee_link_id;
	qdf_copy_macaddr(&conn_req.bssid, &mlo_link_info->ap_link_addr);
	qdf_copy_macaddr(&conn_req.bssid_hint, &mlo_link_info->ap_link_addr);
	if (req->reason == MLO_LINK_SWITCH_REASON_SMD_ROAM_ADD_LINK) {
		/* Idle vdev has no SSID; read it from the assoc vdev instead */
		struct wlan_objmgr_vdev *assoc_vdev =
					wlan_mlo_get_assoc_link_vdev(vdev);
		if (!assoc_vdev) {
			mlo_err("SMD_ADD_LINK: No assoc vdev found");
			goto out;
		}
		wlan_vdev_mlme_get_ssid(assoc_vdev, conn_req.ssid.ssid,
					&conn_req.ssid.length);
	} else {
		wlan_vdev_mlme_get_ssid(vdev, conn_req.ssid.ssid,
					&conn_req.ssid.length);
	}

	qdf_copy_macaddr(&conn_req.mld_addr, &recfg_ctx->curr_recfg_req.add_link_info.mld_addr);

	conn_req.crypto.auth_type = 0;
	if (mlo_dev_ctx->smd_ctx && mlo_dev_ctx->smd_ctx->num_prepared > 0 &&
	    mlo_dev_ctx->smd_ctx->prepared_targets[0].prepared &&
	    mlo_dev_ctx->smd_ctx->prepared_targets[0].target_bss_ctx) {
		struct wlan_mlo_sta *tgt_bss_ctx =
			mlo_dev_ctx->smd_ctx->prepared_targets[0].target_bss_ctx;
		struct mlo_partner_info *pinfo = &conn_req.ml_parnter_info;
		uint8_t pi, nl = 0;

		pinfo->num_partner_links = 0;
		for (pi = 0; pi < WLAN_MAX_ML_BSS_LINKS; pi++) {
			struct mlo_link_info *linfo = &tgt_bss_ctx->links_info[pi];

			if (qdf_is_macaddr_zero(&linfo->ap_link_addr))
				continue;
			if (linfo->link_id == req->new_ieee_link_id)
				continue;
			pinfo->partner_link_info[nl].link_id = linfo->link_id;
			if (recfg_ctx->curr_recfg_req.st_prep_link_recfg)
				pinfo->partner_link_info[nl].vdev_id = WLAN_INVALID_VDEV_ID;
			else
				pinfo->partner_link_info[nl].vdev_id = linfo->vdev_id;
			pinfo->partner_link_info[nl].ap_link_addr = linfo->ap_link_addr;
			pinfo->partner_link_info[nl].link_addr = linfo->link_addr;
			pinfo->partner_link_info[nl].chan_freq = linfo->chan_freq;
			nl++;
		}
		pinfo->num_partner_links = nl;
		mlo_debug("SMD_ADD_LINK: updated partner_info from target_bss_ctx: %d links",
			  nl);
	} else {
		conn_req.ml_parnter_info = sta_ctx->ml_partner_info;
	}

	mlo_allocate_and_copy_ies(&conn_req, sta_ctx->copied_conn_req);

	status = wlan_cm_start_connect(vdev, &conn_req);
	if (QDF_IS_STATUS_SUCCESS(status))
		mlo_update_connected_links(vdev, 1);

	wlan_cm_free_connect_req_param(&conn_req);

out:
	if (QDF_IS_STATUS_ERROR(status)) {
		mlo_err("VDEV %d link switch connect request failed",
			wlan_vdev_get_id(vdev));
		mlo_mgr_remove_link_switch_cmd(vdev);
	}

	mlo_debug("SMD: triggering link switch connect for vdev %d",
		  req->vdev_id);

	return status;
}

QDF_STATUS
smd_roam_start_link_switch(struct wlan_objmgr_vdev *vdev,
			   struct wlan_serialization_command *cmd)
{
	QDF_STATUS status = QDF_STATUS_E_INVAL;
	uint8_t vdev_id, old_link_id, new_link_id;
	struct wlan_mlo_dev_context *mlo_dev_ctx = vdev->mlo_dev_ctx;
	struct wlan_mlo_link_switch_req *req = &mlo_dev_ctx->link_ctx->last_req;
	struct qdf_mac_addr bssid;

	vdev_id = wlan_vdev_get_id(vdev);
	old_link_id = req->curr_ieee_link_id;
	new_link_id = req->new_ieee_link_id;

	mlo_debug("VDEV %d start link switch", vdev_id);
	mlo_mgr_link_switch_trans_next_state(mlo_dev_ctx);

	if (!smd_is_roaming_in_progress(vdev)) {
		mlo_err("SMD roaming not in progress");
		return QDF_STATUS_SUCCESS;
	}

	status = wlan_vdev_get_bss_peer_mac(vdev, &bssid);
	if (QDF_IS_STATUS_ERROR(status))
		return status;

	status = wlan_vdev_get_bss_peer_mld_mac(vdev, &req->peer_mld_addr);
	if (QDF_IS_STATUS_ERROR(status))
		return status;

	status = mlo_mgr_link_switch_notify(vdev, req);
	if (QDF_IS_STATUS_ERROR(status))
		return status;

	wlan_vdev_mlme_set_mlo_link_switch_in_progress(vdev);
	status = mlo_mgr_link_switch_trans_next_state(mlo_dev_ctx);
	if (QDF_IS_STATUS_ERROR(status))
		return status;

	status = wlan_cm_disconnect(vdev, CM_MLO_LINK_SWITCH_DISCONNECT,
				    REASON_FW_TRIGGERED_LINK_SWITCH, &bssid);

	if (QDF_IS_STATUS_ERROR(status))
		mlo_err("VDEV %d disconnect request not handled", req->vdev_id);

	return status;
}

bool smd_roam_skip_rso(struct wlan_objmgr_vdev *vdev)
{
	return cm_is_vdev_smd_roam_sync_in_progress(vdev);
}

QDF_STATUS
smd_link_recfg_parse_perptk_ies(
	struct wlan_mlo_link_recfg_rsp *link_recfg_rsp,
	uint8_t *opt, uint32_t remaining)
{
	while (remaining >= MIN_IE_LEN) {
		uint8_t eid = opt[0];
		uint8_t elen = opt[1];

		if (remaining < (uint32_t)(MIN_IE_LEN + elen)) {
			mlo_err("Optional IE truncated: eid=%u len=%u rem=%u",
				eid, elen, remaining);
			break;
		}

		if (eid == WLAN_ELEMID_EXTN_ELEM && elen >= 1) {
			uint8_t ext_id = opt[2];
			uint8_t payload_len = elen - 1;

			switch (ext_id) {
			case WLAN_EXTN_ELEMID_KEY_DELIVERY:
				if (!link_recfg_rsp->key_delivery.ptr) {
					link_recfg_rsp->key_delivery.ptr =
						qdf_mem_malloc(payload_len);
					if (!link_recfg_rsp->key_delivery.ptr)
						return QDF_STATUS_E_NOMEM;
					qdf_mem_copy(link_recfg_rsp->key_delivery.ptr,
						     opt + MIN_IE_LEN + 1,
						     payload_len);
					link_recfg_rsp->key_delivery.len = payload_len;
					mlo_debug("Parsed Key Delivery IE len=%u",
						  payload_len);
				}
				break;
			case WLAN_EXTN_ELEMID_MSCS_DESCRIPTOR:
				if (!link_recfg_rsp->mscs_descriptor.ptr) {
					link_recfg_rsp->mscs_descriptor.ptr =
						qdf_mem_malloc(payload_len);
					if (!link_recfg_rsp->mscs_descriptor.ptr)
						return QDF_STATUS_E_NOMEM;
					qdf_mem_copy(link_recfg_rsp->mscs_descriptor.ptr,
						     opt + MIN_IE_LEN + 1,
						     payload_len);
					link_recfg_rsp->mscs_descriptor.len = payload_len;
					mlo_debug("Parsed MSCS Descriptor IE len=%u",
						  payload_len);
				}
				break;
			case WLAN_EXTN_ELEMID_DH_PARAM:
				if (!link_recfg_rsp->diffie_hellman_param.ptr) {
					link_recfg_rsp->diffie_hellman_param.ptr =
						qdf_mem_malloc(payload_len);
					if (!link_recfg_rsp->diffie_hellman_param.ptr)
						return QDF_STATUS_E_NOMEM;
					qdf_mem_copy(link_recfg_rsp->diffie_hellman_param.ptr,
						     opt + MIN_IE_LEN + 1,
						     payload_len);
					link_recfg_rsp->diffie_hellman_param.len = payload_len;
					mlo_debug("Parsed DH Parameter IE len=%u",
						  payload_len);
				}
				break;
			case WLAN_EXTN_ELEMID_NONCE:
				if (!link_recfg_rsp->nonce.ptr) {
					link_recfg_rsp->nonce.ptr =
						qdf_mem_malloc(payload_len);
					if (!link_recfg_rsp->nonce.ptr)
						return QDF_STATUS_E_NOMEM;
					qdf_mem_copy(link_recfg_rsp->nonce.ptr,
						     opt + MIN_IE_LEN + 1,
						     payload_len);
					link_recfg_rsp->nonce.len = payload_len;
					mlo_debug("Parsed Nonce IE len=%u",
						  payload_len);
				}
				break;
			case WLAN_EXTN_ELEMID_MIC:
				if (!link_recfg_rsp->mic.ptr) {
					link_recfg_rsp->mic.ptr =
						qdf_mem_malloc(payload_len);
					if (!link_recfg_rsp->mic.ptr)
						return QDF_STATUS_E_NOMEM;
					qdf_mem_copy(link_recfg_rsp->mic.ptr,
						     opt + MIN_IE_LEN + 1,
						     payload_len);
					link_recfg_rsp->mic.len = payload_len;
					mlo_debug("Parsed MIC IE len=%u",
						  payload_len);
				}
				break;
			default:
				break;
			}
		}

		opt += MIN_IE_LEN + elen;
		remaining -= MIN_IE_LEN + elen;
	}
	return QDF_STATUS_SUCCESS;
}

void
smd_link_recfg_free_perptk_ies(struct wlan_mlo_link_recfg_rsp *link_recfg_rsp)
{
	if (link_recfg_rsp->key_delivery.ptr) {
		qdf_mem_free(link_recfg_rsp->key_delivery.ptr);
		link_recfg_rsp->key_delivery.ptr = NULL;
		link_recfg_rsp->key_delivery.len = 0;
	}
	if (link_recfg_rsp->mscs_descriptor.ptr) {
		qdf_mem_free(link_recfg_rsp->mscs_descriptor.ptr);
		link_recfg_rsp->mscs_descriptor.ptr = NULL;
		link_recfg_rsp->mscs_descriptor.len = 0;
	}
	if (link_recfg_rsp->diffie_hellman_param.ptr) {
		qdf_mem_free(link_recfg_rsp->diffie_hellman_param.ptr);
		link_recfg_rsp->diffie_hellman_param.ptr = NULL;
		link_recfg_rsp->diffie_hellman_param.len = 0;
	}
	if (link_recfg_rsp->nonce.ptr) {
		qdf_mem_free(link_recfg_rsp->nonce.ptr);
		link_recfg_rsp->nonce.ptr = NULL;
		link_recfg_rsp->nonce.len = 0;
	}
	if (link_recfg_rsp->mic.ptr) {
		qdf_mem_free(link_recfg_rsp->mic.ptr);
		link_recfg_rsp->mic.ptr = NULL;
		link_recfg_rsp->mic.len = 0;
	}
}

void
smd_link_recfg_cleanup_rsp(struct mlo_link_recfg_context *ctx,
			   struct wlan_mlo_link_recfg_rsp *link_recfg_rsp)
{
	if (link_recfg_rsp->oci_ie.ptr) {
		qdf_mem_free(link_recfg_rsp->oci_ie.ptr);
		link_recfg_rsp->oci_ie.ptr = NULL;
		link_recfg_rsp->oci_ie.len = 0;
	}
	if (link_recfg_rsp->mlo_ie.ptr) {
		qdf_mem_free(link_recfg_rsp->mlo_ie.ptr);
		link_recfg_rsp->mlo_ie.ptr = NULL;
		link_recfg_rsp->mlo_ie.len = 0;
	}
	if (link_recfg_rsp->smd_bss_trans_params.ptr) {
		qdf_mem_free(link_recfg_rsp->smd_bss_trans_params.ptr);
		link_recfg_rsp->smd_bss_trans_params.ptr = NULL;
		link_recfg_rsp->smd_bss_trans_params.len = 0;
	}
	smd_link_recfg_free_perptk_ies(link_recfg_rsp);
	if (ctx->rsp_frame.ptr) {
		qdf_mem_free(ctx->rsp_frame.ptr);
		ctx->rsp_frame.ptr = NULL;
		ctx->rsp_frame.len = 0;
	}
	if (ctx->rsp_rx_frame.ptr) {
		qdf_mem_free(ctx->rsp_rx_frame.ptr);
		ctx->rsp_rx_frame.ptr = NULL;
		ctx->rsp_rx_frame.len = 0;
	}
}

void
smd_link_recfg_ctx_cleanup(struct mlo_link_recfg_context *recfg_ctx)
{
	if (!recfg_ctx)
		return;

	smd_free_cached_sync_ind(recfg_ctx);

	qdf_mem_zero(recfg_ctx->vdev_repurpose_req,
		     sizeof(recfg_ctx->vdev_repurpose_req));
	recfg_ctx->num_vdev_repurpose_req = 0;
	qdf_mem_zero(&recfg_ctx->smd_transition_ie,
		     sizeof(recfg_ctx->smd_transition_ie));
	recfg_ctx->tgt_ap_link_bitmap = 0;
	recfg_ctx->smd_roam_in_progress = false;
	recfg_ctx->current_link_index = 0;
	recfg_ctx->st_exec_in_progress = false;

	mlo_link_recfg_ctx_free_ies(recfg_ctx);
}

#endif /* WLAN_FEATURE_11BN_SMD */
