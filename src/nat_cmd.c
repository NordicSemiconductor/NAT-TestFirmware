/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <modem/at_cmd.h>
#include <shell/shell.h>
#include <shell/shell_uart.h>
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
        shell_print(shell, "Error while processing AT command: %d\n", err);
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

static void handle_set_timeout(const struct shell *shell, size_t argc, char **argv)
{
    long value;

    if (argc <= 1) {
        shell_print(shell, "Timeout value was not provided\n");
        return;
    } else {
        char *unused;
        value = strtol(argv[1], &unused, 10);
        if (value <= 0) {
            shell_print(shell, "Timeout value needs to be > 0\n");
        }
    }

    if (!strcmp(argv[-2], "udp")) {
        set_initial_timeout(TEST_UDP, value);
        if (get_initial_timeout(TEST_UDP) == value) {
            shell_print(shell, "UDP timeout multiplier set to: %d", value);
        }
    } else if (!strcmp(argv[-2], "tcp")) {
        set_initial_timeout(TEST_TCP, value);
        if (get_timeout_multiplier(TEST_TCP) == value) {
            shell_print(shell, "TCP timeout multiplier set to: %d", value);
        }
    }
}

static void handle_get_timeout(const struct shell *shell, size_t argc, char **argv)
{
    if (!strcmp(argv[-2], "udp")) {
        shell_print(shell, "UDP initial timeout: %d\n", get_initial_timeout(TEST_UDP));
    } else if (!strcmp(argv[-2], "tcp")) {
        shell_print(shell, "TCP initial timeout: %d\n", get_initial_timeout(TEST_TCP));
    }
}

static void handle_set_multiplier(const struct shell *shell, size_t argc, char **argv)
{
    float value;

    if (argc <= 1) {
        shell_print(shell, "Multiplier value was not provided\n");
        return;
    } else {
        char *unused;
        value = strtof(argv[1], &unused);
        if (value <= 1) {
            shell_print(shell, "Multiplier value needs to be > 1\n");
        }
    }

    if (!strcmp(argv[-2], "udp")) {
        set_timeout_multiplier(TEST_UDP, value);
        if (get_timeout_multiplier(TEST_UDP) == value) {
            shell_print(shell, "UDP timeout multiplier set to: %s", argv[1]);
        }
    } else if (!strcmp(argv[-2], "tcp")) {
        set_timeout_multiplier(TEST_TCP, value);
        if (get_timeout_multiplier(TEST_TCP) == value) {
            shell_print(shell, "TCP timeout multiplier set to: %s", argv[1]);
        }
    }
}

static void handle_get_multiplier(const struct shell *shell, size_t argc, char **argv)
{
    char msg[40];
    if (!strcmp(argv[-2], "udp")) {
        snprintf(msg, 40, "UDP timeout multiplier: %.1f\n", get_timeout_multiplier(TEST_UDP));
        shell_print(shell, "%s", msg);
    } else if (!strcmp(argv[-2], "tcp")) {
        snprintf(msg, 40, "TCP timeout multiplier: %.1f\n", get_timeout_multiplier(TEST_TCP));
        shell_print(shell, "%s", msg);
    }
}

static void handle_start_test(const struct shell *shell, size_t argc, char **argv)
{
    int err = -1;

    if (!strcmp(argv[0], "udp")) {
        err = nat_test_start(TEST_UDP, shell);
    } else if (!strcmp(argv[0], "tcp")) {
        err = nat_test_start(TEST_TCP, shell);
    } else if (!strcmp(argv[0], "udp_and_tcp")) {
        err = nat_test_start(TEST_UDP_AND_TCP, shell);
    }

    if (err < 0) {
        shell_print(shell, "Another test is still active\n");
        return;
    }
}

static void handle_stop_test(const struct shell *shell, size_t argc, char **argv)
{
    int err = nat_test_stop();
    if (err < 0) {
        shell_print(shell, "Unable to stop running test\nTry again in a few minutes\n");
        return;
    }
    shell_print(shell, "Test stopped\n");
}

static void handle_get_network_mode(const struct shell *shell, size_t argc, char **argv)
{
    shell_print(shell, "Network mode: %d\n", get_network_mode());
}

static void handle_set_network_mode(const struct shell *shell, size_t argc, char **argv)
{
    char *unused;
    long value = strtol(argv[1], &unused, 10);
    int err = set_network_mode(value);
    if (err == -INVALID_MODE) {
        shell_print(shell, "Invalid mode\n");
    } else if (err == -TEST_RUNNING) {
        shell_print(shell, "Active test - Unable to change mode\n");
    }

    shell_print(shell, "Changed network mode to %d\n", value);
}

static void handle_get_network_state(const struct shell *shell, size_t argc, char **argv)
{
    shell_print(shell, "Network connection state: %d\n", get_network_state());
}

SHELL_STATIC_SUBCMD_SET_CREATE(network_mode_accessor_cmds,
                               SHELL_CMD(set, NULL, "Set network mode", handle_set_network_mode),
                               SHELL_CMD(get, NULL, "Get network mode", handle_get_network_mode),
                               SHELL_SUBCMD_SET_END);
SHELL_STATIC_SUBCMD_SET_CREATE(network_conf_cmds,
                               SHELL_CMD(mode, &network_mode_accessor_cmds, "Configure netowkr mode", NULL),
                               SHELL_CMD(state, NULL, "Get network state", handle_get_network_state),
                               SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(test_timeout_accessor_cmds,
                               SHELL_CMD(set, NULL, "Set initial timeout", handle_set_timeout),
                               SHELL_CMD(get, NULL, "Get initial timeout", handle_get_timeout),
                               SHELL_SUBCMD_SET_END);
SHELL_STATIC_SUBCMD_SET_CREATE(test_multiplier_accessor_cmds,
                               SHELL_CMD(set, NULL, "Set timeout multiplier", handle_set_multiplier),
                               SHELL_CMD(get, NULL, "Get timeout multiplier", handle_get_multiplier),
                               SHELL_SUBCMD_SET_END);
SHELL_STATIC_SUBCMD_SET_CREATE(test_conf_cmds,
                               SHELL_CMD(initial_timeout, &test_timeout_accessor_cmds, "Configure initial timeout", NULL),
                               SHELL_CMD(timeout_multiplier, &test_multiplier_accessor_cmds, "Configure timeout multiplier", NULL),
                               SHELL_SUBCMD_SET_END);
SHELL_STATIC_SUBCMD_SET_CREATE(test_conf_types_cmds,
                               SHELL_CMD(udp, &test_conf_cmds, "Configure UDP test parameters", NULL),
                               SHELL_CMD(tcp, &test_conf_cmds, "Configure TCP test parameters", NULL),
                               SHELL_SUBCMD_SET_END);
SHELL_STATIC_SUBCMD_SET_CREATE(conf_cmds,
                               SHELL_CMD(test, &test_conf_types_cmds, "Read/Edit test parameters", NULL),
                               SHELL_CMD(network, &network_conf_cmds, "Read/Edit network parameters", NULL),
                               SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(config, &conf_cmds, "Read/Edit NAT-test client parameters", NULL);

SHELL_CMD_REGISTER(stop_running_test, NULL, "Stop running test", handle_stop_test);

SHELL_STATIC_SUBCMD_SET_CREATE(test_types,
                               SHELL_CMD(udp, NULL, "Start UDP test", handle_start_test),
                               SHELL_CMD(tcp, NULL, "Start TCP test", handle_start_test),
                               SHELL_CMD(udp_and_tcp, NULL, "Start first UDP test and then TCP test", handle_start_test),
                               SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(start, &test_types, "Start test", NULL);
