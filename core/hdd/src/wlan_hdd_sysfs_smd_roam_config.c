/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * DOC: wlan_hdd_sysfs_smd_roam_config.c
 *
 * Implementation for creating sysfs file smd_roam_manual_conf.
 *
 * Sysfs values:
 * 0 - default low-latency SMD roam policy
 * 1 - manual SMD execution via source/current AP
 * 2 - manual SMD execution via target/new AP
 */

#include <wlan_hdd_includes.h>

#if defined(WLAN_SYSFS) && defined(WLAN_FEATURE_11BN_SMD)
#include "osif_vdev_sync.h"
#include <wlan_hdd_sysfs.h>
#include "wlan_cm_tgt_if_tx_api.h"
#include <target_if.h>
#include <wmi_unified.h>
#include "wlan_hdd_sysfs_smd_roam_config.h"

enum hdd_smd_roam_manual_mode {
	HDD_SMD_ROAM_MODE_DEFAULT = 0,
	HDD_SMD_ROAM_MODE_SOURCE_AP = 1,
	HDD_SMD_ROAM_MODE_TARGET_AP = 2,
	HDD_SMD_ROAM_MODE_MAX = HDD_SMD_ROAM_MODE_TARGET_AP,
};

static bool
hdd_smd_roam_is_supported(struct hdd_context *hdd_ctx)
{
	struct wmi_unified *wmi_handle;

	wmi_handle = get_wmi_unified_hdl_from_psoc(hdd_ctx->psoc);
	if (!wmi_handle)
		return false;

	return wmi_service_enabled(wmi_handle,
				   wmi_service_smd_bss_transition_support);
}

static int
hdd_smd_roam_prepare_config(uint32_t mode, struct hdd_adapter *adapter,
			    struct wlan_roam_smd_config *req)
{
	if (mode > HDD_SMD_ROAM_MODE_MAX)
		return -EINVAL;

	req->vdev_id = adapter->deflink->vdev_id;

	if (mode == HDD_SMD_ROAM_MODE_DEFAULT) {
		req->prefer_mode = WMI_ROAM_LOWLATENCY_MODE;
		return 0;
	}

	req->prefer_mode = WMI_ROAM_MANUAL_MODE;
	if (mode == HDD_SMD_ROAM_MODE_TARGET_AP)
		WMI_SMD_CONFIG_MANUAL_CONF_SET_EXECUTION_VIA_NEW_AP(
							req->manual_conf, 1);

	return 0;
}

static ssize_t
__hdd_sysfs_smd_roam_manual_conf_show(struct net_device *net_dev, char *buf)
{
	struct hdd_adapter *adapter = netdev_priv(net_dev);
	struct hdd_context *hdd_ctx;
	int ret;

	if (hdd_validate_adapter(adapter))
		return -EINVAL;

	hdd_ctx = WLAN_HDD_GET_CTX(adapter);
	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret)
		return ret;

	if (!wlan_hdd_validate_modules_state(hdd_ctx))
		return -EINVAL;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 adapter->smd_roam_manual_mode);
}

static ssize_t
hdd_sysfs_smd_roam_manual_conf_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct net_device *net_dev = container_of(dev, struct net_device, dev);
	struct osif_vdev_sync *vdev_sync;
	ssize_t ret;

	ret = osif_vdev_sync_op_start(net_dev, &vdev_sync);
	if (ret)
		return ret;

	ret = __hdd_sysfs_smd_roam_manual_conf_show(net_dev, buf);

	osif_vdev_sync_op_stop(vdev_sync);

	return ret;
}

static ssize_t
__hdd_sysfs_smd_roam_manual_conf_store(struct net_device *net_dev,
				       char const *buf, size_t count)
{
	struct hdd_adapter *adapter = netdev_priv(net_dev);
	char buf_local[MAX_SYSFS_USER_COMMAND_SIZE_LENGTH + 1];
	struct wlan_roam_smd_config req = {0};
	struct hdd_context *hdd_ctx;
	uint32_t mode;
	char *sptr, *token;
	int ret;

	if (hdd_validate_adapter(adapter))
		return -EINVAL;

	hdd_ctx = WLAN_HDD_GET_CTX(adapter);
	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret)
		return ret;

	if (!wlan_hdd_validate_modules_state(hdd_ctx))
		return -EINVAL;

	if (!hdd_cm_is_vdev_associated(adapter->deflink)) {
		hdd_err_rl("SMD roam config is valid only after connection");
		return -ENOTCONN;
	}

	if (!hdd_smd_roam_is_supported(hdd_ctx)) {
		hdd_err_rl("SMD roam config is not supported");
		return -EOPNOTSUPP;
	}

	ret = hdd_sysfs_validate_and_copy_buf(buf_local, sizeof(buf_local),
					      buf, count);
	if (ret) {
		hdd_err_rl("invalid input");
		return ret;
	}

	sptr = buf_local;
	token = strsep(&sptr, " ");
	if (!token || kstrtou32(token, 0, &mode))
		return -EINVAL;

	ret = hdd_smd_roam_prepare_config(mode, adapter, &req);
	if (ret) {
		hdd_err_rl("invalid SMD roam manual mode %u", mode);
		return ret;
	}

	ret = qdf_status_to_os_return(wlan_cm_tgt_send_roam_smd_config(
							hdd_ctx->psoc,
							req.vdev_id,
							&req));
	if (ret) {
		hdd_err_rl("failed to send SMD roam config, ret %d", ret);
		return ret;
	}

	adapter->smd_roam_manual_mode = mode;

	hdd_debug("SMD roam manual mode %u vdev_id %u prefer_mode %u manual_conf 0x%x",
		  mode, req.vdev_id, req.prefer_mode, req.manual_conf);

	return count;
}

static ssize_t
hdd_sysfs_smd_roam_manual_conf_store(struct device *dev,
				     struct device_attribute *attr,
				     char const *buf, size_t count)
{
	struct net_device *net_dev = container_of(dev, struct net_device, dev);
	struct osif_vdev_sync *vdev_sync;
	ssize_t ret;

	ret = osif_vdev_sync_op_start(net_dev, &vdev_sync);
	if (ret)
		return ret;

	ret = __hdd_sysfs_smd_roam_manual_conf_store(net_dev, buf, count);

	osif_vdev_sync_op_stop(vdev_sync);

	return ret;
}

static DEVICE_ATTR(smd_roam_manual_conf, 0660,
		   hdd_sysfs_smd_roam_manual_conf_show,
		   hdd_sysfs_smd_roam_manual_conf_store);

void hdd_sysfs_smd_roam_config_create(struct hdd_adapter *adapter)
{
	int error;

	error = device_create_file(&adapter->dev->dev,
				   &dev_attr_smd_roam_manual_conf);
	if (error)
		hdd_err("could not create smd_roam_manual_conf sysfs file");
}

void hdd_sysfs_smd_roam_config_destroy(struct hdd_adapter *adapter)
{
	device_remove_file(&adapter->dev->dev,
			   &dev_attr_smd_roam_manual_conf);
}
#endif
