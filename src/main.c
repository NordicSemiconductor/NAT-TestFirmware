/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <zephyr/types.h>
#include <cJSON_os.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#include <power/reboot.h>

#include "nat_test.h"

K_SEM_DEFINE(lte_connected_startup, 0, 1);
volatile enum lte_lc_nw_reg_status network_status;
volatile enum lte_lc_system_mode network_mode;

int get_network_mode(void)
{
	return network_mode;
}

int set_network_mode(int mode)
{
	if (mode != LTE_LC_SYSTEM_MODE_LTEM &&
	    mode != LTE_LC_SYSTEM_MODE_NBIOT) {
		return -INVALID_MODE;
	} else if (get_test_state() != IDLE) {
		return -TEST_RUNNING;
	} else if (network_mode == mode) {
		return 0;
	}

	network_mode = mode;

	lte_lc_offline();
	lte_lc_system_mode_set(mode);
	lte_lc_normal();

	return 0;
}

int get_network_status(void)
{
	return network_status;
}

static void lte_handler(const struct lte_lc_evt *const evt)
{
	s64_t start_time_ms = 0;
	s64_t total_time_ms = 0;

	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		switch (evt->nw_reg_status) {
		case LTE_LC_NW_REG_REGISTERED_HOME:
		case LTE_LC_NW_REG_REGISTERED_ROAMING:
			start_time_ms = 0;

			/* Only used during startup */
			k_sem_give(&lte_connected_startup);
			break;
		case LTE_LC_NW_REG_SEARCHING:
		case LTE_LC_NW_REG_UNKNOWN:
			if (network_status == LTE_LC_NW_REG_REGISTERED_HOME ||
			    network_status ==
				    LTE_LC_NW_REG_REGISTERED_ROAMING) {
				start_time_ms = k_uptime_get();
			} else if (start_time_ms > 0) {
				total_time_ms += k_uptime_delta(&start_time_ms);
			}

			if (total_time_ms >= CONFIG_LTE_NETWORK_TIMEOUT) {
				printk("LTE link could not be established.\n");
				printk("Rebooting...\n");
				sys_reboot(SYS_REBOOT_WARM);
			}
			break;
		case LTE_LC_NW_REG_REGISTRATION_DENIED:
		case LTE_LC_NW_REG_UICC_FAIL:
			printk("LTE link could not be established.\n");
			printk("Rebooting...\n");
			sys_reboot(SYS_REBOOT_WARM);
			break;
		default:
			break;
		}

		network_status = evt->nw_reg_status;

		break;
	default:
		break;
	}
}

void main(void)
{
	int err;

	printk("NAT-test client started\n");
	printk("Version: %s\n", CONFIG_NAT_TEST_VERSION);

	if (IS_ENABLED(CONFIG_LTE_NETWORK_MODE_NBIOT)) {
		network_mode = LTE_LC_SYSTEM_MODE_NBIOT;
	} else {
		network_mode = LTE_LC_SYSTEM_MODE_LTEM;
	}
	network_status = LTE_LC_NW_REG_NOT_REGISTERED;

	printk("Setting up LTE connection\n");

	err = lte_lc_init_and_connect_async(lte_handler);
	if (err) {
		printk("Error initializing and connecting to LTE, error: %d\n",
		       err);
		return;
	}

	k_sem_take(&lte_connected_startup, K_FOREVER);

	printk("LTE connected\n");

	cJSON_Init();

	err = modem_info_init();
	if (err) {
		printk("Modem info could not be initialised: %d\n", err);
		return;
	}

	nat_test_init();

	err = nat_test_start(TEST_UDP_AND_TCP);
	if (err) {
		printk("Test was already running.\n");
	}
}
