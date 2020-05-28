/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <modem/at_cmd.h>
#include <modem/lte_lc.h>
#include <net/socket.h>
#include <shell/shell.h>
#include <shell/shell_uart.h>
#include <modem/lte_lc.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr.h>

#include "nat_test.h"

static void handle_at_cmd(const struct shell *shell, size_t argc, char **argv)
{
	int err;
	char response[200];
	enum at_cmd_state state;

	if (argc <= 1) {
		shell_print(shell, "AT command was not provided\n");
		return;
	}

	err = at_cmd_write(argv[1], response, sizeof(response), &state);
	if (err < 0) {
		shell_print(shell, "Error while processing AT command: %d\n",
			    err);
		state = AT_CMD_ERROR;
	}

	switch (state) {
	case AT_CMD_OK:
		shell_print(shell, "%s\n", response);
		shell_print(shell, "OK\n");
		break;
	case AT_CMD_ERROR:
		shell_print(shell, "ERROR\n");
		break;
	default:
		break;
	}
}

SHELL_CMD_REGISTER(at, NULL, "AT commands", handle_at_cmd);

static void handle_set_timeout(const struct shell *shell, size_t argc,
			       char **argv)
{
	u32_t value;

	if (argc <= 1) {
		shell_print(shell, "Timeout value was not provided\n");
		return;
	}

	value = strtol(argv[1], NULL, 10);
	if (value <= 0) {
		shell_print(shell, "Timeout value needs to be > 0\n");
	}

	if (!strcmp(argv[-2], "udp")) {
		udp_initial_timeout = value;
		shell_print(shell, "UDP timeout multiplier set to: %d",
			    udp_initial_timeout);
	} else if (!strcmp(argv[-2], "tcp")) {
		tcp_initial_timeout = value;
		shell_print(shell, "TCP timeout multiplier set to: %d",
			    tcp_initial_timeout);
	}
}

static void handle_get_timeout(const struct shell *shell, size_t argc,
			       char **argv)
{
	if (!strcmp(argv[-2], "udp")) {
		shell_print(shell, "UDP initial timeout: %d\n",
			    udp_initial_timeout);
	} else if (!strcmp(argv[-2], "tcp")) {
		shell_print(shell, "TCP initial timeout: %d\n",
			    tcp_initial_timeout);
	}
}

static void handle_set_multiplier(const struct shell *shell, size_t argc,
				  char **argv)
{
	float value;
	char msg[40];

	if (argc <= 1) {
		shell_print(shell, "Multiplier value was not provided\n");
		return;
	}

	value = strtof(argv[1], NULL);
	if (value <= 1) {
		shell_print(shell, "Multiplier value needs to be > 1\n");
	}

	if (!strcmp(argv[-2], "udp")) {
		udp_timeout_multiplier = value;
		snprintf(msg, 40, "UDP timeout multiplier set to: %.1f\n",
			 udp_timeout_multiplier);
		shell_print(shell, "%s", msg);
	} else if (!strcmp(argv[-2], "tcp")) {
		tcp_timeout_multiplier = value;
		snprintf(msg, 40, "TCP timeout multiplier set to: %.1f\n",
			 tcp_timeout_multiplier);
		shell_print(shell, "%s", msg);
	}
}

static void handle_get_multiplier(const struct shell *shell, size_t argc,
				  char **argv)
{
	char msg[40];

	if (!strcmp(argv[-2], "udp")) {
		snprintf(msg, 40, "UDP timeout multiplier: %.1f\n",
			 udp_timeout_multiplier);
		shell_print(shell, "%s", msg);
	} else if (!strcmp(argv[-2], "tcp")) {
		snprintf(msg, 40, "TCP timeout multiplier: %.1f\n",
			 tcp_timeout_multiplier);
		shell_print(shell, "%s", msg);
	}
}

static void handle_start_test(const struct shell *shell, size_t argc,
			      char **argv)
{
	int err;
	enum test_type type;

	if (!strcmp(argv[0], "udp")) {
		type = TEST_UDP;
	} else if (!strcmp(argv[0], "tcp")) {
		type = TEST_TCP;
	} else if (!strcmp(argv[0], "udp_and_tcp")) {
		type = TEST_UDP_AND_TCP;
	} else {
		shell_print(shell, "Invalid test type\n");
		return;
	}

	err = nat_test_start(type);
	if (err < 0) {
		shell_print(shell, "Another test is still active\n");
		return;
	}
}

static void handle_stop_test(const struct shell *shell, size_t argc,
			     char **argv)
{
	int err;

	err = nat_test_stop();
	if (err < 0) {
		shell_print(
			shell,
			"Unable to stop running test\nTry again in a few minutes\n");
		return;
	}
	shell_print(shell, "Test stopped\n");
}

static void handle_get_network_mode(const struct shell *shell, size_t argc,
				    char **argv)
{
	enum lte_lc_system_mode network_mode = get_network_mode();

	if (network_mode == LTE_LC_SYSTEM_MODE_NBIOT) {
		shell_print(shell, "Network mode: NB-IoT\n");
	} else if (network_mode == LTE_LC_SYSTEM_MODE_LTEM) {
		shell_print(shell, "Network mode: LTE-M\n");
	}
}

static void handle_set_network_mode(const struct shell *shell, size_t argc,
				    char **argv)
{
	long mode = strtol(argv[1], NULL, 10);
	int err = set_network_mode(mode);

	if (err == -INVALID_MODE) {
		shell_print(shell, "Invalid mode\n");
		return;
	} else if (err == -TEST_RUNNING) {
		shell_print(shell, "Active test - Unable to change mode\n");
		return;
	}

	if (mode == LTE_LC_SYSTEM_MODE_NBIOT) {
		shell_print(shell, "Changed network mode to NB-IoT\n");
	} else if (mode == LTE_LC_SYSTEM_MODE_LTEM) {
		shell_print(shell, "Changed network mode to LTE-M\n");
	}
}

static void handle_get_network_status(const struct shell *shell, size_t argc,
				      char **argv)
{
	shell_print(shell, "Network connection status: %d\n",
		    get_network_status());
}

SHELL_STATIC_SUBCMD_SET_CREATE(network_mode_accessor_cmds,
			       SHELL_CMD(set, NULL, "Set network mode",
					 handle_set_network_mode),
			       SHELL_CMD(get, NULL, "Get network mode",
					 handle_get_network_mode),
			       SHELL_SUBCMD_SET_END);
SHELL_STATIC_SUBCMD_SET_CREATE(network_conf_cmds,
			       SHELL_CMD(mode, &network_mode_accessor_cmds,
					 "Configure network mode", NULL),
			       SHELL_CMD(status, NULL, "Get network status",
					 handle_get_network_status),
			       SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(test_timeout_accessor_cmds,
			       SHELL_CMD(set, NULL, "Set initial timeout",
					 handle_set_timeout),
			       SHELL_CMD(get, NULL, "Get initial timeout",
					 handle_get_timeout),
			       SHELL_SUBCMD_SET_END);
SHELL_STATIC_SUBCMD_SET_CREATE(test_multiplier_accessor_cmds,
			       SHELL_CMD(set, NULL, "Set timeout multiplier",
					 handle_set_multiplier),
			       SHELL_CMD(get, NULL, "Get timeout multiplier",
					 handle_get_multiplier),
			       SHELL_SUBCMD_SET_END);
SHELL_STATIC_SUBCMD_SET_CREATE(test_conf_cmds,
			       SHELL_CMD(initial_timeout,
					 &test_timeout_accessor_cmds,
					 "Configure initial timeout", NULL),
			       SHELL_CMD(timeout_multiplier,
					 &test_multiplier_accessor_cmds,
					 "Configure timeout multiplier", NULL),
			       SHELL_SUBCMD_SET_END);
SHELL_STATIC_SUBCMD_SET_CREATE(test_conf_types_cmds,
			       SHELL_CMD(udp, &test_conf_cmds,
					 "Configure UDP test parameters", NULL),
			       SHELL_CMD(tcp, &test_conf_cmds,
					 "Configure TCP test parameters", NULL),
			       SHELL_SUBCMD_SET_END);
SHELL_STATIC_SUBCMD_SET_CREATE(conf_cmds,
			       SHELL_CMD(test, &test_conf_types_cmds,
					 "Read/Edit test parameters", NULL),
			       SHELL_CMD(network, &network_conf_cmds,
					 "Read/Edit network parameters", NULL),
			       SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(config, &conf_cmds, "Read/Edit NAT-test client parameters",
		   NULL);

SHELL_CMD_REGISTER(stop_running_test, NULL, "Stop running test",
		   handle_stop_test);

SHELL_STATIC_SUBCMD_SET_CREATE(
	test_types, SHELL_CMD(udp, NULL, "Start UDP test", handle_start_test),
	SHELL_CMD(tcp, NULL, "Start TCP test", handle_start_test),
	SHELL_CMD(udp_and_tcp, NULL, "Start first UDP test and then TCP test",
		  handle_start_test),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(start, &test_types, "Start test", NULL);
