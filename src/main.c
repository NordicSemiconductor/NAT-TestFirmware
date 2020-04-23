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

#define SERVER_HOSTNAME 			"nat-test.thingy.rocks"
#define UDP_PORT					3050
#define TCP_PORT 					3051
#define SEND_BUF_SIZE	 			512
#define RECV_BUF_SIZE	 			256
#define UDP_INITIAL_TIMEOUT			1
#define TCP_TIMEOUT_OFFSET			35
#define TIMEOUT_INCREMENT(COUNTER)	(((COUNTER)*(COUNTER))/4)
#define TIMEOUT_TOL					5
#define INV_FORMAT_MSG				"Error occured.\nConnection closed.\n"
#define S_TO_MS(S)					((S)*1000)

enum protocol{
	TIMEOUT_UDP,
	TIMEOUT_TCP,
	NONE
};

struct timeout_struct {
	int udp_timeout;
	int tcp_timeout;
} max_timeout;

static enum protocol current_protocol = TIMEOUT_UDP;
static int packet_counter = 1;

static int getTimeout() 
{
		
	if (current_protocol == TIMEOUT_UDP)
	{
		return UDP_INITIAL_TIMEOUT + TIMEOUT_INCREMENT(packet_counter);
	}
	else if (current_protocol == TIMEOUT_TCP)
	{
		return TIMEOUT_INCREMENT(packet_counter * TCP_TIMEOUT_OFFSET);
	}
	return -1;
}

static char* getProtocolName(enum protocol proto)
{
	switch(proto) 
	{
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

static int createSendBuffer(struct modem_param_info * modem_params, char * buffer)
{
	int ret = 0;
	cJSON *root_obj = cJSON_CreateObject();
	cJSON *ip_obj;
	const char *delim = " ";
	const char *ip_strings[10];
	int ip_count = 0;

	if (root_obj == NULL)
	{
		cJSON_Delete(root_obj);
		return -ENOMEM;
	}

	char *token = strtok(modem_params->network.ip_address.value_string, delim);
	while(token != NULL && ip_count < ARRAY_SIZE(ip_strings))
	{
		ip_strings[ip_count] = token;
		token = strtok(NULL, delim);
		ip_count++;
	}

	ip_obj = cJSON_CreateStringArray(ip_strings, ip_count);
	if (ip_obj == NULL)
	{
		cJSON_Delete(ip_obj);
		return -ENOMEM;
	}
	
	ret += json_add_obj(root_obj, "ip", ip_obj);
	ret += json_add_str(root_obj, "op", modem_params->network.current_operator.value_string);
	ret += json_add_number(root_obj, "cell_id", modem_params->network.cellid_dec);
	ret += json_add_number(root_obj, "ue_mode", modem_params->network.ue_mode.value);
	ret += json_add_str(root_obj, "iccid", modem_params->sim.iccid.value_string);
	ret += json_add_number(root_obj, "interval", getTimeout());

	if (ret) 
	{
		goto exit;
	}

	char *root_obj_string = cJSON_Print(root_obj);
	if (root_obj_string == NULL || ip_obj == NULL)
	{
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
	if (err)
	{
		printk("lte_lc_nw_reg_status_get, error: %d\n", err);
		return err;
	}

	switch (nw_reg_status)
	{
		case LTE_LC_NW_REG_REGISTERED_HOME:
		case LTE_LC_NW_REG_REGISTERED_ROAMING:
			return 0;
		default:
			return -1;
	}
}

int send_data(int client_fd)
{
	int err;
	char send_buf[SEND_BUF_SIZE] = {0};
	int send_len;
	struct modem_param_info modem_params = {0};

	err = modem_info_params_init(&modem_params);
	if (err) 
	{
		printk("Modem info params could not be initialised: %d\n", err);
		return -1;
	}

	err = modem_info_params_get(&modem_params);
	if (err < 0)
	{
		printk("Unable to obtain modem parameters: %d\n", err);
		return -1;
	}

	send_len = createSendBuffer(&modem_params, send_buf);
	if (send_len < 0)
	{
		printk("Error creating json object: %d\n", send_len);
		return -1;
	}

	err = send(client_fd, send_buf, send_len + 1, 0);
	if (err < 0) 
	{
		printk("Failed to send data, errno: %d\n", errno);

		return 0;
	}
	printk("Packet sent: %s\n", send_buf);	
	return 1;
}

int poll_and_read(int client_fd)
{
	int err;
	char recv_buf[RECV_BUF_SIZE] = {0};
	size_t ret_len;
	struct pollfd fds[] = { { .fd = client_fd, .events = POLLIN } };

	while(1)
	{
		err = poll(fds, ARRAY_SIZE(fds), S_TO_MS(getTimeout() + TIMEOUT_TOL));
		if (err < 0) {
			printk("poll, error: %d", err);

			return -1;
		}
		else if (err == 0) 
		{
			printk("No response from server\n");
			if (current_protocol == TIMEOUT_UDP)
			{
				max_timeout.udp_timeout = getTimeout();
				return 0;
			}
			else if (current_protocol == TIMEOUT_TCP)
			{
				max_timeout.tcp_timeout = getTimeout();
				return 0;
			}
			else
			{
				return -1;
			}
		}
		else if ((fds[0].revents & POLLIN) == POLLIN) 
		{
			ret_len = recv(client_fd, recv_buf, sizeof(recv_buf) - 1, 0);
			if (ret_len > 0 && ret_len < SEND_BUF_SIZE) 
			{
				// Received "invalid format" message
				if (!strcmp(recv_buf, INV_FORMAT_MSG))
				{
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

static int setup_connection(int * client_fd)
{
	int err;
	struct addrinfo *res;
	struct addrinfo hints = {
		.ai_family = AF_INET,
	};

	err = lte_connection_check();
	if (err < 0)
	{
		printk("LTE link not maintained.\nAttempting to reconnect...\n");
		while(true){
			err = lte_lc_init_and_connect();
			if (err) 
			{
				printk("LTE link could not be established, error: %d\n", err);
				continue;
			}
			break;
		}
	}

	if (current_protocol == TIMEOUT_UDP)
	{
		*client_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (client_fd <= 0) {
			printk("socket() failed, errno: %d\n", errno);
			return -1;
		}

		hints.ai_socktype = SOCK_DGRAM;
	}
	else if (current_protocol == TIMEOUT_TCP)
	{
		*client_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (client_fd <= 0) {
			printk("socket() failed, errno: %d\n", errno);
			return -1;
		}

		hints.ai_socktype = SOCK_STREAM;
	}
	else 
	{
		return -1;
	}

	err = getaddrinfo(SERVER_HOSTNAME, NULL, &hints, &res);
	if (err) {
		printk("getaddrinfo() failed, err %d\n", errno);
		return -1;
	}

	if (current_protocol == TIMEOUT_UDP)
	{
		((struct sockaddr_in *)res->ai_addr)->sin_port = htons(UDP_PORT);
	}
	else if (current_protocol == TIMEOUT_TCP)
	{
		((struct sockaddr_in *)res->ai_addr)->sin_port = htons(TCP_PORT);
	}


	err = connect(*client_fd, res->ai_addr, sizeof(struct sockaddr_in));
	if (err) 
	{
		printk("connect failed, errno: %d\n\r", errno);
		return -1;
	}

	printk("Connected to server with protocol %s\n", getProtocolName(current_protocol));

	return 0;
}

void main(void)
{
	int err;
	int client_fd;

	printk("TCP client started\n");

	printk("Setting up LTE connection\n");

	err = lte_lc_init_and_connect();
	if (err) 
	{
		printk("LTE link could not be established, error: %d\n", err);
		return;
	}

	printk("LTE connected\n");

	cJSON_Init();

	err = modem_info_init();
	if (err) 
	{
		printk("Modem info could not be initialised: %d\n", err);
		return;
	}

	err = setup_connection(&client_fd);
	if (err < 0)
	{
		printk("Failed to configure connection: %d\n", errno);
		return;
	}

	while(current_protocol < NONE)
	{
		err = send_data(client_fd);
		if (err < 0)
		{
			goto reconnect;
		}

		printk("Waiting for response...\n");

		err = poll_and_read(client_fd);
		if (err < 0)
		{
			goto reconnect;
		}
		else if (err == 0)
		{
			printk("Finished checking for protocol %s\n", getProtocolName(current_protocol));
			current_protocol += 1;
			if (strcmp(getProtocolName(current_protocol), "NONE"))
			{
				packet_counter = 1;
				goto reconnect;
			}
			else 
			{
				err = close(client_fd);
				if (err < 0) {}
				break;
			}
		}
		packet_counter++;

		continue;	

reconnect:

		err = close(client_fd);
		if (err < 0) {}

		err = setup_connection(&client_fd);
		if (err < 0)
		{
			printk("Failed to reconfigure connection: %d\n", errno);
			return;
		}
	}

	printk("Finished NAT timeout measurements.\nUDP timed out at: %d seconds\nTCP timed out at: %d seconds\n", max_timeout.udp_timeout, max_timeout.tcp_timeout);

	(void)close(client_fd);
}

