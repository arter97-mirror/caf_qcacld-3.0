/*
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

#ifdef FEATURE_WIFI_TRANSPORT

#include "os_if_dp_wifi_transport.h"
#include "wlan_dp_wifi_transport_ucfg_api.h"
#include "wlan_dp_main.h"
#include <linux/auxiliary_bus.h>
#include <wifi_transport_aux.h>

/*
 * Heap-allocated per-register-cycle auxiliary device instance.
 * Freed from os_if_dp_wifi_transport_adev_release() once all kernel
 * references drop.
 * NULL between deregister and the next register call.
 */
static struct wifi_transport_auxiliary_device *g_wifi_transport_adev;

/**
 * os_if_dp_wifi_transport_log() - Forward a WiFi Transport log entry
 * @level: log severity (enum wifi_tp_log_level)
 * @buf: null-terminated log string
 * @len: length of buf (excluding null terminator)
 *
 * Maps the WiFi Transport driver log level to
 * QDF_MODULE_ID_WIFI_TRANSPORT trace level.
 *
 * Return: None
 */
static void os_if_dp_wifi_transport_log(int level, const char *buf, size_t len)
{
	switch (level) {
	case WIFI_TP_LOG_ERROR:
		__QDF_TRACE_NO_FL(QDF_TRACE_LEVEL_ERROR,
				  QDF_MODULE_ID_WIFI_TRANSPORT, "%s", buf);
		break;
	case WIFI_TP_LOG_INFO:
		__QDF_TRACE_NO_FL(QDF_TRACE_LEVEL_INFO_HIGH,
				  QDF_MODULE_ID_WIFI_TRANSPORT, "%s", buf);
		break;
	case WIFI_TP_LOG_DEBUG:
		__QDF_TRACE_NO_FL(QDF_TRACE_LEVEL_DEBUG,
				  QDF_MODULE_ID_WIFI_TRANSPORT, "%s", buf);
		break;
	case WIFI_TP_LOG_ERROR_RL:
		__QDF_TRACE_RL_NO_FL(QDF_TRACE_LEVEL_ERROR,
				     QDF_MODULE_ID_WIFI_TRANSPORT,
				     "%s", buf);
		break;
	case WIFI_TP_LOG_INFO_RL:
		__QDF_TRACE_RL_NO_FL(QDF_TRACE_LEVEL_INFO_HIGH,
				     QDF_MODULE_ID_WIFI_TRANSPORT,
				     "%s", buf);
		break;
	default:
		break;
	}
}

/**
 * os_if_dp_wifi_transport_init() - Start a WiFi Transport session
 * @ops: WiFi Transport driver callbacks for TX completion and RX indication
 *
 * Called by the WiFi Transport driver after it binds to the aux device.
 * Validates that both required callbacks are provided, then delegates
 * to ucfg layer.
 *
 * Return: 0 on success, -EINVAL if callbacks missing, -ENODEV if no session
 */
static int os_if_dp_wifi_transport_init(const struct wifi_transport_ops *ops)
{
	dp_info("init called by the WiFi Transport driver");
	if (!ops || !ops->tx_completion || !ops->process_rx) {
		dp_err("ops NULL");
		return -EINVAL;
	}

	return qdf_status_to_os_return(ucfg_dp_wifi_transport_init(ops));
}

/**
 * os_if_dp_wifi_transport_deinit() - Stop the active WiFi Transport session
 *
 * Called by the WiFi Transport driver on unload or link-down.
 * Normal stop (not SSR).
 *
 * Return: None
 */
static void os_if_dp_wifi_transport_deinit(void)
{
	dp_info("deinit called by the WiFi Transport driver");
	ucfg_dp_wifi_transport_deinit(false);
}

/**
 * os_if_dp_wifi_transport_set_params() - Apply a runtime control parameter
 * @type: parameter selector (enum wifi_transport_param_type)
 * @data: parameter value
 *
 * Return: 0 on success, -EINVAL on bad args
 */
static int os_if_dp_wifi_transport_set_params(
			enum wifi_transport_param_type type,
			const union wifi_transport_param_data *data)
{
	if (type >= WT_PARAM_MAX || !data)
		return -EINVAL;

	return ucfg_dp_wifi_transport_set_params(type, data);
}

/**
 * os_if_dp_wifi_transport_get_params() - Read a runtime control parameter
 * @type: parameter selector (enum wifi_transport_param_type)
 * @data: output buffer
 *
 * Return: 0 on success, -EINVAL on bad args
 */
static int os_if_dp_wifi_transport_get_params(
			enum wifi_transport_param_type type,
			union wifi_transport_param_data *data)
{
	if (type >= WT_PARAM_MAX || !data)
		return -EINVAL;

	return ucfg_dp_wifi_transport_get_params(type, data);
}

/**
 * os_if_dp_wifi_transport_adev_release() - Free auxiliary device after
 *   last reference
 * @dev: embedded &auxiliary_device.dev
 *
 * Called by the kernel kobject layer when all references to the device are
 * dropped (after auxiliary_device_uninit()).  Frees the per-cycle allocation
 * and resets the global pointer so the next register call starts clean.
 *
 * Return: None
 */
static void os_if_dp_wifi_transport_adev_release(struct device *dev)
{
	struct wifi_transport_auxiliary_device *adev =
		container_of(dev, struct wifi_transport_auxiliary_device,
			     adev.dev);

	dp_info("auxiliary device released (%s)", dev_name(dev));
	qdf_mem_free(adev);
	g_wifi_transport_adev = NULL;
}

QDF_STATUS os_if_dp_wifi_transport_register(struct wlan_objmgr_psoc *psoc)
{
	struct wifi_transport_auxiliary_device *adev;
	qdf_device_t qdf_dev = cds_get_context(QDF_MODULE_ID_QDF_DEVICE);
	int ret;
	QDF_STATUS status;

	dp_info("auxiliary device register");

	if (!qdf_dev) {
		dp_err("QDF device NULL at register");
		return QDF_STATUS_E_FAILURE;
	}

	status = ucfg_dp_wifi_transport_link_init(psoc);
	if (QDF_IS_STATUS_ERROR(status)) {
		dp_err("link_init failed %d", status);
		return status;
	}

	adev = qdf_mem_malloc(sizeof(*adev));
	if (!adev) {
		dp_err("Aux device allocation failed");
		ucfg_dp_wifi_transport_link_deinit(psoc);
		return QDF_STATUS_E_NOMEM;
	}

	g_wifi_transport_adev      = adev;
	adev->host_ops.init        = os_if_dp_wifi_transport_init;
	adev->host_ops.deinit      = os_if_dp_wifi_transport_deinit;
	adev->host_ops.set_params  = os_if_dp_wifi_transport_set_params;
	adev->host_ops.get_params  = os_if_dp_wifi_transport_get_params;
	adev->host_ops.log         = os_if_dp_wifi_transport_log;
	adev->adev.name            = "wifi_tp";
	adev->adev.id              = 0;
	adev->adev.dev.parent      = qdf_dev->dev;
	adev->adev.dev.release     = os_if_dp_wifi_transport_adev_release;

	ret = auxiliary_device_init(&adev->adev);
	if (ret) {
		dp_err("auxiliary_device_init failed %d", ret);
		ucfg_dp_wifi_transport_link_deinit(psoc);
		qdf_mem_free(adev);
		g_wifi_transport_adev = NULL;
		return QDF_STATUS_E_FAILURE;
	}

	ret = auxiliary_device_add(&adev->adev);
	if (ret) {
		dp_err("auxiliary_device_add failed %d", ret);
		/*
		 * auxiliary_device_uninit() -> put_device() -> release() frees
		 * adev and clears g_wifi_transport_adev.
		 */
		auxiliary_device_uninit(&adev->adev);
		ucfg_dp_wifi_transport_link_deinit(psoc);
		return QDF_STATUS_E_FAILURE;
	}

	dp_info("auxiliary device registered (%s)", dev_name(&adev->adev.dev));

	return QDF_STATUS_SUCCESS;
}

void os_if_dp_wifi_transport_deregister(struct wlan_objmgr_psoc *psoc,
					bool is_recovery_stop)
{
	dp_info("deregistering auxiliary device, recovery=%d",
		is_recovery_stop);

	if (g_wifi_transport_adev) {
		auxiliary_device_delete(&g_wifi_transport_adev->adev);
		auxiliary_device_uninit(&g_wifi_transport_adev->adev);
	}

	ucfg_dp_wifi_transport_deinit(is_recovery_stop);
	ucfg_dp_wifi_transport_link_deinit(psoc);
	dp_info("deregister complete");
}

#endif /* FEATURE_WIFI_TRANSPORT */
