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

volatile enum lte_lc_nw_reg_status network_status;
volatile enum lte_lc_system_mode network_mode;

struct k_sem getaddrinfo_sem;

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

	return 0;
}

int get_network_status(void)
{
	return network_status;
}

static void lte_handler(const struct lte_lc_evt *const evt)
{
	static int reconnect_counter = 0;
	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		switch (evt->nw_reg_status) {
		case LTE_LC_NW_REG_REGISTERED_HOME:
		case LTE_LC_NW_REG_REGISTERED_ROAMING:
			reconnect_counter = 0;
			break;
		case LTE_LC_NW_REG_SEARCHING:
			break;
		default:
			if (reconnect_counter <
			    CONFIG_NAT_TEST_MAX_LTE_RECONNECT_ATTEMPTS) {
				printk("LTE link not maintained.\nAttempting to reconnect...\n");
				lte_lc_offline();
				lte_lc_system_mode_set(network_mode);
				lte_lc_normal();
				reconnect_counter++;
			} else {
				reconnect_counter = 0;
				printk("LTE link could not be established.\n");
				if (IS_ENABLED(
					    CONFIG_NAT_TEST_RESET_WHEN_UNABLE_TO_CONNECT)) {
					printk("Resetting...\n");
					sys_reboot(SYS_REBOOT_WARM);
				}
			}
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
		printk("Error initializing and connecting to lte, error: %d\n",
		       err);
		return;
	}

	while ((network_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
	       (network_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
		k_sleep(K_SECONDS(1));
	}
	printk("LTE connected\n");

	k_sem_init(&getaddrinfo_sem, 0, 1);

	cJSON_Init();

	err = modem_info_init();
	if (err) {
		printk("Modem info could not be initialised: %d\n", err);
		return;
	}
	nat_test_init();
	nat_cmd_init();
	k_sem_give(&getaddrinfo_sem);

	nat_test_start(TEST_UDP_AND_TCP);
}
