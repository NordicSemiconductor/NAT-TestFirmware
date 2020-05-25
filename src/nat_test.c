/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <cJSON.h>
#include <cJSON_os.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#include <net/socket.h>
#include <stdarg.h>
#include <stdio.h>

#include "nat_test.h"

#define UDP_PORT 3050
#define TCP_PORT 3051
#define THREAD_STACK_SIZE 8192
#define WAIT_TIME_S 3
#define WAIT_LOG_THRESHOLD_MS (60 * S_TO_MS_MULT)
#define TIMEOUT_TOL_S 10
#define DEFAULT_UDP_INITIAL_TIMEOUT 1
#define DEFAULT_TCP_INITIAL_TIMEOUT 300
#define DEFAULT_UDP_TIMEOUT_MULTIPLIER 2
#define DEFAULT_TCP_TIMEOUT_MULTIPLIER 1.5
#define IP_STRINGS_COUNT 10

struct test_thread_timeout {
    int timeout;
    double multiplier;
    int lower;
    int upper;
};

struct test_thread_data {
    atomic_t type;
    atomic_t state;
    struct k_sem sem;
    struct test_thread_timeout timeout_data;
};

struct test_thread {
    struct k_thread thread;
    k_tid_t tid;
    k_thread_stack_t *stack_area;
    struct test_thread_data thread_data;
};

K_THREAD_STACK_DEFINE(nat_test_thread_stack_area, THREAD_STACK_SIZE);

static struct test_thread test_thread;

volatile int udp_initial_timeout;
volatile int tcp_initial_timeout;
volatile float udp_timeout_multiplier;
volatile float tcp_timeout_multiplier;

int get_test_state(void)
{
    return atomic_get(&test_thread.thread_data.state);
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

static int create_send_buffer(struct modem_param_info *modem_params, char *buffer, int timeout_s)
{
    int ret = 0;
    cJSON *root_obj = cJSON_CreateObject();
    cJSON *ip_obj;
    const char *delim = " ";
    const char *ip_strings[IP_STRINGS_COUNT];
    int ip_count = 0;

    if (root_obj == NULL) {
        printk("Failed to create json root object\n");
        return -ENOMEM;
    }

    char *token = strtok(modem_params->network.ip_address.value_string, delim);
    while (token != NULL) {
        if (ip_count >= ARRAY_SIZE(ip_strings)) {
            printk("More than %d adresses found. Remainder will not be added to json\n", IP_STRINGS_COUNT);
            break;
        }
        ip_strings[ip_count] = token;
        token = strtok(NULL, delim);
        ip_count++;
    }

    ip_obj = cJSON_CreateStringArray(ip_strings, ip_count);
    if (ip_obj == NULL) {
        printk("Failed to create json ip object\n");
        return -ENOMEM;
    }

    ret += json_add_obj(root_obj, "ip", ip_obj);
    ret += json_add_str(root_obj, "op", modem_params->network.current_operator.value_string);
    ret += json_add_number(root_obj, "cell_id", modem_params->network.cellid_dec);
    ret += json_add_number(root_obj, "ue_mode", modem_params->network.ue_mode.value);
    ret += json_add_number(root_obj, "lte_mode", modem_params->network.lte_mode.value);
    ret += json_add_number(root_obj, "nbiot_mode", modem_params->network.nbiot_mode.value);
    ret += json_add_str(root_obj, "iccid", modem_params->sim.iccid.value_string);
    ret += json_add_str(root_obj, "imei", modem_params->device.imei.value_string);
    ret += json_add_number(root_obj, "interval", timeout_s);

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

static int send_data(int client_fd, int timeout_s)
{
    int err;
    char send_buf[BUF_SIZE] = {0};
    int send_len;
    struct modem_param_info modem_params = {0};

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

    send_len = create_send_buffer(&modem_params, send_buf, timeout_s);
    if (send_len < 0) {
        return -1;
    }

    /* send len + 1 for null terminated packet */
    err = send(client_fd, send_buf, send_len + 1, 0);
    if (err < 0) {
        printk("Failed to send data, errno: %d\n", errno);

        return -ENOTCONN;
    }

    printk("Packet sent: %s\n", send_buf);
    return 0;
}

static int poll_and_read(int client_fd, int timeout_s, atomic_t *state)
{
    int err;
    char recv_buf[BUF_SIZE] = {0};
    size_t ret_len;
    struct pollfd fds[] = {{.fd = client_fd, .events = POLLIN}};
    s64_t start_time_ms = k_uptime_get();
    s64_t per_log_poll_time_ms = 0;
    s64_t total_poll_time_ms = 0;

    while (1) {
        if (atomic_get(state) == ABORT) {
            return -1;
        }

        err = poll(fds, ARRAY_SIZE(fds), WAIT_TIME_S * S_TO_MS_MULT);
        if (err < 0) {
            printk("poll, error: %d", err);

            return -ENOTCONN;
        } else if (err == 0) {
            int delta = k_uptime_delta(&start_time_ms);
            per_log_poll_time_ms += delta;
            total_poll_time_ms += delta;
            if (total_poll_time_ms <= (timeout_s + TIMEOUT_TOL_S) * S_TO_MS_MULT) {
                if (per_log_poll_time_ms >= WAIT_LOG_THRESHOLD_MS) {
                    printk("Elapsed time: %d of %d seconds (%d seconds tolerance)\n", (int)(total_poll_time_ms / S_TO_MS_MULT), timeout_s + TIMEOUT_TOL_S, TIMEOUT_TOL_S);
                    per_log_poll_time_ms = 0;
                }

                continue;
            }

            printk("No response from server\n");
            return 0;
        } else if ((fds[0].revents & POLLIN) == POLLIN) {
            ret_len = recv(client_fd, recv_buf, sizeof(recv_buf) - 1, 0);
            if (ret_len > 0 && ret_len < BUF_SIZE) {
                recv_buf[ret_len] = 0;

                if (strstr(recv_buf, "error") != NULL || strstr(recv_buf, "Error") != NULL) {
                    printk("Response: %s\n", recv_buf);
                    return -1;
                }

                printk("Response: %s\n", recv_buf);
                return 1;
            }
        }
    }
}

static int setup_connection(int *client_fd, enum test_type type, int port, atomic_t *state)
{
    int err;
    struct addrinfo *res;
    struct addrinfo hints = {
        .ai_family = AF_INET,
    };
    enum lte_lc_nw_reg_status network_status = get_network_status();
    s64_t start_time = k_uptime_get();
    s64_t wait_time = 0;

    /* Trigger a connect attempt only when device is able exhaust its reconnect attempts without restarting */
    if (!IS_ENABLED(CONFIG_NAT_TEST_RESET_WHEN_UNABLE_TO_CONNECT)) {
        if ((network_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
            (network_status != LTE_LC_NW_REG_REGISTERED_ROAMING) &&
            (network_status != LTE_LC_NW_REG_SEARCHING)) {
            lte_lc_offline();
        }
    }

    while ((network_status != LTE_LC_NW_REG_REGISTERED_HOME) && (network_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
        k_sleep(WAIT_TIME_S);
        if (atomic_get(state) == ABORT) {
            return -1;
        }

        wait_time += k_uptime_delta(&start_time);
        /* Give enough time for an entire lte connect attempt */
        if (wait_time >= CONFIG_LTE_NETWORK_TIMEOUT) {
            printk("Unable to connect. No LTE link was established in time\nTry again later.\n");
            return -1;
        }
        network_status = get_network_status();
    }

    if (type == TEST_UDP) {
        *client_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (client_fd <= 0) {
            printk("socket() failed, errno: %d\n", errno);
            return -1;
        }

        hints.ai_socktype = SOCK_DGRAM;
    } else if (type == TEST_TCP) {
        *client_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (client_fd <= 0) {
            printk("socket() failed, errno: %d\n", errno);
            return -2;
        }

        hints.ai_socktype = SOCK_STREAM;
    } else {
        return -1;
    }

    k_sem_take(&getaddrinfo_sem, K_FOREVER);
    err = getaddrinfo(SERVER_HOSTNAME, NULL, &hints, &res);
    if (err) {
        k_sem_give(&getaddrinfo_sem);
        printk("getaddrinfo() failed, err %d\n", errno);
        return -1;
    }
    k_sem_give(&getaddrinfo_sem);

    ((struct sockaddr_in *)res->ai_addr)->sin_port = htons(port);

    err = connect(*client_fd, res->ai_addr, sizeof(struct sockaddr_in));
    if (err) {
        printk("connect failed, errno: %d\n\r", errno);
        return -1;
    }

    printk("Connected to server\n");

    return 0;
}

static bool get_timeout_binary_search(struct test_thread_timeout *timeout_data, bool timed_out)
{
    if (timed_out) {
        timeout_data->upper = timeout_data->timeout;
    } else {
        timeout_data->lower = timeout_data->timeout;
    }

    if ((timeout_data->upper - timeout_data->lower) == 1) {
        timeout_data->timeout = timeout_data->lower;

        return true;
    } else {
        timeout_data->timeout = timeout_data->lower + ((timeout_data->upper - timeout_data->lower) / 2);
        return false;
    }
}

static void init_values(struct test_thread_timeout *timeout_data, enum test_type type, int *port)
{
    timeout_data->lower = 0;
    timeout_data->upper = 0;

    switch (type) {
    case TEST_UDP:
        timeout_data->timeout = udp_initial_timeout;
        timeout_data->multiplier = udp_timeout_multiplier;
        *port = UDP_PORT;
        break;
    case TEST_TCP:
        timeout_data->timeout = tcp_initial_timeout;
        timeout_data->multiplier = tcp_timeout_multiplier;
        *port = TCP_PORT;
        break;
    default:
        /* Unused */
        break;
    }
}

/**
 * @brief Run single test case (UDP or TCP). Send data with increasing timeout interval until no answer is received (connection closed due to timeout). 
 *        Then use binary search to determine accurate timeout.
 */
static void nat_test_run_single(enum test_type type, struct test_thread_timeout *timeout_data, atomic_t *state)
{
    int err;
    int client_fd;
    bool finished = false;
    bool using_binary_search = false;
    int port = 0;

    init_values(timeout_data, type, &port);

    err = setup_connection(&client_fd, type, port, state);
    if (err < 0) {
        return;
    }

    while (!finished) {
        if (atomic_get(state) == ABORT) {
            goto abort;
        }

        err = send_data(client_fd, timeout_data->timeout);
        if (err < 0) {
            if (err == -ENOTCONN) {
                goto reconnect;
            }
            goto abort;
        }

        err = poll_and_read(client_fd, timeout_data->timeout, state);
        if (err < 0) {
            if (err == -ENOTCONN) {
                goto reconnect;
            }
            goto abort;
        } else if (err == 0) {
            using_binary_search = true;
            finished = get_timeout_binary_search(timeout_data, true);
            goto reconnect;
        } else if (err > 0 && using_binary_search) {
            finished = get_timeout_binary_search(timeout_data, false);
        }

        if (!using_binary_search) {
            timeout_data->lower = timeout_data->timeout;
            timeout_data->timeout *= timeout_data->multiplier;
        }

        continue;

    reconnect:
        close(client_fd);

        err = setup_connection(&client_fd, type, port, state);
        if (err < 0) {
            return;
        }
    }

    printk("Finished NAT timeout measurements\nMax keep-alive time: %d seconds\n", timeout_data->timeout);

abort:
    (void)close(client_fd);
}

static void nat_test_run_both(struct test_thread_data *thread_data)
{
    nat_test_run_single(TEST_UDP, &thread_data->timeout_data, &thread_data->state);

    if (atomic_get(&thread_data->state) != ABORT) {
        nat_test_run_single(TEST_TCP, &thread_data->timeout_data, &thread_data->state);
    }
}

int nat_test_start(enum test_type type)
{
    switch (atomic_get(&test_thread.thread_data.state)) {
    case RUNNING:
    case ABORT:
        return -1;
    case IDLE:
    default:
        break;
    }

    atomic_set(&test_thread.thread_data.type, type);
    k_sem_give(&test_thread.thread_data.sem);

    return 0;
}

int nat_test_stop(void)
{
    switch (atomic_get(&test_thread.thread_data.state)) {
    case RUNNING:
        atomic_set(&test_thread.thread_data.state, ABORT);
        break;
    case ABORT:
        break;
    case IDLE:
    default:
        break;
    }

    return 0;
}

static void nat_test_thread_entry_point(void *param, void *unused, void *unused2)
{
    struct test_thread_data *thread_data = (struct test_thread_data *)param;

    atomic_set(&thread_data->state, IDLE);

    while (true) {
        k_sem_take(&thread_data->sem, K_FOREVER);

        atomic_set(&thread_data->state, RUNNING);

        printk("Test started\n");
        switch (atomic_get(&thread_data->type)) {
        case TEST_UDP:
        case TEST_TCP:
            nat_test_run_single(thread_data->type, &thread_data->timeout_data, &thread_data->state);
            break;
        case TEST_UDP_AND_TCP:
            nat_test_run_both(thread_data);
            break;
        default:
            printk("Thread with invalid type started");
            return;
        }
        atomic_set(&thread_data->state, IDLE);
        printk("Test idle\n");
    }
}

static void prepare_and_start_thread(struct test_thread *thread)
{
    k_sem_init(&thread->thread_data.sem, 0, 1);
    thread->stack_area = nat_test_thread_stack_area;

    thread->tid = k_thread_create(&thread->thread, thread->stack_area, THREAD_STACK_SIZE,
                                  nat_test_thread_entry_point, (void *)&thread->thread_data,
                                  NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);
}

void nat_test_init(void)
{
    udp_initial_timeout = DEFAULT_UDP_INITIAL_TIMEOUT;
    tcp_initial_timeout = DEFAULT_TCP_INITIAL_TIMEOUT;
    udp_timeout_multiplier = DEFAULT_UDP_TIMEOUT_MULTIPLIER;
    tcp_timeout_multiplier = DEFAULT_TCP_TIMEOUT_MULTIPLIER;

    prepare_and_start_thread(&test_thread);
}
