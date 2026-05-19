/*
 * Copyright (c) 2017-2020 The Linux Foundation. All rights reserved.
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
 * DOC: offload lmac interface APIs for tdls
 *
 */

#include <qdf_mem.h>
#include <target_if.h>
#include <qdf_status.h>
#include <wmi_unified_api.h>
#include <wmi_unified_priv.h>
#include <wmi_unified_param.h>
#include <wlan_objmgr_psoc_obj.h>
#include <wlan_tdls_tgt_api.h>
#include <target_if_tdls.h>
#include <cdp_txrx_peer_ops.h>
#include <wlan_utility.h>
#include <wlan_tdls_stats_api.h>
#include <wlan_tdls_api.h>

static inline struct wlan_lmac_if_tdls_rx_ops *
target_if_tdls_get_rx_ops(struct wlan_objmgr_psoc *psoc)
{
	return &psoc->soc_cb.rx_ops->tdls_rx_ops;
}

static int
target_if_tdls_event_handler(ol_scn_t scn, uint8_t *data, uint32_t datalen)
{
	struct wlan_objmgr_psoc *psoc;
	struct wmi_unified *wmi_handle;
	struct wlan_lmac_if_tdls_rx_ops *tdls_rx_ops;
	struct tdls_event_info info;
	QDF_STATUS status;

	if (!scn || !data) {
		target_if_err("scn: 0x%pK, data: 0x%pK", scn, data);
		return -EINVAL;
	}
	psoc = target_if_get_psoc_from_scn_hdl(scn);
	if (!psoc) {
		target_if_err("null psoc");
		return -EINVAL;
	}
	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);

	if (!wmi_handle) {
		target_if_err("null wmi_handle");
		return -EINVAL;
	}

	if (wmi_extract_vdev_tdls_ev_param(wmi_handle, data, &info)) {
		target_if_err("Failed to extract wmi tdls event");
		return -EINVAL;
	}

	tdls_rx_ops = target_if_tdls_get_rx_ops(psoc);
	if (tdls_rx_ops && tdls_rx_ops->tdls_ev_handler) {
		status = tdls_rx_ops->tdls_ev_handler(psoc, &info);
		if (QDF_IS_STATUS_ERROR(status)) {
			target_if_err("fail to handle tdls event");
			return -EINVAL;
		}
	}

	return 0;
}

QDF_STATUS
target_if_tdls_update_fw_state(struct wlan_objmgr_psoc *psoc,
			       struct tdls_info *param)
{
	QDF_STATUS status;
	enum wmi_tdls_state tdls_state;
	struct wmi_unified *wmi_handle;

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("Invalid WMI handle");
		return QDF_STATUS_E_FAILURE;
	}

	if (TDLS_SUPPORT_EXP_TRIG_ONLY == param->tdls_state)
		tdls_state = WMI_TDLS_ENABLE_PASSIVE;
	else if (TDLS_SUPPORT_IMP_MODE == param->tdls_state ||
		 TDLS_SUPPORT_EXT_CONTROL == param->tdls_state)
		tdls_state = WMI_TDLS_ENABLE_CONNECTION_TRACKER_IN_HOST;
	else
		tdls_state = WMI_TDLS_DISABLE;

	status = wmi_unified_update_fw_tdls_state_cmd(wmi_handle,
						      param, tdls_state);

	target_if_debug("vdev_id %d", param->vdev_id);
	return status;
}

QDF_STATUS
target_if_tdls_set_offchan_mode(struct wlan_objmgr_psoc *psoc,
				struct tdls_channel_switch_params *params)
{
	QDF_STATUS status;
	struct wmi_unified *wmi_handle;

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("Invalid WMI handle");
		return QDF_STATUS_E_FAILURE;
	}
	status = wmi_unified_set_tdls_offchan_mode_cmd(wmi_handle,
						       params);

	return status;
}

/**
 * target_if_tdls_fill_connect_info_entry() - Populate a tdls_stats_entry from
 *                                            a WMI connect_info stats record.
 * @entry: Destination entry (zeroed by the caller or here).
 * @ci:    Source WMI connect_info stats record.
 *
 * Converts the WMI-layer representation of a TDLS control-path event
 * (Types 0-4: setup, discovery, teardown, etc.) into the host-layer
 * struct tdls_stats_entry used by the stats state machine.
 */
static void
target_if_tdls_fill_connect_info_entry(
		struct tdls_stats_entry *entry,
		const struct wmi_host_tdls_connect_info_stats *ci)
{
	entry->type        = (uint8_t)ci->type;
	entry->subtype     = (uint8_t)ci->subtype;
	entry->reason_code = (uint8_t)ci->reason_code;
	entry->is_sender   = (uint8_t)ci->is_sender;
	entry->channel     = (uint16_t)ci->op_freq_mhz;
	entry->rssi        = (int16_t)ci->rssi;
	entry->ts_ms       = ci->timestamp;
	entry->success     = ci->status;
	qdf_mem_copy(entry->peer_mac, ci->peer_mac, QDF_MAC_ADDR_SIZE);
}

/**
 * target_if_tdls_fill_data_stats_entry() - Populate a tdls_stats_entry from
 *                                          a WMI periodic data stats record.
 * @entry: Destination entry (zeroed by the caller or here).
 * @ds:    Source WMI data stats record (Type 5).
 *
 * Converts the WMI-layer representation of a TDLS periodic data stats
 * event (Type 5) into the host-layer struct tdls_stats_entry.
 */
static void
target_if_tdls_fill_data_stats_entry(
		struct tdls_stats_entry *entry,
		const struct wmi_host_tdls_data_stats *ds)
{
	entry->type    = TDLS_STATS_DATA;
	entry->subtype = TDLS_STATS_SUBTYPE_GENERAL;
	entry->ts_ms   = ds->timestamp;
	entry->rssi    = (int16_t)ds->rssi;
	entry->channel = (uint16_t)ds->op_freq_mhz;
	qdf_mem_copy(entry->peer_mac, ds->peer_mac, QDF_MAC_ADDR_SIZE);
	entry->tx_ppdus_cumulative = ds->tx_ppdus_cumulative;
	qdf_mem_copy(entry->tx_mcs_data_ppdu, ds->tx_mcs_data_ppdu,
		     sizeof(entry->tx_mcs_data_ppdu));
	entry->tx_ppdu_failures    = ds->tx_ppdu_failures;
	entry->rx_ppdus_cumulative = ds->rx_ppdus_cumulative;
	qdf_mem_copy(entry->rx_mcs_data_ppdu, ds->rx_mcs_data_ppdu,
		     sizeof(entry->rx_mcs_data_ppdu));
	entry->rx_ppdu_failures    = ds->rx_ppdu_failures;
	entry->data_rate           = ds->data_rate;
}

/**
 * target_if_tdls_fill_stats_entries() - Fill a pre-allocated entries array
 *                                       from a WMI stats event.
 * @ev:      Extracted WMI TDLS stats event (connect_info + data_stats arrays).
 * @entries: Pre-allocated array of (ci_cnt + ds_cnt) tdls_stats_entry elements.
 *           The caller allocates this buffer and is responsible for freeing it.
 *
 * Fills @entries with all connect_info records (indices 0..ci_cnt-1) followed
 * by all data_stats records (indices ci_cnt..total-1) using the per-type fill
 * helpers.  Does not touch the tdls_stats_batch descriptor — the caller sets
 * batch.entries and batch.num_entries directly.
 */
static void
target_if_tdls_fill_stats_entries(
		const struct wmi_host_tdls_stats_event *ev,
		struct tdls_stats_entry *entries)
{
	uint32_t ci_cnt = ev->num_tdls_connect_info_stats;
	uint32_t ds_cnt = ev->num_tdls_data_stats;
	uint32_t idx    = 0;
	uint32_t j;

	for (j = 0; j < ci_cnt; j++, idx++) {
		entries[idx].session_id = ev->vdev_id;
		target_if_tdls_fill_connect_info_entry(
				&entries[idx],
				&ev->tdls_connect_info_stats[j]);
	}

	for (j = 0; j < ds_cnt; j++, idx++) {
		entries[idx].session_id = ev->vdev_id;
		target_if_tdls_fill_data_stats_entry(
				&entries[idx],
				&ev->tdls_data_stats[j]);
	}
}

/**
 * target_if_tdls_stats_event_handler() - WMI event handler for
 *                                        WMI_TDLS_STATS_EVENTID
 * @scn: scn handle
 * @data: event data buffer
 * @datalen: length of event data
 *
 * Extracts per-peer TDLS stats from the WMI event, merges all connect_info
 * and data_stats entries into a single batch via
 * target_if_tdls_build_stats_batch(), and delivers the batch to the TDLS
 * stats state machine in one call.  The SM acquires its lock once, loops
 * over all entries internally, and releases the lock.
 *
 * The caller (WMI layer) owns the @data buffer; this handler must not
 * free it.
 *
 * Return: 0 on success, negative errno otherwise
 */
static int
target_if_tdls_stats_event_handler(ol_scn_t scn, uint8_t *data,
				   uint32_t datalen)
{
	struct wlan_objmgr_psoc *psoc;
	struct wmi_unified *wmi_handle;
	struct wmi_host_tdls_stats_event stats_event;
	struct tdls_soc_priv_obj *soc_obj;
	struct tdls_stats_entry *entries;
	struct tdls_stats_batch batch;
	uint32_t total;
	QDF_STATUS status;
	uint32_t i;

	if (!scn || !data) {
		target_if_err("TDLS stats: scn: 0x%pK, data: 0x%pK",
			      scn, data);
		return -EINVAL;
	}

	psoc = target_if_get_psoc_from_scn_hdl(scn);
	if (!psoc) {
		target_if_err("TDLS stats: null psoc");
		return -EINVAL;
	}

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("TDLS stats: null wmi_handle");
		return -EINVAL;
	}

	qdf_mem_zero(&stats_event, sizeof(stats_event));
	status = wmi_extract_tdls_stats_event(wmi_handle, data, &stats_event);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("TDLS stats: failed to extract event, status %d",
			      status);
		return -EINVAL;
	}

	if (!stats_event.tdls_connect_info_stats &&
	    !stats_event.tdls_data_stats) {
		target_if_debug("TDLS stats: no stats in event");
		return 0;
	}

	soc_obj = wlan_psoc_get_tdls_soc_obj(psoc);
	if (!soc_obj || !soc_obj->stats_ctx) {
		target_if_err("TDLS stats: soc_obj or stats_ctx is NULL");
		qdf_mem_free(stats_event.tdls_connect_info_stats);
		qdf_mem_free(stats_event.tdls_data_stats);
		return -EINVAL;
	}

	total   = stats_event.num_tdls_connect_info_stats +
		  stats_event.num_tdls_data_stats;
	entries = qdf_mem_malloc(total * sizeof(*entries));
	if (!entries) {
		target_if_err("TDLS stats: OOM for combined batch (%u entries)",
			      total);
	} else {
		target_if_tdls_fill_stats_entries(&stats_event, entries);
		batch.num_entries = total;
		batch.entries     = entries;
		for (i = 0; i < total; i++)
			wlan_tdls_stats_entry_find_vdev_info(&entries[i], psoc);

		wlan_tdls_stats_sm_deliver_event(soc_obj->stats_ctx,
						 TDLS_STATS_EV_FW_STATS,
						 sizeof(batch), &batch);
		qdf_mem_free(entries);
	}

	qdf_mem_free(stats_event.tdls_connect_info_stats);
	qdf_mem_free(stats_event.tdls_data_stats);
	return 0;
}

QDF_STATUS
target_if_tdls_register_event_handler(struct wlan_objmgr_psoc *psoc,
				      void *arg)
{
	struct wmi_unified *wmi_handle;
	QDF_STATUS status;

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("null wmi_handle");
		return QDF_STATUS_E_INVAL;
	}

	status = wmi_unified_register_event(wmi_handle,
					    wmi_tdls_peer_event_id,
					    target_if_tdls_event_handler);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("Failed to register tdls peer event handler");
		return status;
	}

	status = wmi_unified_register_event_handler(wmi_handle,
					    wmi_tdls_stats_event_id,
					    target_if_tdls_stats_event_handler,
					    WMI_RX_SERIALIZER_CTX);
	if (QDF_IS_STATUS_ERROR(status)) {
		target_if_err("Failed to register tdls stats event handler");
		wmi_unified_unregister_event(wmi_handle,
					     wmi_tdls_peer_event_id);
		return status;
	}

	return QDF_STATUS_SUCCESS;
}

QDF_STATUS
target_if_tdls_unregister_event_handler(struct wlan_objmgr_psoc *psoc,
					void *arg)
{
	struct wmi_unified *wmi_handle;

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("null wmi_handle");
		return QDF_STATUS_E_INVAL;
	}

	wmi_unified_unregister_event_handler(wmi_handle,
					     wmi_tdls_stats_event_id);

	return wmi_unified_unregister_event(wmi_handle,
					    wmi_tdls_peer_event_id);
}

/**
 * target_if_tdls_request_stats_info() - Send WMI_REQUEST_STATS_INFO_CMDID
 * @psoc: PSOC object
 * @vdev_id: ID of the STA vdev for which stats collection is requested
 * @enable: 1 = enable FW TDLS stats collection (SCC only);
 *          host never sends 0 — FW handles disable automatically
 *
 * Sends WMI_REQUEST_STATS_INFO_CMDID to firmware to start per-peer TDLS
 * data stats collection on the specified vdev.  Called once per STA
 * connection when a single-STA SCC condition is detected.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code otherwise.
 */
static QDF_STATUS
target_if_tdls_request_stats_info(struct wlan_objmgr_psoc *psoc,
				  uint8_t vdev_id, uint32_t enable)
{
	struct wmi_unified *wmi_handle;

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle) {
		target_if_err("TDLS stats: Invalid WMI handle");
		return QDF_STATUS_E_FAILURE;
	}

	return wmi_unified_tdls_request_stats_info_cmd(wmi_handle,
						       vdev_id, enable);
}

QDF_STATUS
target_if_tdls_register_tx_ops(struct wlan_lmac_if_tx_ops *tx_ops)
{
	struct wlan_lmac_if_tdls_tx_ops *tdls_txops;

	tdls_txops = &tx_ops->tdls_tx_ops;

	tdls_txops->update_fw_state = target_if_tdls_update_fw_state;
	tdls_txops->set_offchan_mode = target_if_tdls_set_offchan_mode;
	tdls_txops->tdls_reg_ev_handler = target_if_tdls_register_event_handler;
	tdls_txops->tdls_unreg_ev_handler =
		target_if_tdls_unregister_event_handler;
	tdls_txops->request_stats_info = target_if_tdls_request_stats_info;

	return QDF_STATUS_SUCCESS;
}
