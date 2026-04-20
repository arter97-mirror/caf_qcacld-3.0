/*
 * Copyright (c) 2016-2021 The Linux Foundation. All rights reserved.
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
 * DOC: wma_nan_datapath.c
 *
 * WMA NAN Data path API implementation
 */

#include "wma.h"
#include "wma_api.h"
#include "wmi_unified_api.h"
#include "wmi_unified.h"
#include "wma_nan_datapath.h"
#include "wma_internal.h"
#include "cds_utils.h"
#include "cdp_txrx_peer_ops.h"
#include "cdp_txrx_tx_delay.h"
#include "cdp_txrx_misc.h"
#include <cdp_txrx_handle.h>
#include "wlan_nan_api_i.h"

#ifdef FEATURE_WLAN_SUPPORT_NAN_STANDARD_MODE
/**
 * wma_ndp_peer_create_wait_for_confirm() - Wait for NDP peer create confirm
 * @wma: wma handle
 * @add_sta: add sta parameters
 *
 * This function waits for peer create confirmation from firmware for NDP peer.
 *
 * Return: QDF_STATUS_SUCCESS if request queued successfully, error otherwise
 */
static QDF_STATUS
wma_ndp_peer_create_wait_for_confirm(tp_wma_handle wma,
				     tpAddStaParams add_sta)
{
	struct wma_target_req *add_req;

	if (!tgt_nan_is_fw_support_standard_mode(wma->psoc))
		return QDF_STATUS_E_NOSUPPORT;

	if (!wlan_psoc_nif_fw_ext_cap_get(wma->psoc,
					  WLAN_SOC_F_PEER_CREATE_RESP))
		return QDF_STATUS_E_NOSUPPORT;

	wma_debug("Wait for NDP peer create confirm. vdev_id %d",
		  add_sta->smesessionId);
	add_req = wma_fill_hold_req(wma, add_sta->smesessionId,
				    WMA_PEER_CREATE_REQ,
				    WMA_NDP_PEER_CREATE_RESPONSE,
				    add_sta->staMac, add_sta,
				    WMA_PEER_CREATE_RESPONSE_TIMEOUT);
	if (!add_req) {
		wma_err("Failed to allocate request for vdev_id %d",
			add_sta->smesessionId);
		add_sta->status = QDF_STATUS_E_NULL_VALUE;
		wma_remove_peer(wma, add_sta->staMac,
				add_sta->smesessionId, false);
		return QDF_STATUS_E_NULL_VALUE;
	}

	return QDF_STATUS_SUCCESS;
}
#else
static inline QDF_STATUS
wma_ndp_peer_create_wait_for_confirm(tp_wma_handle wma,
				     tpAddStaParams add_sta)
{
	return QDF_STATUS_E_NOSUPPORT;
}
#endif

#ifdef FEATURE_WLAN_SUPPORT_NAN_STANDARD_MODE
void wma_handle_ndp_peer_create_rsp(tp_wma_handle wma,
				    struct peer_create_rsp_params *rsp_data,
				    struct wma_target_req *req_msg)
{
	wma_send_msg_high_priority(wma, WMA_ADD_STA_RSP, (void *)rsp_data, 0);
	qdf_mem_free(req_msg);
	wma_release_wakelock(&wma->wmi_cmd_rsp_wake_lock);
}

void wma_handle_ndp_peer_create_timeout(tp_wma_handle wma,
					struct wma_target_req *tgt_req)
{
	tAddStaParams *add_sta_params =
		(tAddStaParams *)tgt_req->user_data;

	wma_err("NDP peer create confirm timeout for vdev:%d",
		tgt_req->vdev_id);

	if (wma_crash_on_fw_timeout(wma->fw_timeout_crash))
		wma_trigger_recovery_assert_on_fw_timeout(
			WMA_NDP_PEER_CREATE_RESPONSE,
			WMA_PEER_CREATE_RESPONSE_TIMEOUT);

	if (!add_sta_params) {
		wma_err("vdev:%d Invalid user data for NDP peer create",
			tgt_req->vdev_id);
		qdf_mem_free(tgt_req->user_data);
		return;
	}

	add_sta_params->status = QDF_STATUS_E_TIMEOUT;
	wma_send_msg_high_priority(wma, WMA_ADD_STA_RSP,
				   (void *)add_sta_params, 0);
}
#endif /* FEATURE_WLAN_SUPPORT_NAN_STANDARD_MODE */

QDF_STATUS wma_add_sta_ndi_mode(tp_wma_handle wma, tpAddStaParams add_sta)
{
	enum ol_txrx_peer_state state = OL_TXRX_PEER_STATE_CONN;
	uint8_t pdev_id = WMI_PDEV_ID_SOC;
	void *soc = cds_get_context(QDF_MODULE_ID_SOC);
	QDF_STATUS status;
	struct wma_txrx_node *iface;

	iface = &wma->interfaces[add_sta->smesessionId];
	wma_debug("vdev: %d, peer_mac_addr: "QDF_MAC_ADDR_FMT,
		add_sta->smesessionId, QDF_MAC_ADDR_REF(add_sta->staMac));

	if (cdp_find_peer_exist_on_vdev(soc, add_sta->smesessionId,
					add_sta->staMac)) {
		wma_err("NDI peer already exists, peer_addr "QDF_MAC_ADDR_FMT,
			 QDF_MAC_ADDR_REF(add_sta->staMac));
		add_sta->status = QDF_STATUS_E_EXISTS;
		goto send_rsp;
	}

	/*
	 * The code above only checks the peer existence on its own vdev.
	 * Need to check whether the peer exists on other vDevs because firmware
	 * can't create the peer if the peer with same MAC address already
	 * exists on the pDev. As this peer belongs to other vDevs, just return
	 * here.
	 */
	if (cdp_find_peer_exist(soc, pdev_id, add_sta->staMac)) {
		wma_err("peer exists on other vdev with peer_addr "QDF_MAC_ADDR_FMT,
			 QDF_MAC_ADDR_REF(add_sta->staMac));
		add_sta->status = QDF_STATUS_E_EXISTS;
		goto send_rsp;
	}

	status = wma_create_peer(wma, add_sta->staMac, NULL,
				 WMI_PEER_TYPE_NAN_DATA, add_sta->smesessionId,
				 NULL, false);
	if (status != QDF_STATUS_SUCCESS) {
		wma_err("Failed to create peer for "QDF_MAC_ADDR_FMT,
			 QDF_MAC_ADDR_REF(add_sta->staMac));
		add_sta->status = status;
		goto send_rsp;
	}

	if (!cdp_find_peer_exist_on_vdev(soc, add_sta->smesessionId,
					 add_sta->staMac)) {
		wma_err("Failed to find peer handle using peer mac "QDF_MAC_ADDR_FMT,
			 QDF_MAC_ADDR_REF(add_sta->staMac));
		add_sta->status = QDF_STATUS_E_FAILURE;
		wma_remove_peer(wma, add_sta->staMac, add_sta->smesessionId,
				false);
		goto send_rsp;
	}

	wma_debug("Moving peer "QDF_MAC_ADDR_FMT" to state %d",
		  QDF_MAC_ADDR_REF(add_sta->staMac), state);
	cdp_peer_state_update(soc, add_sta->staMac, state);

	add_sta->status = QDF_STATUS_SUCCESS;

	/* Wait for peer create confirmation from firmware */
	status = wma_ndp_peer_create_wait_for_confirm(wma, add_sta);
	if (QDF_IS_STATUS_SUCCESS(status))
		return status;

send_rsp:
	status = add_sta->status;
	wma_debug("Sending add sta rsp to umac (mac:"QDF_MAC_ADDR_FMT", status:%d)",
		  QDF_MAC_ADDR_REF(add_sta->staMac), add_sta->status);

	wma_send_msg_high_priority(wma, WMA_ADD_STA_RSP, (void *)add_sta, 0);

	return status;
}

QDF_STATUS wma_delete_sta_req_ndi_mode(tp_wma_handle wma,
				       tpDeleteStaParams del_sta)
{
	QDF_STATUS status;
	uint8_t vdev_id = del_sta->smesessionId;
	struct wma_target_req *del_req;

	status = wma_remove_peer(wma, del_sta->staMac, vdev_id, false);
	del_sta->status = status;

	if (QDF_IS_STATUS_SUCCESS(status) &&
	    wmi_service_enabled(wma->wmi_handle,
				wmi_service_sync_delete_cmds)) {
		wma_debug("Wait for the peer delete. vdev_id %d", vdev_id);
		del_req = wma_fill_hold_req(wma, vdev_id, WMA_DELETE_STA_REQ,
					    WMA_DELETE_NDP_PEER_RSP,
					    del_sta->staMac, del_sta,
					    WMA_DELETE_STA_TIMEOUT);
		if (!del_req) {
			wma_err("Failed to allocate request for vdev_id %d",
				vdev_id);
			status = QDF_STATUS_E_NULL_VALUE;
			goto send_rsp;
		}

		return QDF_STATUS_SUCCESS;
	}

send_rsp:
	if (del_sta->respReqd) {
		wma_debug("Sending del rsp to umac (status: %d)",
			  del_sta->status);
		wma_send_msg_high_priority(wma, WMA_DELETE_STA_RSP, del_sta, 0);
	} else {
		wma_debug("NDI Del Sta resp not needed");
		qdf_mem_free(del_sta);
	}

	return status;
}
