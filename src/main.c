/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <net/socket.h>
#include <stdio.h>
#include <string.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#include <cJSON_os.h>
#include <cJSON.h>

#define SERVER_HOSTNAME "nat-test.thingy.rocks"
#define UDP_PORT 3050
#define TCP_PORT 3051
#define SEND_BUF_SIZE 512
#define RECV_BUF_SIZE 256
#define TCP_INITIAL_TIMEOUT 200
#define S_TO_MS_MULT 1000
#define POLL_TIMEOUT (60 * S_TO_MS_MULT)
#define TIMEOUT_TOL 5
#define INV_FORMAT_MSG "Error occured.\nConnection closed.\n"

enum protocol { TIMEOUT_UDP, TIMEOUT_TCP, NONE };

struct timeout_struct {
	int udp;
	int tcp;
} keep_alive_time;

static enum protocol current_protocol = TIMEOUT_UDP;

static char *get_protocol_name(enum protocol protocol)
{
	switch (protocol) {
	case TIMEOUT_UDP:
		return "UDP";
	case TIMEOUT_TCP:
		return "TCP";
	default:
		return "NONE";
	}
}

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

static int json_add_number(cJSON *parent, const char *str, double item)
{
	cJSON *json_num;

	json_num = cJSON_CreateNumber(item);
	if (json_num == NULL) {
		return -ENOMEM;
	}

	return json_add_obj(parent, str, json_num);
}

static int create_send_buffer(struct modem_param_info *modem_params,
			      char *buffer, int timeout_s)
{
	int ret = 0;
	cJSON *root_obj = cJSON_CreateObject();
	cJSON *ip_obj;
	const char *delim = " ";
	const char *ip_strings[10];
	int ip_count = 0;

	if (root_obj == NULL) {
		cJSON_Delete(root_obj);
		return -ENOMEM;
	}

	char *token =
		strtok(modem_params->network.ip_address.value_string, delim);
	while (token != NULL && ip_count < ARRAY_SIZE(ip_strings)) {
		ip_strings[ip_count] = token;
		token = strtok(NULL, delim);
		ip_count++;
	}

	ip_obj = cJSON_CreateStringArray(ip_strings, ip_count);
	if (ip_obj == NULL) {
		cJSON_Delete(ip_obj);
		return -ENOMEM;
	}

	ret += json_add_obj(root_obj, "ip", ip_obj);
	ret += json_add_str(
		root_obj, "op",
		modem_params->network.current_operator.value_string);
	ret += json_add_number(root_obj, "cell_id",
			       modem_params->network.cellid_dec);
	ret += json_add_number(root_obj, "ue_mode",
			       modem_params->network.ue_mode.value);
	ret += json_add_number(root_obj, "lte_mode",
			       modem_params->network.lte_mode.value);
	ret += json_add_number(root_obj, "nbiot_mode",
			       modem_params->network.nbiot_mode.value);
	ret += json_add_str(root_obj, "iccid",
			    modem_params->sim.iccid.value_string);
	ret += json_add_str(root_obj, "imei",
			    modem_params->device.imei.value_string);
	ret += json_add_number(root_obj, "interval", timeout_s);

	if (ret) {
		goto exit;
	}

	char *root_obj_string = cJSON_Print(root_obj);
	if (root_obj_string == NULL || ip_obj == NULL) {
		printk("JSON root object empty\n");
		goto exit;
	}

	ret = strlen(root_obj_string);
	memcpy(buffer, root_obj_string, ret);
	cJSON_FreeString(root_obj_string);

exit:
	cJSON_Delete(root_obj);

	return ret;
}

static int lte_connection_check(void)
{
	int err;

	enum lte_lc_nw_reg_status nw_reg_status;

	err = lte_lc_nw_reg_status_get(&nw_reg_status);
	if (err) {
		printk("lte_lc_nw_reg_status_get, error: %d\n", err);
		return err;
	}

	switch (nw_reg_status) {
	case LTE_LC_NW_REG_REGISTERED_HOME:
	case LTE_LC_NW_REG_REGISTERED_ROAMING:
		return 0;
	default:
		return -1;
	}
}

int send_data(int client_fd, int timeout_s)
{
	int err;
	char send_buf[SEND_BUF_SIZE] = { 0 };
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
		return -1;
	}

	send_len = create_send_buffer(&modem_params, send_buf, timeout_s);
	if (send_len < 0) {
		printk("Error creating json object: %d\n", send_len);
		return -1;
	}

	err = send(client_fd, send_buf, send_len + 1, 0);
	if (err < 0) {
		printk("Failed to send data, errno: %d\n", errno);

		return 0;
	}
	printk("Packet sent: %s\n", send_buf);
	return 1;
}

int poll_and_read(int client_fd, int timeout_s)
{
	int err;
	char recv_buf[RECV_BUF_SIZE] = { 0 };
	size_t ret_len;
	struct pollfd fds[] = { { .fd = client_fd, .events = POLLIN } };
	s64_t start_time = k_uptime_get();
	s64_t total_poll_time_ms = 0;

	while (1) {
		err = poll(fds, ARRAY_SIZE(fds), POLL_TIMEOUT);
		if (err < 0) {
			printk("poll, error: %d", err);

			return -1;
		} else if (err == 0) {
			total_poll_time_ms += k_uptime_delta(&start_time);
			if (total_poll_time_ms <
			    (timeout_s + TIMEOUT_TOL) * S_TO_MS_MULT) {
				printk("Elapsed time: %lld seconds\n",
				       total_poll_time_ms / S_TO_MS_MULT);
				continue;
			}

			printk("No response from server\n");
			return 0;
		} else if ((fds[0].revents & POLLIN) == POLLIN) {
			ret_len = recv(client_fd, recv_buf,
				       sizeof(recv_buf) - 1, 0);
			if (ret_len > 0 && ret_len < SEND_BUF_SIZE) {
				// Received "invalid format" message
				if (!strcmp(recv_buf, INV_FORMAT_MSG)) {
					printk("Response: %s\n", recv_buf);
					return -1;
				}
				recv_buf[ret_len] = 0;
				printk("Response: %s\n", recv_buf);
				return 1;
			}
		}
	}
}

static int setup_connection(int *client_fd)
{
	int err;
	struct addrinfo *res;
	struct addrinfo hints = {
		.ai_family = AF_INET,
	};

	err = lte_connection_check();
	if (err < 0) {
		printk("LTE link not maintained.\nAttempting to reconnect...\n");
		while (true) {
			err = lte_lc_init_and_connect();
			if (err) {
				printk("LTE link could not be established, error: %d\n",
				       err);
				continue;
			}
			break;
		}
	}

	if (current_protocol == TIMEOUT_UDP) {
		*client_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (client_fd <= 0) {
			printk("socket() failed, errno: %d\n", errno);
			return -1;
		}

		hints.ai_socktype = SOCK_DGRAM;
	} else if (current_protocol == TIMEOUT_TCP) {
		*client_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (client_fd <= 0) {
			printk("socket() failed, errno: %d\n", errno);
			return -1;
		}

		hints.ai_socktype = SOCK_STREAM;
	} else {
		return -1;
	}

	err = getaddrinfo(SERVER_HOSTNAME, NULL, &hints, &res);
	if (err) {
		printk("getaddrinfo() failed, err %d\n", errno);
		return -1;
	}

	if (current_protocol == TIMEOUT_UDP) {
		((struct sockaddr_in *)res->ai_addr)->sin_port =
			htons(UDP_PORT);
	} else if (current_protocol == TIMEOUT_TCP) {
		((struct sockaddr_in *)res->ai_addr)->sin_port =
			htons(TCP_PORT);
	}

	err = connect(*client_fd, res->ai_addr, sizeof(struct sockaddr_in));
	if (err) {
		printk("connect failed, errno: %d\n\r", errno);
		return -1;
	}

	printk("Connected to server with protocol %s\n",
	       get_protocol_name(current_protocol));

	return 0;
}

int get_timeout(bool timed_out)
{
	static int udp_counter = 0;
	static int curr_timeout = 0;
	static int upper = 0;
	static int lower = 0;
	static bool using_binary_search = false;

	// Switch to binary search
	if (!using_binary_search && timed_out) {
		using_binary_search = true;
	}

	if (using_binary_search) {
		// Adjust bounds
		if (timed_out) {
			upper = curr_timeout;
		} else {
			lower = curr_timeout;
		}

		// Found timeout rule
		if ((upper - lower) == 1) {
			if (current_protocol == TIMEOUT_UDP) {
				keep_alive_time.udp = lower;
				printk("Finished measuring for %s.\nKeep-alive time set at: %d seconds\n",
				       "UDP", upper);
			} else if (current_protocol == TIMEOUT_TCP) {
				keep_alive_time.tcp = lower;
				printk("Finished measuring for %s.\nKeep-alive time set at: %d seconds\n",
				       "TCP", upper);
			}

			curr_timeout = 0;
			using_binary_search = false;
		} else {
			curr_timeout = lower + ((upper - lower) / 2);
		}
	} else {
		lower = curr_timeout;
		if (current_protocol == TIMEOUT_UDP) {
			udp_counter++;
			curr_timeout = udp_counter * udp_counter;
		} else if (current_protocol == TIMEOUT_TCP) {
			if (curr_timeout == 0) {
				curr_timeout = TCP_INITIAL_TIMEOUT;
			}
			curr_timeout *= 1.5;
		}
	}
	return curr_timeout;
}

void main(void)
{
	int err;
	int client_fd;
	int timeout_s = get_timeout(false);

	printk("TCP client started\n");
	printk("Version: %s\n", CONFIG_NAT_TEST_VERSION);
	printk("Setting up LTE connection\n");

	err = lte_lc_init_and_connect();
	if (err) {
		printk("LTE link could not be established, error: %d\n", err);
		return;
	}

	printk("LTE connected\n");

	cJSON_Init();

	err = modem_info_init();
	if (err) {
		printk("Modem info could not be initialised: %d\n", err);
		return;
	}

	err = setup_connection(&client_fd);
	if (err < 0) {
		printk("Failed to configure connection: %d\n", errno);
		return;
	}

	while (current_protocol < NONE) {
		err = send_data(client_fd, timeout_s);
		if (err < 0) {
			goto reconnect;
		}

		printk("Waiting for response...\n");

		err = poll_and_read(client_fd, timeout_s);
		if (err < 0) {
			goto reconnect;
		} else if (err == 0) {
			timeout_s = get_timeout(true);
		} else if (err > 0) {
			timeout_s = get_timeout(false);
		}

		if (timeout_s == 0) {
			current_protocol++;
			if (strcmp(get_protocol_name(current_protocol),
				   "NONE")) {
				timeout_s = get_timeout(false);
				goto reconnect;
			} else {
				err = close(client_fd);
				if (err < 0) {
				}
				break;
			}
		}

		continue;

	reconnect:

		err = close(client_fd);
		if (err < 0) {
		}

		err = setup_connection(&client_fd);
		if (err < 0) {
			printk("Failed to reconfigure connection: %d\n", errno);
			return;
		}
	}

	printk("Finished NAT timeout measurements.\nUDP keep-alive time: %d seconds\nTCP keep-alive time: %d seconds\n",
	       keep_alive_time.udp, keep_alive_time.tcp);

	(void)close(client_fd);
}
