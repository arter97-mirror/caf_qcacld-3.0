/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include <wlan_hdd_wondertap.h>

/**
 * wlan_hdd_wondertap_init() - Initialize wondertap interface
 * @handle: Pointer to store the wondertap handle
 * @params: Initialization parameters for wondertap interface
 *
 * This function initializes the wondertap interface with the provided
 * parameters. It allocates necessary resources and prepares the interface
 * for operation.
 *
 * Return: 0 on success, negative error code on failure
 */
static
int wlan_hdd_wondertap_init(void **handle,
			    const qdf_wondertap_init_params_t *params)
{
	return 0;
}

/**
 * wlan_hdd_wondertap_deinit() - Deinitialize wondertap interface
 * @handle: Wondertap handle to deinitialize
 * @params: deinit parameters
 *
 * This function deinitializes the wondertap interface and releases all
 * resources allocated during initialization.
 *
 * Return: None
 */
static
void wlan_hdd_wondertap_deinit(void *handle,
			       const qdf_wondertap_deinit_params_t *params)
{
}

/**
 * wlan_hdd_wondertap_set_freq() - Set operating frequency
 * @handle: Wondertap handle
 * @params: Channel parameters including frequency and bandwidth
 *
 * This function configures the operating frequency and bandwidth
 * for the wondertap interface based on the provided parameters.
 *
 * Return: 0 on success, negative error code on failure
 */
static
int wlan_hdd_wondertap_set_freq(void *handle,
				const qdf_wondertap_set_freq_params_t *params)
{
	return 0;
}

/**
 * wlan_hdd_wondertap_set_filter() - Configure a specific hardware packet
 *  filter
 * @handle: Wondertap handle
 * @filter_type: type of filter to configure
 * @params: void pointer to filter-specific parameter structure.
 *
 * This function configures a specific hardware packet
 * for the wondertap interface based on the provided parameters.
 *
 * Return: 0 on success, negative error code on failure
 */
static
int wlan_hdd_wondertap_set_filter(void *handle,
				  qdf_wondertap_filter_type_t filter_type,
				  const void *params)
{
	return 0;
}

/**
 * wlan_hdd_wondertap_set_fixed_tx_rate() - Set fixed TX rate
 * @handle: Wondertap handle
 * @params: TX rate parameters including MCS, NSS, and preamble type
 *
 * This function configures a fixed transmission rate for the wondertap
 * interface. When set, all packets will be transmitted at the specified
 * rate instead of using rate adaptation.
 *
 * Return: 0 on success, negative error code on failure
 */
static int
wlan_hdd_wondertap_set_fixed_tx_rate(void *handle,
				const qdf_wondertap_tx_rate_params_t *params)
{
	return 0;
}

/**
 * wlan_hdd_wondertap_set_tx_rate_mask() - Set TX rate mask
 * @handle: Wondertap handle
 * @params: TX rate mask parameters specifying allowed rates
 *
 * This function configures a mask of allowed transmission rates for the
 * wondertap interface. The rate adaptation algorithm will only select
 * rates that are enabled in the mask.
 *
 * Return: 0 on success, negative error code on failure
 */
static int
wlan_hdd_wondertap_set_tx_rate_mask(void *handle,
			const qdf_wondertap_tx_rate_mask_params_t *params)
{
	return 0;
}

/**
 * wlan_hdd_wondertap_get_capabilities() - Populate supported capabilities
 * @handle: Wondertap handle
 * @features: Pointer to structure to store supported features
 *
 * This function populates the list of features that the
 * driver supports for the wondertap operation.
 *
 * Return: 0 on success, negative error code on failure
 */
static int
wlan_hdd_wondertap_get_capabilities(void *handle,
				    qdf_wondertap_capability_t *features)
{
	return 0;
}

/**
 * wlan_drv_wondertap_ops - Wondertap operations structure
 *
 * This structure defines the set of operations that the WLAN driver
 * provides to the wondertap framework. It includes callbacks for
 * initialization, configuration, and feature queries.
 */
static qdf_wondertap_ops_t wlan_drv_wondertap_ops = {
	.init = wlan_hdd_wondertap_init,
	.deinit = wlan_hdd_wondertap_deinit,
	.set_freq = wlan_hdd_wondertap_set_freq,
	.set_filter = wlan_hdd_wondertap_set_filter,
	.set_fixed_tx_rate = wlan_hdd_wondertap_set_fixed_tx_rate,
	.set_tx_rate_mask = wlan_hdd_wondertap_set_tx_rate_mask,
	.get_capabilities = wlan_hdd_wondertap_get_capabilities,
};

int wlan_hdd_wondertap_register_ops(void)
{
	return qdf_wondertap_register_ops(&wlan_drv_wondertap_ops);
}

void wlan_hdd_wondertap_unregister_ops(void)
{
	qdf_wondertap_unregister_ops(&wlan_drv_wondertap_ops);
}

void hdd_sme_passthrough_mode_callback(uint8_t vdev_id, bool is_up)
{
}
