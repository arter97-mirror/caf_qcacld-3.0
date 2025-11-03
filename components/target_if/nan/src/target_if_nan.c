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
 * DOC: contains nan target if functions
 */

#include "../../../nan/core/src/nan_main_i.h"
#include "nan_public_structs.h"
#include "nan_ucfg_api.h"
#include "target_if_nan.h"
#include "wlan_nan_api.h"
#include "wmi_unified_nan_api.h"
#include "target_if.h"
#include "wmi_unified_api.h"
#include "scheduler_api.h"
#include <wmi_unified.h>

static QDF_STATUS target_if_nan_event_flush_cb(struct scheduler_msg *msg)
{
	struct wlan_objmgr_psoc *psoc;

	if (!msg || !msg->bodyptr) {
		target_if_err("Empty message for NAN Discovery event");
		return QDF_STATUS_E_INVAL;
	}

	psoc = ((struct nan_event_params *)msg->bodyptr)->psoc;
	wlan_objmgr_psoc_release_ref(psoc, WLAN_NAN_ID);
	qdf_mem_free(msg->bodyptr);
	msg->bodyptr = NULL;

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS target_if_nan_event_dispatcher(struct scheduler_msg *msg)
{
	struct wlan_nan_rx_ops *nan_rx_ops;
	struct nan_event_params *nan_rsp;
	struct wlan_objmgr_psoc *psoc;
	QDF_STATUS status;

	nan_rsp = msg->bodyptr;
	psoc = nan_rsp->psoc;

	nan_rx_ops = nan_psoc_get_rx_ops(psoc);
	if (!nan_rx_ops) {
		target_if_err("nan_rx_ops is null");
		status = QDF_STATUS_E_NULL_VALUE;
	} else {
		status = nan_rx_ops->nan_discovery_event_rx(msg);
	}

	target_if_nan_event_flush_cb(msg);
	return status;
}

static QDF_STATUS target_if_ndp_event_flush_cb(struct scheduler_msg *msg)
{
	void *ptr = msg->bodyptr;
	struct wlan_objmgr_vdev *vdev = NULL;

	switch (msg->type) {
	case NDP_INITIATOR_RSP:
		vdev = ((struct nan_datapath_initiator_rsp *)ptr)->vdev;
		break;
	case NDP_INDICATION:
		vdev = ((struct nan_datapath_indication_event *)ptr)->vdev;
		break;
	case NDP_CONFIRM:
		vdev = ((struct nan_datapath_confirm_event *)ptr)->vdev;
		break;
	case NDP_RESPONDER_RSP:
		vdev = ((struct nan_datapath_responder_rsp *)ptr)->vdev;
		break;
	case NDP_END_RSP:
		vdev = ((struct nan_datapath_end_rsp_event *)ptr)->vdev;
		break;
	case NDP_END_IND:
		vdev = ((struct nan_datapath_end_indication_event *)ptr)->vdev;
		break;
	case NDP_SCHEDULE_UPDATE:
		vdev = ((struct nan_datapath_sch_update_event *)ptr)->vdev;
		break;
	case NDP_HOST_UPDATE:
		vdev = ((struct nan_datapath_host_event *)ptr)->vdev;
		break;
	default:
		break;
	}

	if (vdev)
		wlan_objmgr_vdev_release_ref(vdev, WLAN_NAN_ID);
	qdf_mem_free(msg->bodyptr);
	msg->bodyptr = NULL;

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS target_if_ndp_event_dispatcher(struct scheduler_msg *msg)
{
	QDF_STATUS status;
	void *ptr = msg->bodyptr;
	struct wlan_objmgr_psoc *psoc;
	struct wlan_objmgr_vdev *vdev = NULL;
	struct wlan_nan_rx_ops *nan_rx_ops;

	switch (msg->type) {
	case NDP_INITIATOR_RSP:
		vdev = ((struct nan_datapath_initiator_rsp *)ptr)->vdev;
		break;
	case NDP_INDICATION:
		vdev = ((struct nan_datapath_indication_event *)ptr)->vdev;
		break;
	case NDP_CONFIRM:
		vdev = ((struct nan_datapath_confirm_event *)ptr)->vdev;
		break;
	case NDP_RESPONDER_RSP:
		vdev = ((struct nan_datapath_responder_rsp *)ptr)->vdev;
		break;
	case NDP_END_RSP:
		vdev = ((struct nan_datapath_end_rsp_event *)ptr)->vdev;
		break;
	case NDP_END_IND:
		vdev = ((struct nan_datapath_end_indication_event *)ptr)->vdev;
		break;
	case NDP_SCHEDULE_UPDATE:
		vdev = ((struct nan_datapath_sch_update_event *)ptr)->vdev;
		break;
	case NDP_HOST_UPDATE:
		vdev = ((struct nan_datapath_host_event *)ptr)->vdev;
		break;
	default:
		target_if_err("invalid msg type %d", msg->type);
		status = QDF_STATUS_E_INVAL;
		goto free_res;
	}

	if (!vdev) {
		target_if_err("vdev is null");
		status = QDF_STATUS_E_NULL_VALUE;
		goto free_res;
	}

	psoc = wlan_vdev_get_psoc(vdev);
	if (!psoc) {
		target_if_err("psoc is null");
		status = QDF_STATUS_E_NULL_VALUE;
		goto free_res;
	}

	nan_rx_ops = nan_psoc_get_rx_ops(psoc);
	if (!nan_rx_ops) {
		target_if_err("nan_rx_ops is null");
		status = QDF_STATUS_E_NULL_VALUE;
		goto free_res;
	}

	status = nan_rx_ops->nan_datapath_event_rx(msg);
free_res:
	if (vdev)
		wlan_objmgr_vdev_release_ref(vdev, WLAN_NAN_ID);
	qdf_mem_free(msg->bodyptr);
	msg->bodyptr = NULL;
	return status;
}

static QDF_STATUS target_if_nan_ndp_initiator_req(
			struct nan_datapath_initiator_req *ndp_req)
{
	QDF_STATUS status;
	struct wmi_unified *wmi_handle;
	struct wlan_objmgr_psoc *psoc;
	struct scheduler_msg pe_msg = {0};
	struct wlan_nan_rx_ops *nan_rx_ops;
	struct nan_datapath_initiator_rsp ndp_rsp = {0};

	if (!ndp_req) {
		target_if_err("ndp_req is null.");
		return QDF_STATUS_E_INVAL;
	}

	psoc = wlan_vdev_get_psoc(ndp_req->vdev);
	if (!psoc) {
		target_if_err("psoc is null.");
		return QDF_STATUS_E_INVAL;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null.");
		return QDF_STATUS_E_INVAL;
	}

	nan_rx_ops = nan_psoc_get_rx_ops(psoc);
	if (!nan_rx_ops) {
		target_if_err("nan_rx_ops is null.");
		return QDF_STATUS_E_INVAL;
	}

	status = wmi_unified_ndp_initiator_req_cmd_send(wmi_handle, ndp_req);
	if (QDF_IS_STATUS_SUCCESS(status))
		return status;

	ndp_rsp.vdev = ndp_req->vdev;
	ndp_rsp.transaction_id = ndp_req->transaction_id;
	ndp_rsp.ndp_instance_id = ndp_req->service_instance_id;
	ndp_rsp.status = NAN_DATAPATH_DATA_INITIATOR_REQ_FAILED;
	pe_msg.type = NDP_INITIATOR_RSP;
	pe_msg.bodyptr = &ndp_rsp;
	if (nan_rx_ops->nan_datapath_event_rx)
		nan_rx_ops->nan_datapath_event_rx(&pe_msg);

	return status;
}

static int target_if_nan_dmesg_handler(ol_scn_t scn, uint8_t *data,
				       uint32_t data_len)
{
	QDF_STATUS status;
	struct nan_dump_msg msg;
	struct wmi_unified *wmi_handle;
	struct wlan_objmgr_psoc *psoc;

	psoc = target_if_get_psoc_from_scn_hdl(scn);
	if (!psoc) {
		target_if_err("psoc is null");
		return -EINVAL;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null");
		return -EINVAL;
	}

	status = wmi_extract_nan_msg(wmi_handle, data, &msg);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("parsing of event failed, %d", status);
		return -EINVAL;
	}

	if (!msg.msg) {
		target_if_err("msg not present %d", msg.data_len);
		return -EINVAL;
	}

	target_if_info("%s", msg.msg);

	return 0;
}

#if defined(WLAN_FEATURE_NAN) && defined(FEATURE_WLAN_SUPPORT_NAN_STANDARD_MODE)
/**
 * target_if_nan_disable_rsp_handler() - Handler for NAN disable response event
 * @scn: scn handle
 * @data: Event data buffer from firmware
 * @len: Length of event data
 *
 * This function handles the WMI_NAN_DISABLE_RSP_EVENTID event from firmware.
 * It extracts the NAN disable response parameters, allocates memory for the
 * event structure, obtains a PSOC reference, and posts the event to the
 * scheduler for processing by the NAN discovery event dispatcher.
 *
 * Return: 0 on success, negative error code on failure
 */
static int target_if_nan_disable_rsp_handler(ol_scn_t scn, uint8_t *data,
					     uint32_t len)
{
	struct nan_event_params *nan_rsp, temp_evt_params = {0};
	struct scheduler_msg msg = {0};
	struct wmi_unified *wmi_handle;
	struct wlan_objmgr_psoc *psoc;
	QDF_STATUS status;

	psoc = target_if_get_psoc_from_scn_hdl(scn);
	if (!psoc) {
		target_if_err("psoc is null");
		return -EINVAL;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null");
		return -EINVAL;
	}

	status = wmi_extract_nan_disable_rsp_event(wmi_handle, data,
						   &temp_evt_params);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("parsing of event failed, %d", status);
		return -EINVAL;
	}

	nan_rsp = qdf_mem_malloc(sizeof(*nan_rsp) + temp_evt_params.buf_len);
	if (!nan_rsp)
		return -ENOMEM;

	qdf_mem_copy(nan_rsp, &temp_evt_params, sizeof(*nan_rsp));
	if (temp_evt_params.buf_len)
		qdf_mem_copy(nan_rsp->buf, temp_evt_params.buf,
			     temp_evt_params.buf_len);

	status = wlan_objmgr_psoc_try_get_ref(psoc, WLAN_NAN_ID);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("Failed to obtain psoc ref");
		qdf_mem_free(nan_rsp);
		return -EACCES;
	}

	nan_rsp->psoc = psoc;

	msg.bodyptr = nan_rsp;
	msg.type = nan_rsp->evt_type;
	msg.callback = target_if_nan_event_dispatcher;
	msg.flush_callback = target_if_nan_event_flush_cb;
	target_if_debug("NAN Event sent: %d", msg.type);
	status = scheduler_post_message(QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF, &msg);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("failed to post msg, status: %d", status);
		target_if_nan_event_flush_cb(&msg);
		return -EINVAL;
	}

	return 0;
}

/**
 * target_if_nan_disable_ind_handler() - Handler for NAN disable indication event
 * @scn: scn handle
 * @data: Event data buffer from firmware
 * @len: Length of event data
 *
 * This function handles the WMI_NAN_DISABLE_IND_EVENTID event from firmware.
 * It extracts the NAN disable indication parameters, allocates memory for the
 * event structure, obtains a PSOC reference, and posts the event to the
 * scheduler for processing by the NAN discovery event dispatcher. This
 * indication is sent by firmware when NAN is disabled autonomously.
 *
 * Return: 0 on success, negative error code on failure
 */
static int target_if_nan_disable_ind_handler(ol_scn_t scn, uint8_t *data,
					     uint32_t len)
{
	struct nan_event_params *nan_rsp, temp_evt_params = {0};
	struct scheduler_msg msg = {0};
	struct wmi_unified *wmi_handle;
	struct wlan_objmgr_psoc *psoc;
	QDF_STATUS status;

	psoc = target_if_get_psoc_from_scn_hdl(scn);
	if (!psoc) {
		target_if_err("psoc is null");
		return -EINVAL;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null");
		return -EINVAL;
	}

	status = wmi_extract_nan_disable_ind_event(wmi_handle, data,
						   &temp_evt_params);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("parsing of event failed, %d", status);
		return -EINVAL;
	}

	nan_rsp = qdf_mem_malloc(sizeof(*nan_rsp) + temp_evt_params.buf_len);
	if (!nan_rsp)
		return -ENOMEM;

	qdf_mem_copy(nan_rsp, &temp_evt_params, sizeof(*nan_rsp));
	if (temp_evt_params.buf_len)
		qdf_mem_copy(nan_rsp->buf, temp_evt_params.buf,
			     temp_evt_params.buf_len);

	status = wlan_objmgr_psoc_try_get_ref(psoc, WLAN_NAN_ID);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("Failed to obtain psoc ref");
		qdf_mem_free(nan_rsp);
		return -EACCES;
	}
	nan_rsp->psoc = psoc;

	msg.bodyptr = nan_rsp;
	msg.type = nan_rsp->evt_type;
	msg.callback = target_if_nan_event_dispatcher;
	msg.flush_callback = target_if_nan_event_flush_cb;
	target_if_debug("NAN Event sent: %d", msg.type);
	status = scheduler_post_message(QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF, &msg);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("failed to post msg, status: %d", status);
		target_if_nan_event_flush_cb(&msg);
		return -EINVAL;
	}

	return 0;
}

static int target_if_nan_enable_rsp_handler(ol_scn_t scn, uint8_t *data,
					    uint32_t len)
{
	struct nan_event_params *nan_rsp;
	struct nan_enable_rsp_params fw_evt_params = {0};
	struct scheduler_msg msg = {0};
	struct wmi_unified *wmi_handle;
	struct wlan_objmgr_psoc *psoc;
	QDF_STATUS status;

	psoc = target_if_get_psoc_from_scn_hdl(scn);
	if (!psoc) {
		target_if_err("psoc is null");
		return -EINVAL;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null");
		return -EINVAL;
	}

	status = wmi_extract_nan_enable_rsp_event(wmi_handle, data,
						  &fw_evt_params);

	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("parsing of event failed, %d", status);
		return -EINVAL;
	}
	nan_rsp = qdf_mem_malloc(sizeof(*nan_rsp));
	if (!nan_rsp)
		return -ENOMEM;

	status = wlan_objmgr_psoc_try_get_ref(psoc, WLAN_NAN_ID);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("Failed to obtain psoc ref");
		qdf_mem_free(nan_rsp);
		return -EACCES;
	}
	nan_rsp->evt_type = nan_event_id_enable_rsp;
	nan_rsp->is_nan_enable_success = (fw_evt_params.status == 0);
	nan_rsp->vdev_id = fw_evt_params.vdev_id;
	nan_rsp->mac_id = fw_evt_params.mac_id;
	nan_rsp->psoc = psoc;
	msg.bodyptr = nan_rsp;
	msg.type = nan_rsp->evt_type;
	msg.callback = target_if_nan_event_dispatcher;
	msg.flush_callback = target_if_nan_event_flush_cb;
	target_if_debug("NAN Event sent: %d", msg.type);
	status = scheduler_post_message(QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF, &msg);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("failed to post msg, status: %d", status);
		target_if_nan_event_flush_cb(&msg);
		return -EINVAL;
	}

	return 0;
}
#endif
static int target_if_ndp_initiator_rsp_handler(ol_scn_t scn, uint8_t *data,
						uint32_t len)
{
	QDF_STATUS status;
	struct wmi_unified *wmi_handle;
	struct wlan_objmgr_psoc *psoc;
	struct scheduler_msg msg = {0};
	struct nan_datapath_initiator_rsp *rsp;

	psoc = target_if_get_psoc_from_scn_hdl(scn);
	if (!psoc) {
		target_if_err("psoc is null");
		return -EINVAL;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null");
		return -EINVAL;
	}

	rsp = qdf_mem_malloc(sizeof(*rsp));
	if (!rsp)
		return -ENOMEM;

	status = wmi_extract_ndp_initiator_rsp(wmi_handle, data, rsp);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("parsing of event failed, %d", status);
		qdf_mem_free(rsp);
		return -EINVAL;
	}

	msg.bodyptr = rsp;
	msg.type = NDP_INITIATOR_RSP;
	msg.callback = target_if_ndp_event_dispatcher;
	msg.flush_callback = target_if_ndp_event_flush_cb;
	target_if_debug("NDP_INITIATOR_RSP sent: %d", msg.type);
	status = scheduler_post_message(QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF, &msg);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_ndp_event_flush_cb(&msg);
		return -EINVAL;
	}

	return 0;
}

static int target_if_ndp_ind_handler(ol_scn_t scn, uint8_t *data,
				     uint32_t data_len)
{
	QDF_STATUS status;
	struct wlan_objmgr_psoc *psoc;
	struct wmi_unified *wmi_handle;
	struct scheduler_msg msg = {0};
	struct nan_datapath_indication_event *rsp;

	psoc = target_if_get_psoc_from_scn_hdl(scn);
	if (!psoc) {
		target_if_err("psoc is null");
		return -EINVAL;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null");
		return -EINVAL;
	}

	rsp = qdf_mem_malloc(sizeof(*rsp));
	if (!rsp)
		return -ENOMEM;

	status = wmi_extract_ndp_ind(wmi_handle, data, rsp);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("parsing of event failed, %d", status);
		qdf_mem_free(rsp);
		return -EINVAL;
	}

	msg.bodyptr = rsp;
	msg.type = NDP_INDICATION;
	msg.callback = target_if_ndp_event_dispatcher;
	msg.flush_callback = target_if_ndp_event_flush_cb;
	target_if_debug("NDP_INDICATION sent: %d", msg.type);
	status = scheduler_post_message(QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF, &msg);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_ndp_event_flush_cb(&msg);
		return -EINVAL;
	}

	return 0;
}

static int target_if_ndp_confirm_handler(ol_scn_t scn, uint8_t *data,
					uint32_t data_len)
{
	QDF_STATUS status;
	struct wlan_objmgr_psoc *psoc;
	struct wmi_unified *wmi_handle;
	struct scheduler_msg msg = {0};
	struct nan_datapath_confirm_event *rsp;
	uint8_t i;

	psoc = target_if_get_psoc_from_scn_hdl(scn);
	if (!psoc) {
		target_if_err("psoc is null");
		return -EINVAL;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null");
		return -EINVAL;
	}

	rsp = qdf_mem_malloc(sizeof(*rsp));
	if (!rsp)
		return -ENOMEM;

	status = wmi_extract_ndp_confirm(wmi_handle, data, rsp);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("parsing of event failed, %d", status);
		qdf_mem_free(rsp);
		return -EINVAL;
	}

	for (i = 0; i < rsp->num_channels; i++)
		rsp->ch[i].ch_width =
		  target_if_wmi_chan_width_to_phy_ch_width(rsp->ch[i].ch_width);

	msg.bodyptr = rsp;
	msg.type = NDP_CONFIRM;
	msg.callback = target_if_ndp_event_dispatcher;
	msg.flush_callback = target_if_ndp_event_flush_cb;
	target_if_debug("NDP_CONFIRM sent: %d", msg.type);
	status = scheduler_post_message(QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF, &msg);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_ndp_event_flush_cb(&msg);
		return -EINVAL;
	}

	return 0;
}

static QDF_STATUS target_if_nan_ndp_responder_req(
				struct nan_datapath_responder_req *req)
{
	QDF_STATUS status;
	struct wmi_unified *wmi_handle;
	struct wlan_objmgr_psoc *psoc;
	struct scheduler_msg pe_msg = {0};
	struct wlan_nan_rx_ops *nan_rx_ops;
	struct nan_datapath_responder_rsp rsp = {0};

	if (!req) {
		target_if_err("Invalid req.");
		return QDF_STATUS_E_INVAL;
	}

	psoc = wlan_vdev_get_psoc(req->vdev);
	if (!psoc) {
		target_if_err("psoc is null.");
		return QDF_STATUS_E_NULL_VALUE;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null.");
		return QDF_STATUS_E_NULL_VALUE;
	}

	nan_rx_ops = nan_psoc_get_rx_ops(psoc);
	if (!nan_rx_ops) {
		target_if_err("nan_rx_ops is null.");
		return QDF_STATUS_E_NULL_VALUE;
	}

	status = wmi_unified_ndp_responder_req_cmd_send(wmi_handle, req);
	if (QDF_IS_STATUS_SUCCESS(status))
		return status;

	rsp.vdev = req->vdev;
	rsp.transaction_id = req->transaction_id;
	rsp.status = NAN_DATAPATH_RSP_STATUS_ERROR;
	rsp.reason = NAN_DATAPATH_DATA_RESPONDER_REQ_FAILED;
	pe_msg.bodyptr = &rsp;
	pe_msg.type = NDP_RESPONDER_RSP;
	if (nan_rx_ops->nan_datapath_event_rx)
		nan_rx_ops->nan_datapath_event_rx(&pe_msg);

	return status;
}

static int target_if_ndp_responder_rsp_handler(ol_scn_t scn, uint8_t *data,
						uint32_t len)
{
	QDF_STATUS status;
	struct wlan_objmgr_psoc *psoc;
	struct wmi_unified *wmi_handle;
	struct scheduler_msg msg = {0};
	struct nan_datapath_responder_rsp *rsp;

	psoc = target_if_get_psoc_from_scn_hdl(scn);
	if (!psoc) {
		target_if_err("psoc is null");
		return -EINVAL;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null.");
		return -EINVAL;
	}

	rsp = qdf_mem_malloc(sizeof(*rsp));
	if (!rsp)
		return -ENOMEM;

	status = wmi_extract_ndp_responder_rsp(wmi_handle, data, rsp);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("parsing of event failed, %d", status);
		qdf_mem_free(rsp);
		return -EINVAL;
	}

	msg.bodyptr = rsp;
	msg.type = NDP_RESPONDER_RSP;
	msg.callback = target_if_ndp_event_dispatcher;
	msg.flush_callback = target_if_ndp_event_flush_cb;
	target_if_debug("NDP_INITIATOR_RSP sent: %d", msg.type);
	status = scheduler_post_message(QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF, &msg);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_ndp_event_flush_cb(&msg);
		return -EINVAL;
	}

	return 0;
}

static QDF_STATUS target_if_nan_ndp_end_req(struct nan_datapath_end_req *req)
{
	QDF_STATUS status;
	struct wmi_unified *wmi_handle;
	struct wlan_objmgr_psoc *psoc;
	struct scheduler_msg msg = {0};
	struct wlan_nan_rx_ops *nan_rx_ops;
	struct nan_datapath_end_rsp_event end_rsp = {0};

	if (!req) {
		target_if_err("req is null");
		return QDF_STATUS_E_INVAL;
	}

	psoc = wlan_vdev_get_psoc(req->vdev);
	if (!psoc) {
		target_if_err("psoc is null.");
		return QDF_STATUS_E_NULL_VALUE;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null.");
		return QDF_STATUS_E_NULL_VALUE;
	}

	nan_rx_ops = nan_psoc_get_rx_ops(psoc);
	if (!nan_rx_ops) {
		target_if_err("nan_rx_ops is null.");
		return QDF_STATUS_E_NULL_VALUE;
	}

	status = wmi_unified_ndp_end_req_cmd_send(wmi_handle, req);
	if (QDF_IS_STATUS_SUCCESS(status))
		return status;

	end_rsp.vdev = req->vdev;
	msg.type = NDP_END_RSP;
	end_rsp.status = NAN_DATAPATH_RSP_STATUS_ERROR;
	end_rsp.reason = NAN_DATAPATH_END_FAILED;
	end_rsp.transaction_id = req->transaction_id;
	msg.bodyptr = &end_rsp;

	if (nan_rx_ops->nan_datapath_event_rx)
		nan_rx_ops->nan_datapath_event_rx(&msg);

	return status;
}

static int target_if_ndp_end_rsp_handler(ol_scn_t scn, uint8_t *data,
					 uint32_t data_len)
{
	QDF_STATUS status;
	struct wlan_objmgr_psoc *psoc;
	struct wmi_unified *wmi_handle;
	struct scheduler_msg msg = {0};
	struct nan_datapath_end_rsp_event *end_rsp;

	psoc = target_if_get_psoc_from_scn_hdl(scn);
	if (!psoc) {
		target_if_err("psoc is null");
		return -EINVAL;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null.");
		return -EINVAL;
	}

	end_rsp = qdf_mem_malloc(sizeof(*end_rsp));
	if (!end_rsp)
		return -ENOMEM;

	status = wmi_extract_ndp_end_rsp(wmi_handle, data, end_rsp);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("parsing of event failed, %d", status);
		qdf_mem_free(end_rsp);
		return -EINVAL;
	}

	msg.bodyptr = end_rsp;
	msg.type = NDP_END_RSP;
	msg.callback = target_if_ndp_event_dispatcher;
	msg.flush_callback = target_if_ndp_event_flush_cb;
	target_if_debug("NDP_END_RSP sent: %d", msg.type);
	status = scheduler_post_message(QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF, &msg);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_ndp_event_flush_cb(&msg);
		return -EINVAL;
	}

	return 0;
}

static QDF_STATUS target_if_nan_ndp_update_config(
				struct nan_datapath_update_config *config)
{
	struct wlan_objmgr_vdev *vdev;
	struct wmi_unified *wmi_handle;
	struct wlan_objmgr_psoc *psoc;

	if (!config) {
		target_if_err("Invalid config.");
		return QDF_STATUS_E_INVAL;
	}

	vdev = config->vdev;
	if (!vdev) {
		target_if_err("vdev object is NULL!");
		return QDF_STATUS_E_INVAL;
	}

	psoc = wlan_vdev_get_psoc(vdev);
	if (!psoc) {
		target_if_err("psoc is null.");
		return QDF_STATUS_E_NULL_VALUE;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null.");
		return QDF_STATUS_E_NULL_VALUE;
	}

	return wmi_unified_ndp_update_config_cmd_send(wmi_handle, config);
}

static int target_if_ndp_end_ind_handler(ol_scn_t scn, uint8_t *data,
					 uint32_t data_len)
{
	QDF_STATUS status;
	struct wlan_objmgr_psoc *psoc;
	struct wmi_unified *wmi_handle;
	struct scheduler_msg msg = {0};
	struct nan_datapath_end_indication_event *rsp = NULL;

	psoc = target_if_get_psoc_from_scn_hdl(scn);
	if (!psoc) {
		target_if_err("psoc is null");
		return -EINVAL;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null.");
		return -EINVAL;
	}

	status = wmi_extract_ndp_end_ind(wmi_handle, data, &rsp);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("parsing of event failed, %d", status);
		return -EINVAL;
	}

	rsp->vdev = wlan_objmgr_get_vdev_by_opmode_from_psoc(
			  wmi_handle->soc->wmi_psoc, QDF_NDI_MODE, WLAN_NAN_ID);
	if (!rsp->vdev) {
		target_if_err("vdev is null");
		qdf_mem_free(rsp);
		return -EINVAL;
	}

	msg.bodyptr = rsp;
	msg.type = NDP_END_IND;
	msg.callback = target_if_ndp_event_dispatcher;
	msg.flush_callback = target_if_ndp_event_flush_cb;
	target_if_debug("NDP_END_IND sent: %d", msg.type);
	status = scheduler_post_message(QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF, &msg);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_ndp_event_flush_cb(&msg);
		return -EINVAL;
	}

	return 0;
}

static int target_if_ndp_sch_update_handler(ol_scn_t scn, uint8_t *data,
					    uint32_t data_len)
{
	QDF_STATUS status;
	struct wlan_objmgr_psoc *psoc;
	struct wmi_unified *wmi_handle;
	struct scheduler_msg msg = {0};
	struct nan_datapath_sch_update_event *rsp;
	uint8_t i;

	psoc = target_if_get_psoc_from_scn_hdl(scn);
	if (!psoc) {
		target_if_err("psoc is null");
		return -EINVAL;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null.");
		return -EINVAL;
	}

	rsp = qdf_mem_malloc(sizeof(*rsp));
	if (!rsp)
		return -ENOMEM;

	status = wmi_extract_ndp_sch_update(wmi_handle, data, rsp);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("parsing of event failed, %d", status);
		qdf_mem_free(rsp);
		return -EINVAL;
	}

	for (i = 0; i < rsp->num_channels; i++)
		rsp->ch[i].ch_width =
		  target_if_wmi_chan_width_to_phy_ch_width(rsp->ch[i].ch_width);

	msg.bodyptr = rsp;
	msg.type = NDP_SCHEDULE_UPDATE;
	msg.callback = target_if_ndp_event_dispatcher;
	msg.flush_callback = target_if_ndp_event_flush_cb;
	target_if_debug("NDP_SCHEDULE_UPDATE sent: %d", msg.type);
	status = scheduler_post_message(QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF, &msg);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_ndp_event_flush_cb(&msg);
		return -EINVAL;
	}

	return 0;
}

static QDF_STATUS target_if_nan_end_all_ndps_req(void *req)
{
	struct wlan_objmgr_vdev *vdev;
	struct wlan_objmgr_psoc *psoc;
	struct wmi_unified *wmi_handle;
	uint8_t vdev_id;

	vdev = ((struct nan_datapath_end_all_ndps *)req)->vdev;
	if (!vdev) {
		target_if_err("vdev object is NULL!");
		return QDF_STATUS_E_INVAL;
	}
	vdev_id = wlan_vdev_get_id(vdev);

	psoc = wlan_vdev_get_psoc(vdev);
	if (!psoc) {
		target_if_err("psoc is null.");
		return QDF_STATUS_E_NULL_VALUE;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null.");
		return QDF_STATUS_E_NULL_VALUE;
	}

	return wmi_unified_terminate_all_ndps_req_cmd(wmi_handle, vdev_id);
}

static int target_if_ndp_host_event_handler(ol_scn_t scn, uint8_t *data,
					    uint32_t data_len)
{
	QDF_STATUS status;
	struct wlan_objmgr_psoc *psoc;
	struct wmi_unified *wmi_handle;
	struct scheduler_msg msg = {0};
	struct nan_datapath_host_event *host_evt = NULL;

	psoc = target_if_get_psoc_from_scn_hdl(scn);
	if (!psoc) {
		target_if_err("psoc is null");
		return -EINVAL;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null.");
		return -EINVAL;
	}

	host_evt = qdf_mem_malloc(sizeof(*host_evt));
	if (!host_evt)
		return -ENOMEM;

	status = wmi_extract_ndp_host_event(wmi_handle, data, host_evt);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("parsing of event failed, %d", status);
		qdf_mem_free(host_evt);
		return -EINVAL;
	}

	if (!host_evt->vdev) {
		target_if_err("vdev is null");
		qdf_mem_free(host_evt);
		return -EINVAL;
	}

	msg.bodyptr = host_evt;
	msg.type = NDP_HOST_UPDATE;
	msg.callback = target_if_ndp_event_dispatcher;
	msg.flush_callback = target_if_ndp_event_flush_cb;
	target_if_debug("NDP_HOST_UPDATE sent: %d", msg.type);
	status = scheduler_post_message(QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF, &msg);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("failed to post msg, status: %d", status);
		target_if_ndp_event_flush_cb(&msg);
		return -EINVAL;
	}

	return 0;
}

static QDF_STATUS target_if_nan_datapath_req(void *req, uint32_t req_type)
{
	/* send cmd to fw */
	switch (req_type) {
	case NDP_INITIATOR_REQ:
		target_if_nan_ndp_initiator_req(req);
		break;
	case NDP_RESPONDER_REQ:
		target_if_nan_ndp_responder_req(req);
		break;
	case NDP_END_REQ:
		target_if_nan_ndp_end_req(req);
		break;
	case NDP_END_ALL:
		target_if_nan_end_all_ndps_req(req);
		break;
	case NDP_UPDATE_CONFIG:
		target_if_nan_ndp_update_config(req);
		break;
	default:
		target_if_err("invalid req type");
		break;
	}
	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS target_if_nan_generic_req(struct wlan_objmgr_psoc *psoc,
					    void *nan_req)
{
	struct wmi_unified *wmi_handle;

	if (!psoc) {
		target_if_err("psoc is null.");
		return QDF_STATUS_E_NULL_VALUE;
	}

	if (!nan_req) {
		target_if_err("Invalid req.");
		return QDF_STATUS_E_INVAL;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null.");
		return QDF_STATUS_E_NULL_VALUE;
	}
	return wmi_unified_nan_req_cmd(wmi_handle, nan_req);
}

#if defined(WLAN_FEATURE_NAN) && defined(FEATURE_WLAN_SUPPORT_NAN_STANDARD_MODE)
static QDF_STATUS target_if_nan_standard_req(struct wlan_objmgr_psoc *psoc,
					     void *nan_req)
{
	struct wmi_unified *wmi_handle;

	if (!psoc) {
		target_if_err("psoc is null.");
		return QDF_STATUS_E_NULL_VALUE;
	}
	if (!nan_req) {
		target_if_err("Invalid req.");
		return QDF_STATUS_E_INVAL;
	}
	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null.");
		return QDF_STATUS_E_NULL_VALUE;
	}
	return wmi_unified_nan_start_req(wmi_handle, nan_req);
}
#else
static QDF_STATUS target_if_nan_standard_req(struct wlan_objmgr_psoc *psoc,
					     void *nan_req)
{
	return QDF_STATUS_E_INVAL;
}
#endif

static QDF_STATUS target_if_nan_disable_req(struct nan_disable_req *nan_req)
{
	struct wmi_unified *wmi_handle;
	struct wlan_objmgr_psoc *psoc;

	if (!nan_req) {
		target_if_err("Invalid req.");
		return QDF_STATUS_E_INVAL;
	}
	psoc = nan_req->psoc;

	if (!psoc) {
		target_if_err("psoc is null.");
		return QDF_STATUS_E_NULL_VALUE;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null.");
		return QDF_STATUS_E_NULL_VALUE;
	}

	if (ucfg_nan_is_fw_support_standard_mode(psoc))
		return wmi_unified_nan_stop_req_cmd(wmi_handle, nan_req);
	else
		return wmi_unified_nan_disable_req_cmd(wmi_handle, nan_req);
}

static QDF_STATUS target_if_nan_discovery_req(void *req, uint32_t req_type)
{
	QDF_STATUS status;

	if (!req) {
		target_if_err("Invalid req.");
		return QDF_STATUS_E_INVAL;
	}

	switch (req_type) {
	case NAN_DISABLE_REQ:
		status = target_if_nan_disable_req(req);
		break;
	case NAN_GENERIC_REQ: {
			struct nan_generic_req *nan_req = req;

			status = target_if_nan_generic_req(nan_req->psoc,
							   &nan_req->params);
			break;
		}
	case NAN_ENABLE_REQ: {
			struct nan_enable_req *nan_req = req;
			if (ucfg_nan_is_fw_support_standard_mode(nan_req->psoc))
				status = target_if_nan_standard_req(
								nan_req->psoc,
								nan_req);
			else
				status = target_if_nan_generic_req(
							nan_req->psoc,
							&nan_req->params);
			break;
		}
	default:
		target_if_err("Invalid NAN req type");
		status = QDF_STATUS_E_INVAL;
		break;
	}

	return status;
}

void target_if_nan_register_tx_ops(struct wlan_nan_tx_ops *tx_ops)
{
	tx_ops->nan_discovery_req_tx = target_if_nan_discovery_req;
	tx_ops->nan_datapath_req_tx = target_if_nan_datapath_req;
}

void target_if_nan_register_rx_ops(struct wlan_nan_rx_ops *rx_ops)
{
	rx_ops->nan_discovery_event_rx = nan_discovery_event_handler;
	rx_ops->nan_datapath_event_rx = nan_datapath_event_handler;
}

int target_if_nan_rsp_handler(ol_scn_t scn, uint8_t *data, uint32_t len)
{
	struct nan_event_params *nan_rsp, temp_evt_params = {0};
	struct scheduler_msg msg = {0};
	struct wmi_unified *wmi_handle;
	struct wlan_objmgr_psoc *psoc;
	QDF_STATUS status;
	uint8_t *buf_ptr;

	psoc = target_if_get_psoc_from_scn_hdl(scn);
	if (!psoc) {
		target_if_err("psoc is null");
		return -EINVAL;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null");
		return -EINVAL;
	}

	status = wmi_extract_nan_event_rsp(wmi_handle, data, &temp_evt_params,
					   &buf_ptr, wlan_get_nan_config(psoc));
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("parsing of event failed, %d", status);
		return -EINVAL;
	}

	nan_rsp = qdf_mem_malloc(sizeof(*nan_rsp) + temp_evt_params.buf_len);
	if (!nan_rsp)
		return -ENOMEM;

	qdf_mem_copy(nan_rsp, &temp_evt_params, sizeof(*nan_rsp));

	status = wlan_objmgr_psoc_try_get_ref(psoc, WLAN_NAN_ID);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("Failed to obtain psoc ref");
		return -EACCES;
	}

	nan_rsp->psoc = psoc;
	qdf_mem_copy(nan_rsp->buf, buf_ptr, nan_rsp->buf_len);

	msg.bodyptr = nan_rsp;
	msg.type = nan_rsp->evt_type;
	msg.callback = target_if_nan_event_dispatcher;
	msg.flush_callback = target_if_nan_event_flush_cb;
	target_if_debug("NAN Event sent: %d", msg.type);
	status = scheduler_post_message(QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF,
					QDF_MODULE_ID_TARGET_IF, &msg);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("failed to post msg, status: %d", status);
		target_if_nan_event_flush_cb(&msg);
		return -EINVAL;
	}

	return 0;
}

#if defined(WLAN_FEATURE_NAN) && defined(FEATURE_WLAN_SUPPORT_NAN_STANDARD_MODE)
/**
 * target_if_nan_join_cluster_event_handler() - Handler for NAN cluster events
 * @scn: scn handle
 * @data: event data buffer
 * @datalen: length of event data
 *
 * This function handles both WMI_NAN_JOINED_CLUSTER_EVENTID and
 * WMI_NAN_STARTED_CLUSTER_EVENTID events from firmware.
 *
 * Return: 0 on success, negative error code on failure
 */
static
int target_if_nan_join_cluster_event_handler(ol_scn_t scn, uint8_t *data,
					     uint32_t datalen)
{
	struct wlan_objmgr_psoc *psoc;
	struct wlan_objmgr_vdev *vdev;
	struct nan_cluster_event cluster_event;
	struct nan_psoc_priv_obj *nan_psoc_obj;
	wmi_unified_t wmi_handle;
	QDF_STATUS status;

	if (!scn || !data) {
		target_if_err("Invalid parameters");
		return -EINVAL;
	}

	psoc = target_if_get_psoc_from_scn_hdl(scn);

	nan_psoc_obj = nan_get_psoc_priv_obj(psoc);
	if (!nan_psoc_obj) {
		target_if_err("nan PSOC is NULL");
		return -EINVAL;
	}

	if (!nan_psoc_obj->cb_obj.os_if_nan_process_cluster_event) {
		target_if_err("function handler is null");
		return -EINVAL;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null");
		return -EINVAL;
	}

	cluster_event.event_type = NAN_CLUSTER_EVENT_JOINED;
	/* Extract cluster event information from WMI buffer */
	status = wmi_extract_nan_cluster_event(wmi_handle, data,
					       &cluster_event);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("Failed to extract cluster event: %d", status);
		return -EINVAL;
	}

	/* Get vdev object */
	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(psoc,
						    cluster_event.vdev_id,
						    WLAN_NAN_ID);
	if (!vdev) {
		target_if_err("vdev is null for vdev_id: %d",
			      cluster_event.vdev_id);
		return -EINVAL;
	}

	nan_psoc_obj->cb_obj.os_if_nan_process_cluster_event(vdev,
							     &cluster_event);

	/* Release vdev reference */
	wlan_objmgr_vdev_release_ref(vdev, WLAN_NAN_ID);

	return qdf_status_to_os_return(status);
}

/**
 * target_if_nan_start_cluster_event_handler() - Handler for NAN cluster events
 * @scn: scn handle
 * @data: event data buffer
 * @datalen: length of event data
 *
 * This function handles both WMI_NAN_JOINED_CLUSTER_EVENTID and
 * WMI_NAN_STARTED_CLUSTER_EVENTID events from firmware.
 *
 * Return: 0 on success, negative error code on failure
 */
static
int target_if_nan_start_cluster_event_handler(ol_scn_t scn, uint8_t *data,
                                              uint32_t datalen)
{
	struct wlan_objmgr_psoc *psoc;
	struct wlan_objmgr_vdev *vdev;
	struct nan_cluster_event cluster_event;
	struct nan_psoc_priv_obj *nan_psoc_obj;
	wmi_unified_t wmi_handle;
	QDF_STATUS status;

	if (!scn || !data) {
		target_if_err("Invalid parameters");
		return -EINVAL;
	}

	psoc = target_if_get_psoc_from_scn_hdl(scn);

	nan_psoc_obj = nan_get_psoc_priv_obj(psoc);
	if (!nan_psoc_obj) {
		target_if_err("nan PSOC is NULL");
		return -EINVAL;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("wmi_handle is null");
		return -EINVAL;
	}

	cluster_event.event_type = NAN_CLUSTER_EVENT_STARTED;
	/* Extract cluster event information from WMI buffer */
	status = wmi_extract_nan_cluster_event(wmi_handle,
					       data, &cluster_event);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("Failed to extract cluster event: %d", status);
		return -EINVAL;
	}

	/* Get vdev object */
	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(psoc,
						    cluster_event.vdev_id,
						    WLAN_NAN_ID);
	if (!vdev) {
		target_if_err("vdev is null for vdev_id: %d",
			      cluster_event.vdev_id);
		return -EINVAL;
	}

	if (!nan_psoc_obj->cb_obj.os_if_nan_process_cluster_event) {
		target_if_err("function handler is null");
		wlan_objmgr_vdev_release_ref(vdev, WLAN_NAN_ID);
		return -EINVAL;
	}
	nan_psoc_obj->cb_obj.os_if_nan_process_cluster_event(vdev,
							     &cluster_event);

	/* Release vdev reference */
	wlan_objmgr_vdev_release_ref(vdev, WLAN_NAN_ID);

	return qdf_status_to_os_return(status);
}

/**
 * target_if_nan_next_dw_info_event_handler() - Handler for NAN next DW info event
 * @scn: Opaque SOC handle
 * @data: Event data from firmware
 * @datalen: Length of event data
 *
 * This function handles the WMI_NAN_NEXT_DW_INFO_EVENTID event from firmware
 * and forwards it to the UMAC layer for further processing.
 *
 * Return: 0 on success, negative error code on failure
 */
static int target_if_nan_next_dw_info_event_handler(ol_scn_t scn, uint8_t *data,
						    uint32_t datalen)
{
	struct wlan_objmgr_psoc *psoc;
	struct wlan_objmgr_vdev *vdev;
	struct nan_next_dw_info_event event;
	QDF_STATUS status;
	wmi_unified_t wmi_handle;
	struct nan_psoc_priv_obj *nan_psoc_obj;

	if (!scn || !data) {
		target_if_err("Invalid parameters");
		return -EINVAL;
	}

	psoc = target_if_get_psoc_from_scn_hdl(scn);

	nan_psoc_obj = nan_get_psoc_priv_obj(psoc);
	if (!nan_psoc_obj) {
		target_if_err("nan PSOC is NULL");
		return -EINVAL;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("WMI handle is NULL");
		return -EINVAL;
	}

	/* Extract event parameters from WMI */
	qdf_mem_zero(&event, sizeof(event));
	status = wmi_extract_nan_next_dw_info(wmi_handle, data, &event);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("Failed to extract NAN next DW info: %d", status);
		return -EINVAL;
	}

	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(psoc, event.vdev_id,
                                                    WLAN_NAN_ID);
	if (!vdev) {
		target_if_err("vdev is null");
		return -EINVAL;
        }

	if (!nan_psoc_obj->cb_obj.os_if_nan_next_dw_notif_handler) {
		target_if_err("function handler is null");
		wlan_objmgr_vdev_release_ref(vdev, WLAN_NAN_ID);
		return -EINVAL;
	}

	nan_psoc_obj->cb_obj.os_if_nan_next_dw_notif_handler(vdev, &event);

	wlan_objmgr_vdev_release_ref(vdev, WLAN_NAN_ID);

	return qdf_status_to_os_return(status);
}

/**
 * target_if_deregister_nan_std_mode_event_handler() - Deregister NAN std mode
 *						       events
 * @psoc: psoc pointer
 *
 * This function deregisters nan std mode events.
 *
 * Return: QDF_STATUS_SUCCESS on success, QDF_STATUS_E_* on failure
 */
static QDF_STATUS
target_if_deregister_nan_std_mode_event_handler(struct wlan_objmgr_psoc *psoc)
{
	QDF_STATUS ret, status = QDF_STATUS_SUCCESS;
	wmi_unified_t handle = get_wmi_unified_hdl_from_psoc(psoc);

	if (!handle) {
		target_if_err("handle is NULL");
		return QDF_STATUS_E_FAILURE;
	}

	ret = wmi_unified_unregister_event_handler(
						handle,
						wmi_nan_next_dw_info_event_id);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event deregistration failed, ret: %d", ret);
		status = ret;
	}

	ret = wmi_unified_unregister_event_handler(
					handle,
					wmi_nan_joined_cluster_event_id);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event deregistration failed, ret: %d", ret);
		status = ret;
	}

	ret = wmi_unified_unregister_event_handler(handle,
					     wmi_nan_started_cluster_event_id);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event deregistration failed, ret: %d", ret);
		status = ret;
	}

	ret = wmi_unified_unregister_event_handler(
					handle,
					wmi_nan_disable_rsp_event_id);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event deregistration failed, ret: %d", ret);
		status = ret;
	}

	ret = wmi_unified_unregister_event_handler(
					handle,
					wmi_nan_disable_ind_event_id);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event deregistration failed, ret: %d", ret);
		status = ret;
	}

	ret = wmi_unified_unregister_event_handler(handle,
						   wmi_nan_enable_rsp_event_id);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event deregistration failed, ret: %d", ret);
		status = ret;
	}

	return status;
}


/**
 * target_if_register_nan_std_mode_event_handler() - Register NAN std mode
 *						     events
 * @psoc: psoc pointer
 *
 * This function registers nan std mode events.
 *
 * Return: QDF_STATUS_SUCCESS on success, QDF_STATUS_E_* on failure
 */
static QDF_STATUS
target_if_register_nan_std_mode_event_handler(struct wlan_objmgr_psoc *psoc)
{
	QDF_STATUS ret;
	wmi_unified_t handle = get_wmi_unified_hdl_from_psoc(psoc);

	if (!handle) {
		target_if_err("handle is NULL");
		return QDF_STATUS_E_FAILURE;
	}

	ret = wmi_unified_register_event_handler(
				handle, wmi_nan_next_dw_info_event_id,
				target_if_nan_next_dw_info_event_handler,
				WMI_RX_UMAC_CTX);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event registration failed, ret: %d", ret);
		target_if_nan_deregister_events(psoc);
		return QDF_STATUS_E_FAILURE;
	}

	ret = wmi_unified_register_event_handler(
				handle, wmi_nan_joined_cluster_event_id,
				target_if_nan_join_cluster_event_handler,
				WMI_RX_UMAC_CTX);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event registration failed, ret: %d", ret);
		target_if_nan_deregister_events(psoc);
		return QDF_STATUS_E_FAILURE;
	}

	ret = wmi_unified_register_event_handler(
				handle, wmi_nan_started_cluster_event_id,
				target_if_nan_start_cluster_event_handler,
				WMI_RX_UMAC_CTX);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event registration failed, ret: %d", ret);
		target_if_nan_deregister_events(psoc);
		return QDF_STATUS_E_FAILURE;
	}

	ret = wmi_unified_register_event_handler(
					handle,
					wmi_nan_disable_rsp_event_id,
					target_if_nan_disable_rsp_handler,
					WMI_RX_UMAC_CTX);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event registration failed, ret: %d", ret);
		return QDF_STATUS_E_FAILURE;
	}

	ret = wmi_unified_register_event_handler(
					handle,
					wmi_nan_disable_ind_event_id,
					target_if_nan_disable_ind_handler,
					WMI_RX_UMAC_CTX);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event registration failed, ret: %d", ret);
		return QDF_STATUS_E_FAILURE;
	}

	ret = wmi_unified_register_event_handler(
					handle,
					wmi_nan_enable_rsp_event_id,
					target_if_nan_enable_rsp_handler,
					WMI_RX_UMAC_CTX);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event registration failed, ret: %d", ret);
		return QDF_STATUS_E_FAILURE;
	}

	return QDF_STATUS_SUCCESS;
}

bool target_if_nan_is_fw_support_standard_mode(struct wlan_objmgr_psoc *psoc)
{
	wmi_unified_t wmi_handle = lmac_get_wmi_unified_hdl(psoc);

	if (!wmi_handle) {
		target_if_err("wmi_handle is null");
		return false;
	}

	return wmi_service_enabled(wmi_handle,
				   wmi_service_nan_standard_mode_support);
}
#else
static inline QDF_STATUS
target_if_register_nan_std_mode_event_handler(struct wlan_objmgr_psoc *psoc)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
target_if_deregister_nan_std_mode_event_handler(struct wlan_objmgr_psoc *psoc)
{
	return QDF_STATUS_SUCCESS;
}
#endif /* WLAN_FEATURE_NAN && FEATURE_WLAN_SUPPORT_NAN_STANDARD_MODE */

QDF_STATUS target_if_nan_register_events(struct wlan_objmgr_psoc *psoc)
{
	QDF_STATUS ret;
	wmi_unified_t handle = get_wmi_unified_hdl_from_psoc(psoc);

	if (!handle) {
		target_if_err("handle is NULL");
		return QDF_STATUS_E_FAILURE;
	}

	/* Register for nan response event */
	ret = wmi_unified_register_event_handler(handle, wmi_nan_event_id,
						 target_if_nan_rsp_handler,
						 WMI_RX_UMAC_CTX);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event registration failed, ret: %d", ret);
		return QDF_STATUS_E_FAILURE;
	}

	ret = wmi_unified_register_event_handler(handle,
		wmi_ndp_initiator_rsp_event_id,
		target_if_ndp_initiator_rsp_handler,
		WMI_RX_UMAC_CTX);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event registration failed, ret: %d", ret);
		return QDF_STATUS_E_FAILURE;
	}

	ret = wmi_unified_register_event_handler(handle, wmi_nan_dmesg_event_id,
						 target_if_nan_dmesg_handler,
						 WMI_RX_WORK_CTX);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event registration failed, ret: %d", ret);
		target_if_nan_deregister_events(psoc);
		return QDF_STATUS_E_FAILURE;
	}

	ret = wmi_unified_register_event_handler(handle,
		wmi_ndp_indication_event_id,
		target_if_ndp_ind_handler,
		WMI_RX_UMAC_CTX);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event registration failed, ret: %d", ret);
		target_if_nan_deregister_events(psoc);
		return QDF_STATUS_E_FAILURE;
	}

	ret = wmi_unified_register_event_handler(handle,
		wmi_ndp_confirm_event_id,
		target_if_ndp_confirm_handler,
		WMI_RX_UMAC_CTX);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event registration failed, ret: %d", ret);
		target_if_nan_deregister_events(psoc);
		return QDF_STATUS_E_FAILURE;
	}

	ret = wmi_unified_register_event_handler(handle,
		wmi_ndp_responder_rsp_event_id,
		target_if_ndp_responder_rsp_handler,
		WMI_RX_UMAC_CTX);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event registration failed, ret: %d", ret);
		target_if_nan_deregister_events(psoc);
		return QDF_STATUS_E_FAILURE;
	}

	ret = wmi_unified_register_event_handler(handle,
		wmi_ndp_end_indication_event_id,
		target_if_ndp_end_ind_handler,
		WMI_RX_UMAC_CTX);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event registration failed, ret: %d", ret);
		target_if_nan_deregister_events(psoc);
		return QDF_STATUS_E_FAILURE;
	}

	ret = wmi_unified_register_event_handler(handle,
		wmi_ndp_end_rsp_event_id,
		target_if_ndp_end_rsp_handler,
		WMI_RX_UMAC_CTX);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event registration failed, ret: %d", ret);
		target_if_nan_deregister_events(psoc);
		return QDF_STATUS_E_FAILURE;
	}

	ret = wmi_unified_register_event_handler(handle,
		wmi_ndl_schedule_update_event_id,
		target_if_ndp_sch_update_handler,
		WMI_RX_UMAC_CTX);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event registration failed, ret: %d", ret);
		target_if_nan_deregister_events(psoc);
		return QDF_STATUS_E_FAILURE;
	}

	ret = wmi_unified_register_event_handler(handle, wmi_ndp_event_id,
					       target_if_ndp_host_event_handler,
					       WMI_RX_UMAC_CTX);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event registration failed, ret: %d", ret);
		target_if_nan_deregister_events(psoc);
		return QDF_STATUS_E_FAILURE;
	}

	ret = target_if_register_nan_std_mode_event_handler(psoc);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("NAN standard mode event registration failed, ret: %d", ret);
		return QDF_STATUS_E_FAILURE;
	}

	return QDF_STATUS_SUCCESS;
}

QDF_STATUS target_if_nan_deregister_events(struct wlan_objmgr_psoc *psoc)
{
	QDF_STATUS ret, status = QDF_STATUS_SUCCESS;
	wmi_unified_t handle = get_wmi_unified_hdl_from_psoc(psoc);

	if (!handle) {
		target_if_err("handle is NULL");
		return QDF_STATUS_E_FAILURE;
	}
	ret = wmi_unified_unregister_event_handler(handle,
				wmi_ndl_schedule_update_event_id);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event deregistration failed, ret: %d", ret);
		status = ret;
	}

	ret = wmi_unified_unregister_event_handler(handle,
				wmi_ndp_end_rsp_event_id);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event deregistration failed, ret: %d", ret);
		status = ret;
	}

	ret = wmi_unified_unregister_event_handler(handle,
				wmi_ndp_end_indication_event_id);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event deregistration failed, ret: %d", ret);
		status = ret;
	}

	ret = wmi_unified_unregister_event_handler(handle,
				wmi_ndp_responder_rsp_event_id);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event deregistration failed, ret: %d", ret);
		status = ret;
	}

	ret = wmi_unified_unregister_event_handler(handle,
				wmi_ndp_confirm_event_id);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event deregistration failed, ret: %d", ret);
		status = ret;
	}

	ret = wmi_unified_unregister_event_handler(handle,
				wmi_ndp_indication_event_id);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event deregistration failed, ret: %d", ret);
		status = ret;
	}

	ret = wmi_unified_unregister_event_handler(handle,
						   wmi_nan_dmesg_event_id);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event deregistration failed, ret: %d", ret);
		status = ret;
	}

	ret = wmi_unified_unregister_event_handler(handle,
				wmi_ndp_initiator_rsp_event_id);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event deregistration failed, ret: %d", ret);
		status = ret;
	}

	ret = wmi_unified_unregister_event_handler(handle, wmi_ndp_event_id);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("wmi event deregistration failed, ret: %d", ret);
		status = ret;
	}
	ret = target_if_deregister_nan_std_mode_event_handler(psoc);
	if (QDF_IS_STATUS_ERROR(ret)) {
		target_if_err("NAN standard mode event deregistration failed, ret: %d", ret);
		status = ret;
	}

	if (QDF_IS_STATUS_ERROR(status))
		return QDF_STATUS_E_FAILURE;
	else
		return QDF_STATUS_SUCCESS;
}

void target_if_nan_set_vdev_feature_config(struct wlan_objmgr_psoc *psoc,
					   uint8_t vdev_id)
{
	QDF_STATUS status;
	uint32_t nan_features;
	struct vdev_set_params param;
	wmi_unified_t wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);

	if (!wmi_handle) {
		target_if_err("Invalid wmi handle");
		return;
	}

	if (!target_if_is_vdev_valid(vdev_id)) {
		target_if_err("vdev_id: %d is invalid, reject the req: param id %d",
			      vdev_id,
			      wmi_vdev_param_enable_disable_nan_config_features);
		return;
	}

	ucfg_get_nan_feature_config(psoc, &nan_features);
	target_if_debug("vdev_id:%d NAN features:0x%x", vdev_id, nan_features);

	param.vdev_id = vdev_id;
	param.param_id = wmi_vdev_param_enable_disable_nan_config_features;
	param.param_value = nan_features;

	status = wmi_unified_vdev_set_param_send(wmi_handle, &param);
	if (QDF_IS_STATUS_ERROR(status))
		target_if_err("failed to set NAN_CONFIG_FEATURES(status = %d)",
			      status);
}

#if defined(FEATURE_WLAN_SUPPORT_NAN_STANDARD_MODE) && defined(WLAN_FEATURE_NAN)
bool target_if_nan_is_fw_support_standard_mode(struct wlan_objmgr_psoc *psoc)
{
	wmi_unified_t wmi_handle = lmac_get_wmi_unified_hdl(psoc);

	if (!wmi_handle) {
		target_if_err("wmi_handle is null");
		return false;
	}

	return wmi_service_enabled(wmi_handle,
				   wmi_service_nan_standard_mode_support);
}

QDF_STATUS target_if_nan_set_device_caps(struct wlan_objmgr_psoc *psoc,
					 struct nan_capabilities *caps)
{
	struct device_nan_capabilities *device_caps;
	struct target_psoc_info *tgt_hdl;

	tgt_hdl = wlan_psoc_get_tgt_if_handle(psoc);
	if (!tgt_hdl) {
		target_if_err("tgt_hdl is null");
		return QDF_STATUS_E_NULL_VALUE;
	}

	device_caps = &tgt_hdl->info.service_ext2_param.nan_caps;

	QDF_SET_BITS(caps->flags, NAN_FLAGS_CONFIGURABLE_SYNC_BIT,
		     NAN_CONFIGURATION_BIT_NUM, NAN_SET_CONFIGURATION_FLAG);
	QDF_SET_BITS(caps->flags, NAN_FLAGS_USERSPACE_DE_BIT,
		     NAN_CONFIGURATION_BIT_NUM, NAN_SET_CONFIGURATION_FLAG);

	QDF_SET_BITS(caps->op_mode, NAN_VHT_OPMODE_BIT,
		     NAN_CONFIGURATION_BIT_NUM, device_caps->vht_phy_mode);
	QDF_SET_BITS(caps->op_mode, NAN_HE_OPMODE_BIT,
		     NAN_CONFIGURATION_BIT_NUM, device_caps->he_phy_mode);
	QDF_SET_BITS(caps->op_mode, NAN_HE_VHT_80_80_OPMODE_BIT,
		     NAN_CONFIGURATION_BIT_NUM, device_caps->he_vht_80_80);
	QDF_SET_BITS(caps->op_mode, NAN_HE_VHT_160_OPMODE_BIT,
		     NAN_CONFIGURATION_BIT_NUM, device_caps->he_vht_160);

	QDF_SET_BITS(caps->n_antennas, NAN_TX_ANT_BIT,
		     NAN_CONFIGURATION_BIT_ANTENNA_NUM,
		     device_caps->num_tx_ant);
	QDF_SET_BITS(caps->n_antennas, NAN_RX_ANT_BIT,
		     NAN_CONFIGURATION_BIT_ANTENNA_NUM,
		     device_caps->num_rx_ant);

	caps->max_channel_switch_time = device_caps->max_chan_switch_time;
	QDF_SET_BITS(caps->dev_capabilities, NAN_S3_SUPPORT_BIT,
		     NAN_CONFIGURATION_BIT_NUM, device_caps->s3_support);

	return QDF_STATUS_SUCCESS;
}
#endif /* FEATURE_WLAN_SUPPORT_NAN_STANDARD_MODE && WLAN_FEATURE_NAN */
