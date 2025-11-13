/*
 * Copyright (c) 2011-2020 The Linux Foundation. All rights reserved.
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
 * DOC: wlan_hdd_sysfs_antenna_chainmask.c
 *
 * implementation for creating sysfs file antenna_chainmask
 */

#include <wlan_hdd_includes.h>
#include <wlan_hdd_main.h>
#include "osif_vdev_sync.h"
#include <wlan_hdd_sysfs.h>
#include <wlan_hdd_sysfs_antenna_chainmask.h>
#include <wma_api.h>
#include <wma.h>
#include <wmi_unified.h>

static ssize_t
__hdd_sysfs_antenna_chainmask_show(struct net_device *net_dev,
				   char *buf)
{
	struct hdd_adapter *adapter = netdev_priv(net_dev);
	struct hdd_context *hdd_ctx;
	uint8_t tx_mask;
	uint8_t rx_mask;
	int ret;
	ssize_t count;
	QDF_STATUS status;

	hdd_enter_dev(net_dev);

	if (hdd_validate_adapter(adapter)){
		hdd_exit();
		return -EINVAL;
	}

	hdd_ctx = WLAN_HDD_GET_CTX(adapter);
	ret = wlan_hdd_validate_context(hdd_ctx);

	if (ret) return ret;

	status = ucfg_mlme_get_chain_mask(hdd_ctx->psoc, &tx_mask,
					  &rx_mask);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("failed to get TX, RX chain mask from mlme");
		return -EINVAL;
	}

	/* if 0 return max value as 0 mean no set cmnd received yet */
	if (!tx_mask)
		tx_mask = hdd_ctx->num_rf_chains == HDD_ANTENNA_MODE_2X2 ?
				HDD_CHAIN_MODE_2X2 : HDD_CHAIN_MODE_1X1;
	if (!rx_mask)
		rx_mask = hdd_ctx->num_rf_chains == HDD_ANTENNA_MODE_2X2 ?
				HDD_CHAIN_MODE_2X2 : HDD_CHAIN_MODE_1X1;

	count = scnprintf(buf, PAGE_SIZE, "Configured Antenna TX %d, RX %d \n",
				tx_mask, rx_mask);
	hdd_exit();
	return count;
}

static ssize_t
hdd_sysfs_antenna_chainmask_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct net_device *net_dev = container_of(dev, struct net_device, dev);
	struct osif_vdev_sync *vdev_sync;
	ssize_t err_size;

	err_size = osif_vdev_sync_op_start(net_dev, &vdev_sync);
	if (err_size)
		return err_size;

	err_size = __hdd_sysfs_antenna_chainmask_show(net_dev, buf);

	osif_vdev_sync_op_stop(vdev_sync);

	return err_size;
}

static DEVICE_ATTR(antenna_chainmask, 0440,
		   hdd_sysfs_antenna_chainmask_show, NULL);

int hdd_sysfs_antenna_chainmask_create(struct hdd_adapter *adapter)
{
	int error;

	error = device_create_file(&adapter->dev->dev,
				   &dev_attr_antenna_chainmask);
	if (error)
		hdd_err("could not create antenna_chainmask sysfs file");

	return error;
}

void hdd_sysfs_antenna_chainmask_destroy(struct hdd_adapter *adapter)
{
	device_remove_file(&adapter->dev->dev, &dev_attr_antenna_chainmask);
}
