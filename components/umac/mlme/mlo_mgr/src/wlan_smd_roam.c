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
#include <wlan_cm_roam_i.h>
#include "wlan_cm_roam_api.h"
#include "wlan_mlme_vdev_mgr_interface.h"
#include <include/wlan_mlme_cmn.h>
#include <wlan_cm_api.h>
#include <utils_mlo.h>
#include <wlan_mlo_mgr_peer.h>
#include "wlan_mlo_link_force.h"
#include "wlan_scan_api.h"
#include "lim_types.h"
#include <wlan_smd_roam.h>

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
		link_info = &mlo_dev_ctx->link_ctx->links_info[j];

		if (!qdf_is_macaddr_equal(&link_add->self_link_addr,
					  &link_info->link_addr)) {
			mlo_err("link %d info self " QDF_MAC_ADDR_FMT " not equal ADD_LINK: " QDF_MAC_ADDR_FMT "",
				link_info->link_id,
				QDF_MAC_ADDR_REF(link_info->self_link_addr.bytes),
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

		if (!cm_is_vdev_connected(vdev)) {
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
			link_info = &mlo_dev_ctx->link_ctx->links_info[j];

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

QDF_STATUS smd_fw_roam_start(struct wlan_objmgr_vdev *vdev)
{
	struct wlan_objmgr_psoc *psoc;
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct mlo_link_recfg_context *recfg_ctx;
	struct wlan_mlo_link_recfg_req recfg_req = {0};
	struct mlo_link_info *curr_link_info = NULL;
	QDF_STATUS status;
	uint8_t i, idx;

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

	/* Validate VDEV repurpose TLVs */
	if (!recfg_ctx->num_vdev_repurpose_req) {
		mlo_err("SMD: No VDEV repurpose requests");
		return QDF_STATUS_E_INVAL;
	}

	mlo_debug("SMD: Roaming started, num_vdev_repurpose_req=%u",
		  recfg_ctx->num_vdev_repurpose_req);

	qdf_mem_zero(&recfg_req, sizeof(struct wlan_mlo_link_recfg_req));

	for (i = 0; i < recfg_ctx->num_vdev_repurpose_req; i++) {
		qdf_copy_macaddr(&recfg_req.add_link_info.link[i].ap_link_addr,
				 &recfg_ctx->vdev_repurpose_req[i].bssid);

		recfg_req.add_link_info.link[i].vdev_id = recfg_ctx->vdev_repurpose_req[i].vdev_id;
		recfg_req.add_link_info.num_links += 1;
		recfg_req.add_link_info.link[i].priority_index = i;
		mlo_debug("SMD: Add Target Link Priority %u: vdev_id=%u BSSID=" QDF_MAC_ADDR_FMT " MLD=" QDF_MAC_ADDR_FMT,
			  i,
			  recfg_ctx->vdev_repurpose_req[i].vdev_id,
			  QDF_MAC_ADDR_REF(recfg_ctx->vdev_repurpose_req[i].bssid.bytes),
			  QDF_MAC_ADDR_REF(recfg_ctx->vdev_repurpose_req[i].mld_addr.bytes));
		mlo_debug("Flags: bringup: %u cleanup: %u,inactive_link_pre_stop: %u, priority_index: %u",
			  recfg_ctx->vdev_repurpose_req[i].bringup_vdev,
			  recfg_ctx->vdev_repurpose_req[i].cleanup_vdev,
			  recfg_ctx->vdev_repurpose_req[i].inactive_link_pre_stop,
			  recfg_req.add_link_info.link[i].priority_index);
	}

	mlo_debug("SMD: Stored target AP link bitmap in link recfg ctx: 0x%x",
		  recfg_ctx->tgt_ap_link_bitmap);

	/* SMD vdev repurpose req is populated by priority
	 * copy to delete link info.
	 * Example:
	 * index 0 will be deleted first, if AP accepts the index 0 add link
	 */
	for (i = 0, idx = 0; i < recfg_ctx->num_vdev_repurpose_req &&
	     idx < WLAN_MAX_ML_BSS_LINKS ; i++) {
		curr_link_info = smd_find_current_ap_link_info(vdev,
							       recfg_ctx->vdev_repurpose_req[i].vdev_id);
		if (!curr_link_info) {
			mlo_err("Link info not found for vdev id %d",
				recfg_ctx->vdev_repurpose_req[i].vdev_id);
			continue;
		}
		recfg_req.del_link_info.link[idx].vdev_id = recfg_ctx->vdev_repurpose_req[i].vdev_id;
		recfg_req.del_link_info.link[idx].link_id = curr_link_info->link_id;
		qdf_copy_macaddr(&recfg_req.del_link_info.link[idx].self_link_addr,
				 &curr_link_info->link_addr);
		qdf_copy_macaddr(&recfg_req.del_link_info.link[idx].ap_link_addr,
				 &curr_link_info->ap_link_addr);
		mlo_debug("Delete link id %d, freq %d BSSID="QDF_MAC_ADDR_FMT "vdev id %d ",
			  curr_link_info->link_id,
			  curr_link_info->chan_freq,
			  QDF_MAC_ADDR_REF(curr_link_info->ap_link_addr.bytes),
			  curr_link_info->vdev_id);
		recfg_req.del_link_info.num_links += 1;
		idx++;
	}

	recfg_req.vdev_id = wlan_vdev_get_id(vdev);
	recfg_req.is_user_req = false;  /* SMD roaming is FW-initiated */
	recfg_req.is_fw_ind_received = true; /* This is from FW roam event */
	recfg_req.st_prep_link_recfg = true; /* This is for ST Prep req */

	status = smd_update_channel_freq(psoc, &recfg_req);
	if (QDF_IS_STATUS_ERROR(status)) {
		mlo_err("failed to find link freq for fw link recfg ind event");
		goto end;
	}

	status = mlo_link_recfg_sm_deliver_event(
				mlo_dev_ctx,
				WLAN_LINK_RECFG_SM_EV_SMD_ROAM_START,
				sizeof(recfg_req), &recfg_req);
end:
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
	/* TODO: Update below API with UHR link reconfig */
	status = lim_send_link_recfg_action_req_frame(vdev_id,
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
		link_info = &mlo_dev_ctx->link_ctx->links_info[i];
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

		if (!cm_is_vdev_disconnected(vdev)) {
			wlan_objmgr_vdev_release_ref(vdev, WLAN_LINK_RECFG_ID);
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
		link_info = &mlo_dev_ctx->link_ctx->links_info[i];
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
		mlo_err("fail to ssign self link for added links status %d",
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
		mlo_link_recfg_set_tx_link_addr(recfg_ctx,
						recfg_req,
						&next->req,
						curr_link_set &
						~del_link_set);
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
	} else if (recfg_req->add_link_info.num_links &&
		recfg_req->st_exec_link_recfg) {
		/* Handle ST Exec Request to add Target AP links */
		mlo_debug("Send ST Exec Request to add Target AP links");
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
		next->abort_handler = NULL;
		next++;
		next->state = WLAN_LINK_RECFG_S_COMPLETED;
		next->event = WLAN_LINK_RECFG_SM_EV_COMPLETED;
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
smd_is_roaming_in_progress(struct wlan_objmgr_vdev *vdev)
{
	if (!vdev || !vdev->mlo_dev_ctx)
		return false;

	return smd_roam_in_progress(vdev->mlo_dev_ctx->link_recfg_ctx);
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

	if (!bss_info || !tran || !recfg_ctx)
		return QDF_STATUS_E_NULL_VALUE;

	psoc = mlo_link_recfg_get_psoc(recfg_ctx);
	if (!psoc) {
		mlo_err("psoc is null");
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

	if (!tran->req.del_link_info.num_links) {
		mlo_err("Delete num links is 0");
		status = QDF_STATUS_E_INVAL;
		goto end;
	}

	if (bss_info->vdev_id != tran->req.del_link_info.link[0].vdev_id) {
		link_info = smd_find_current_ap_link_info(vdev, bss_info->vdev_id);

		if (!link_info) {
			mlo_err("Link info not found");
			status = QDF_STATUS_E_INVAL;
			goto end;
		}

		// update del link info
		tran->req.del_link_info.link[0].vdev_id = link_info->vdev_id;
		qdf_copy_macaddr(&tran->req.del_link_info.link[0].self_link_addr,
				 &link_info->link_addr);
		qdf_copy_macaddr(&tran->req.del_link_info.link[0].ap_link_addr,
				 &link_info->ap_link_addr);
		tran->req.del_link_info.num_links = 1;
		mlo_debug("Update del Link info vdev id %d", link_info->vdev_id);
	}

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

	// TODO: Update the Accepted links in smd_ctx.

	// update accepted links and check if delete link needs to be updated.
	bss_info = smd_find_first_accepted_link(recfg_ctx, tran);

	// Check if vdev id matches. del link info
	smd_update_del_link_info(recfg_ctx, bss_info, tran);

	/* Same PTK case, TODO add check for same ptk vs diff ptk */
	mlo_link_recfg_store_key(recfg_ctx, &tran->req);

	/* TODO: update added link mlo mgr */
	return status;
}

void
smd_link_recfg_complete(struct mlo_link_recfg_context *recfg_ctx,
			bool success)
{
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

	return QDF_STATUS_SUCCESS;
}

struct mlo_link_info *
smd_get_prepared_ap_link_info(struct wlan_objmgr_vdev *vdev,
			      struct qdf_mac_addr *ap_link_addr)
{
	struct mlo_link_info *link_info;
	uint8_t link_info_iter;

	if (!vdev || !vdev->mlo_dev_ctx || !ap_link_addr ||
	    qdf_is_macaddr_zero(ap_link_addr))
		return NULL;

	link_info = &vdev->mlo_dev_ctx->link_ctx->links_info[0];
	for (link_info_iter = 0; link_info_iter < WLAN_MAX_ML_BSS_LINKS;
	     link_info_iter++) {
		if (qdf_is_macaddr_equal(&link_info->ap_link_addr,
					 ap_link_addr))
			return link_info;
		link_info++;
	}

	return NULL;
}

struct mlo_link_info *
smd_get_prep_ap_link_info(struct wlan_objmgr_vdev *vdev,
			  struct wlan_mlo_link_switch_req *req)
{
	return smd_get_prepared_ap_link_info(vdev, &req->tgt_ap_link_addr);
}

QDF_STATUS
smd_roam_prep_complete(struct mlo_link_recfg_context *recfg_ctx,
		       struct mlo_link_recfg_state_req *req)
{
	return QDF_STATUS_SUCCESS;
}

QDF_STATUS smd_fw_roam_sync(struct wlan_objmgr_vdev *vdev)
{
	QDF_STATUS status = QDF_STATUS_SUCCESS;
	return status;
}
#endif
