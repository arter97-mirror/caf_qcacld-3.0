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

QDF_STATUS smd_fw_roam_start(struct wlan_objmgr_vdev *vdev)
{
	struct wlan_objmgr_psoc *psoc;
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct mlo_link_recfg_context *recfg_ctx;
	struct wlan_mlo_link_recfg_req recfg_req = {0};
	struct wlan_mlo_link_recfg_bss_info *link;
	struct mlo_link_info *ap_link_info;
	QDF_STATUS status;
	uint8_t i;

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
		mlo_debug("SMD: Priority %u: vdev_id=%u BSSID=" QDF_MAC_ADDR_FMT " MLD=" QDF_MAC_ADDR_FMT,
			  i,
			  recfg_ctx->vdev_repurpose_req[i].vdev_id,
			  QDF_MAC_ADDR_REF(recfg_ctx->vdev_repurpose_req[i].bssid.bytes),
			  QDF_MAC_ADDR_REF(recfg_ctx->vdev_repurpose_req[i].mld_addr.bytes));
		mlo_debug("Flags: bringup: %u cleanup: %u,inactive_link_pre_stop: %u",
			  recfg_ctx->vdev_repurpose_req[i].bringup_vdev,
			  recfg_ctx->vdev_repurpose_req[i].cleanup_vdev,
			  recfg_ctx->vdev_repurpose_req[i].inactive_link_pre_stop);
	}

	mlo_debug("SMD: Stored target AP link bitmap in link recfg ctx: 0x%x",
		  recfg_ctx->tgt_ap_link_bitmap);

	/* SMD vdev repurpose req is populated by priority
	 * copy the first entry as the link to be deleted first
	 */
	recfg_req.del_link_info.link[0].vdev_id = recfg_ctx->vdev_repurpose_req[0].vdev_id;
	recfg_req.del_link_info.num_links += 1;
	link = &recfg_req.del_link_info.link[0];
	ap_link_info = mlo_mgr_get_ap_link_by_link_id(mlo_dev_ctx,
						      link->link_id);
	if (!ap_link_info) {
		mlo_debug("del link " QDF_MAC_ADDR_FMT " link info not found",
			  QDF_MAC_ADDR_REF(link->ap_link_addr.bytes));
		status = QDF_STATUS_E_INVAL;
		goto end;
	}
	if (!ap_link_info->link_chan_info) {
		mlo_debug("del link " QDF_MAC_ADDR_FMT " ch info not found",
			  QDF_MAC_ADDR_REF(link->ap_link_addr.bytes));
		status = QDF_STATUS_E_INVAL;
		goto end;
	}

	/*
	 * Populate recfg_req structure
	 */
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
		next->event = WLAN_LINK_RECFG_SM_EV_ADD_LINK;
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
		/* TODO: SMD Execution */
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

QDF_STATUS
smd_st_prep_response_received(struct mlo_link_recfg_context *recfg_ctx,
			      struct link_recfg_rx_rsp *recfg_resp_data,
			      uint16_t event_data_len)
{
	QDF_STATUS status;
	struct mlo_link_recfg_state_tran *tran;

	if (!recfg_ctx || !recfg_resp_data || !event_data_len)
		return QDF_STATUS_E_INVAL;

	tran = mlo_link_recfg_get_curr_tran_req(recfg_ctx);
	if (!tran) {
		mlo_err("curr tran ctx null");
		return QDF_STATUS_E_INVAL;
	}

	if (QDF_TIMER_STATE_RUNNING ==
		qdf_mc_timer_get_current_state(&recfg_ctx->link_recfg_rsp_timer)) {
		status = qdf_mc_timer_stop(&recfg_ctx->link_recfg_rsp_timer);
		if (QDF_IS_STATUS_ERROR(status)) {
			mlo_err("Failed to stop the Link Recfg rsp timer");
			return QDF_STATUS_E_FAILURE;
		}
	}

	if (QDF_IS_STATUS_ERROR(recfg_resp_data->status)) {
		mlo_err("RX response failure %d", recfg_resp_data->status);
		return QDF_STATUS_E_INVAL;
	}

	mlo_debug("RX response success");

	status = mlo_link_recfg_tranistion_to_next_state(recfg_ctx);

	return status;

}
#endif
