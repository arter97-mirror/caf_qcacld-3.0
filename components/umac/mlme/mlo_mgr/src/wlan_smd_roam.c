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

#ifdef WLAN_FEATURE_11BN_SMD
QDF_STATUS smd_fw_roam_start(struct wlan_objmgr_vdev *vdev,
			     struct roam_offload_roam_event *roam_event)
{
	struct wlan_mlo_dev_context *mlo_dev_ctx;
	struct mlo_link_recfg_context *recfg_ctx;

	if (!vdev || !roam_event) {
		mlo_err("Invalid parameters");
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

	return QDF_STATUS_SUCCESS;
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
smd_create_link_recfg_transition_list(struct wlan_objmgr_vdev *vdev,
				      struct mlo_link_recfg_context *recfg_ctx)
{
	if (!recfg_ctx) {
		mlo_err("Invalid recfg req");
		return QDF_STATUS_E_INVAL;
	}

	return QDF_STATUS_SUCCESS;
}

QDF_STATUS
smd_link_recfg_assign_self_link_addr(struct mlo_link_recfg_context *recfg_ctx)
{
	if (!recfg_ctx) {
		mlo_err("Invalid recfg context");
		return QDF_STATUS_E_INVAL;
	}

	return QDF_STATUS_SUCCESS;
}
#endif
