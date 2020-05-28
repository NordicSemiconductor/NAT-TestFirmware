/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <cJSON.h>
#include <cJSON_os.h>
#include <modem/at_cmd.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#include <net/socket.h>
#include <shell/shell.h>
#include <shell/shell_uart.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr.h>

#include "nat_test.h"

#define AT_CMD_SERVER_PORT 3060
#define WAIT_TIME_S 3
#define THREAD_STACK_SIZE 8192
#define AT_LOG_TIMEOUT_S 20
#define AT_BUF_SIZE 200

struct at_cmd_log {
	void *fifo_reserved;
	char cmd[AT_BUF_SIZE];
	char res[AT_BUF_SIZE];
};

K_FIFO_DEFINE(at_cmd_fifo);

K_THREAD_STACK_DEFINE(nat_cmd_thread_stack_area, THREAD_STACK_SIZE);
struct k_thread thread;

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
		struct at_cmd_log *item;
		item = k_malloc(sizeof(*item));

		if (item == NULL) {
			shell_print(shell,
				    "Failed to allocate at_cmd_log item.\n");
			break;
		}
		strcpy(item->cmd, argv[1]);
		strcpy(item->res, response);

		k_fifo_put(&at_cmd_fifo, (void *)item);
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

	if (argc <= 1) {
		shell_print(shell, "Multiplier value was not provided\n");
		return;
	} else {
		char *unused;
		value = strtof(argv[1], &unused);
		if (value <= 1) {
			shell_print(shell,
				    "Multiplier value needs to be > 1\n");
		}
	}

	if (!strcmp(argv[-2], "udp")) {
		udp_timeout_multiplier = value;
		shell_print(shell, "UDP timeout multiplier set to: %s",
			    udp_timeout_multiplier);
	} else if (!strcmp(argv[-2], "tcp")) {
		tcp_timeout_multiplier = value;
		shell_print(shell, "TCP timeout multiplier set to: %s",
			    tcp_timeout_multiplier);
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

	err = lte_lc_psm_req(false);
	if (err < 0) {
		shell_print(
			shell,
			"Failed to disable Power Saving mode.\nRequest to start test denied.\n");
		return;
	}

	err = lte_lc_edrx_req(false);
	if (err < 0) {
		shell_print(
			shell,
			"Failed to disable use of eDRX.\nRequest to start test denied.\n");
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
	int err = nat_test_stop();
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
	shell_print(shell, "Network mode: %d\n", get_network_mode());
}

static void handle_set_network_mode(const struct shell *shell, size_t argc,
				    char **argv)
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

static int json_add_obj(cJSON *parent, const char *str, cJSON *item)
{
	cJSON_AddItemToObject(parent, str, item);

	return 0;
}

static int json_add_str(cJSON *parent, const char *str, const char *item)
{
	cJSON *json_str;

	json_str = cJSON_CreateString(item);
	if (json_str == NULL) {
		return -ENOMEM;
	}

	return json_add_obj(parent, str, json_str);
}

static int create_send_buffer(struct modem_param_info *modem_params,
			      const struct at_cmd_log *item, char *buffer)
{
	int ret = 0;
	cJSON *root_obj = cJSON_CreateObject();

	if (root_obj == NULL) {
		printk("Failed to create json root object\n");
		return -ENOMEM;
	}

	ret += json_add_str(
		root_obj, "op",
		modem_params->network.current_operator.value_string);
	ret += json_add_str(root_obj, "iccid",
			    modem_params->sim.iccid.value_string);
	ret += json_add_str(root_obj, "imei",
			    modem_params->device.imei.value_string);
	ret += json_add_str(root_obj, "cmd", item->cmd);
	ret += json_add_str(root_obj, "result", item->res);

	if (ret) {
		printk("Failed to add json value\n");
		ret = -ENOMEM;
		goto exit;
	}

	char *root_obj_string = cJSON_Print(root_obj);
	if (root_obj_string == NULL) {
		printk("Failed to print json object\n");
		ret = -1;
		goto exit;
	}

	ret = strlen(root_obj_string);
	memcpy(buffer, root_obj_string, ret);
	cJSON_FreeString(root_obj_string);

exit:
	cJSON_Delete(root_obj);

	return ret;
}

static int send_data(int client_fd, struct at_cmd_log *item)
{
	int err;
	char send_buf[BUF_SIZE] = { 0 };
	int send_len;
	struct modem_param_info modem_params = { 0 };

	err = modem_info_params_init(&modem_params);
	if (err) {
		printk("Modem info params could not be initialised: %d\n", err);
		return -1;
	}

	err = modem_info_params_get(&modem_params);
	if (err < 0) {
		printk("Unable to obtain modem parameters: %d\n", err);
		return -ENOTCONN;
	}

	send_len = create_send_buffer(&modem_params, item, send_buf);
	if (send_len < 0) {
		return -1;
	}

	/* send len + 1 for null terminated packet */
	err = send(client_fd, send_buf, send_len + 1, 0);
	if (err < 0) {
		printk("Failed to send data, errno: %d\n", errno);

		return -ENOTCONN;
	}

	printk("AT cmd and result sent: %s\n", send_buf);
	return 0;
}

static int setup_connection(int *client_fd, struct pollfd *fds)
{
	int err;
	struct addrinfo *res;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};

	int network_status = get_network_status();
	while ((network_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
	       (network_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
		network_status = get_network_status();

		/* Trigger a connect attempt only when device can exhaust its reconnect attempts without restarting */
		if (!IS_ENABLED(CONFIG_NAT_TEST_RESET_WHEN_UNABLE_TO_CONNECT)) {
			if (network_status != LTE_LC_NW_REG_SEARCHING) {
				lte_lc_offline();
				lte_lc_system_mode_set(get_network_mode());
				lte_lc_normal();
			}
		}
		k_sleep(K_SECONDS(CONFIG_LTE_NETWORK_TIMEOUT / 3));
	}

	*client_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (client_fd <= 0) {
		printk("socket() failed, errno: %d\n", errno);
		return -1;
	}

	k_sem_take(&getaddrinfo_sem, K_FOREVER);
	err = getaddrinfo(SERVER_HOSTNAME, NULL, &hints, &res);
	if (err) {
		printk("getaddrinfo() failed, err %d\n", errno);
		k_sem_give(&getaddrinfo_sem);
		return -1;
	}
	k_sem_give(&getaddrinfo_sem);

	((struct sockaddr_in *)res->ai_addr)->sin_port =
		htons(AT_CMD_SERVER_PORT);

	err = connect(*client_fd, res->ai_addr, sizeof(struct sockaddr_in));
	if (err) {
		printk("connect failed, errno: %d\n\r", errno);
		return -1;
	}

	fds->fd = *client_fd;
	fds->events = POLLIN;

	return 0;
}

static void thread_entry_point(void *param, void *unused, void *unused2)
{
	struct k_fifo *fifo = (struct k_fifo *)param;
	struct pollfd fds = { 0 };
	char recv_buf[BUF_SIZE];
	int client_fd = 0;
	int err;
	int ret_len;
	bool connected;

	err = setup_connection(&client_fd, &fds);
	if (err < 0) {
		connected = false;
	} else {
		connected = true;
	}
	while (true) {
		while (!k_fifo_is_empty(fifo)) {
			if (!connected) {
				goto reconnect;
			}

			struct at_cmd_log *item =
				(struct at_cmd_log *)k_fifo_get(fifo,
								K_NO_WAIT);

			/* Should not ever be NULL */
			if (item != NULL) {
				printk("Item: %s, %s\n", item->cmd, item->res);
				err = send_data(client_fd, item);
				if (err == ENOTCONN) {
					k_fifo_put(fifo, (void *)item);
					goto reconnect;
				} else if (err < 0) {
					free(item);
					printk("Exiting AT command client thread.\n");
					return;
				}

				free(item);

				memset(recv_buf, 0, BUF_SIZE);

				err = poll(&fds, 1,
					   AT_LOG_TIMEOUT_S * S_TO_MS_MULT);
				if (err < 0) {
					printk("poll, error: %d", err);
					goto reconnect;
				} else if (err == 0) {
					printk("No response from server.\nAppending AT log back into fifo.\n");
					k_fifo_put(fifo, (void *)item);
					goto reconnect;
				} else if ((fds.revents & POLLIN) == POLLIN) {
					ret_len = recv(client_fd, recv_buf,
						       sizeof(recv_buf) - 1, 0);
					if (ret_len > 0 && ret_len < BUF_SIZE) {
						recv_buf[ret_len] = 0;

						if (strstr(recv_buf, "error") !=
							    NULL ||
						    strstr(recv_buf, "Error") !=
							    NULL) {
							printk("Response: %s\nExiting AT command client thread.\n",
							       recv_buf);
							return;
						}

						printk("Response: %s\n",
						       recv_buf);
					}
				}
			}
		}

		k_sleep(K_SECONDS(WAIT_TIME_S));

		continue;

	reconnect:
		(void)close(client_fd);

		connected = false;

		err = setup_connection(&client_fd, &fds);
		if (err < 0) {
			printk("Failed to reconnect to server.\nEmptying fifo.\n");
			/* Empty queue */
			while (!k_fifo_is_empty(fifo)) {
				k_fifo_get(fifo, K_NO_WAIT);
			}
			continue;
		}

		connected = true;
	}
}

void nat_cmd_init(void)
{
	k_thread_create(&thread, nat_cmd_thread_stack_area, THREAD_STACK_SIZE,
			thread_entry_point, (void *)&at_cmd_fifo, NULL, NULL,
			THREAD_PRIORITY, 0, K_NO_WAIT);
}
