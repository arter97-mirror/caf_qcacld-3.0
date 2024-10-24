/*
 * Copyright (c) 2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021, 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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
 * DOC: wlan_hdd_sysfs_11k_neighbor_req.c
 *
 * WLAN Host Device Driver implementation to create sysfs
 * 11k_neighbor_req
 */
#include "wlan_hdd_main.h"
#include "osif_psoc_sync.h"
#include "osif_vdev_sync.h"
#include "wlan_dsc_test.h"
#include "wlan_hdd_sysfs.h"
#include "wlan_module_ids.h"
#include "wma.h"
static ssize_t __hdd_sysfs_11k_neighbor_req_store(struct net_device *net_dev,
				const char __user *buf, size_t count)
{
	struct hdd_adapter *adapter = netdev_priv(net_dev);
	struct hdd_context *hdd_ctx;
	char *buf_local = NULL;
	int ret;
	tRrmNeighborReq request;
	tRrmNeighborRspCallbackInfo callback;
	bool rrm_enabled = false;

	if (hdd_validate_adapter(adapter))
		return -EINVAL;

	hdd_ctx = WLAN_HDD_GET_CTX(adapter);
	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret)
		return ret;

	ucfg_wlan_mlme_get_rrm_enabled(hdd_ctx->psoc, &rrm_enabled);
	if (rrm_enabled) {

		if (!wlan_hdd_validate_modules_state(hdd_ctx))
			return -EINVAL;

		if (count > MAX_CMD_INPUT)
			return -EINVAL;

		buf_local = (char *)qdf_mem_malloc(sizeof(char) * count);
		if (!buf_local)
			return -EINVAL;

		strscpy(buf_local, buf, count);
		buf_local[count - 1] = '\0';

		request.neighbor_report_offload = false;
		request.no_ssid = count ? false : true;
		if (!request.no_ssid) {
			request.ssid.length = count - 1;
			qdf_mem_copy(request.ssid.ssId,
				     buf_local,
				     request.ssid.length);
		}
		callback.neighborRspCallback = NULL;
		callback.neighborRspCallbackContext = NULL;
		callback.timeout = 5000;
		sme_neighbor_report_request(
				hdd_ctx->mac_handle,
				adapter->deflink->vdev_id,
				&request,
				&callback);
	}
	qdf_mem_free(buf_local);

	return count;
}

static ssize_t hdd_sysfs_11k_neighbor_req_store(struct device *dev,
						struct device_attribute *attr,
						char const *buf, size_t count)
{
	struct net_device *net_dev = container_of(dev, struct net_device, dev);
	struct osif_vdev_sync *vdev_sync;
	ssize_t errno_size;

	errno_size = osif_vdev_sync_op_start(net_dev, &vdev_sync);
	if (errno_size)
		return errno_size;

	errno_size = __hdd_sysfs_11k_neighbor_req_store(
				net_dev, buf, count);
	if (errno_size < 0)
		hdd_err_rl("errno_size %zd", errno_size);

	osif_vdev_sync_op_stop(vdev_sync);

	return errno_size;
}

static DEVICE_ATTR(11k_neighbor_req, 0220,
		   NULL, hdd_sysfs_11k_neighbor_req_store);

int hdd_sysfs_11k_neighbor_req_create(struct hdd_adapter *adapter)
{
	int error;

	error = device_create_file(&adapter->dev->dev,
				   &dev_attr_11k_neighbor_req);
	if (error)
		hdd_err("could not create 11k_neighbor_req sysfs file");

	return error;
}

void hdd_sysfs_11k_neighbor_req_destroy(struct hdd_adapter *adapter)
{
	device_remove_file(&adapter->dev->dev, &dev_attr_11k_neighbor_req);
}

