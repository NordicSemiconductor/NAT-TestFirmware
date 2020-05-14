/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include "nat_test.h"

#include <cJSON.h>
#include <cJSON_os.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#include <net/socket.h>
#include <power/reboot.h>
#include <shell/shell.h>
#include <shell/shell_uart.h>
#include <stdarg.h>
#include <stdio.h>
#include <zephyr.h>

#define UDP_PORT 3050
#define TCP_PORT 3051
#define THREAD_PRIORITY 5
#define THREAD_STACK_SIZE 8192
#define S_TO_MS_MULT 1000
#define POLL_TIMEOUT_S 3
#define WAIT_LOG_THRESHOLD_MS (60 * S_TO_MS_MULT)
#define TIMEOUT_TOL_S 10
#define MAX_RECONNECT_ATTEMPTS 5
#define CONNECTION_TIMEOUT_M CONFIG_LTE_NETWORK_TIMEOUT
#define DEFAULT_UDP_INITIAL_TIMEOUT 1
#define DEFAULT_TCP_INITIAL_TIMEOUT 300
#define DEFAULT_UDP_TIMEOUT_MULTIPLIER 2
#define DEFAULT_TCP_TIMEOUT_MULTIPLIER 1.5
#define DEFAULT_CONNECTION_MODE LTE_LC_SYSTEM_MODE_LTEM
#define DEFAULT_CONNECTION_STATE LTE_LC_NW_REG_NOT_REGISTERED
#define BUF_SIZE 512
#define IP_STRINGS_COUNT 10

enum test_state {
    UNINITIALIZED,
    IDLE,
    RUNNING,
    ABORT
};

struct connection {
    atomic_t mode;  // lte_lc_system_mode
    atomic_t state; // lte_lc_nw_reg_status
} connection;

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
    const struct shell *shell;
    struct test_thread_timeout timeout_data;
};

struct test_thread {
    struct k_thread thread;
    k_tid_t tid;
    k_thread_stack_t *stack_area;
    struct test_thread_data thread_data;
};

K_SEM_DEFINE(lte_connected, 0, 1);

K_THREAD_STACK_DEFINE(thread_stack_area, THREAD_STACK_SIZE);

static struct test_thread test_thread;

volatile int udp_initial_timeout;
volatile int tcp_initial_timeout;
volatile float udp_timeout_multiplier;
volatile float tcp_timeout_multiplier;

int get_initial_timeout(enum test_type type)
{
    if (type == TEST_UDP) {
        return udp_initial_timeout;
    } else if (type == TEST_TCP) {
        return tcp_initial_timeout;
    }

    return 0;
}

void set_initial_timeout(enum test_type type, int value)
{
    if (type == TEST_UDP) {
        udp_initial_timeout = value;
    } else if (type == TEST_TCP) {
        tcp_initial_timeout = value;
    }
}

float get_timeout_multiplier(enum test_type type)
{
    if (type == TEST_UDP) {
        return udp_timeout_multiplier;
    } else if (type == TEST_TCP) {
        return tcp_timeout_multiplier;
    }

    return 0;
}

void set_timeout_multiplier(enum test_type type, float value)
{
    if (type == TEST_UDP) {
        udp_timeout_multiplier = value;
    } else if (type == TEST_TCP) {
        tcp_timeout_multiplier = value;
    }
}

int get_network_mode()
{
    return atomic_get(&connection.state);
}

int set_network_mode(int mode)
{
    if (mode != LTE_LC_SYSTEM_MODE_LTEM && mode != LTE_LC_SYSTEM_MODE_NBIOT) {
        return -INVALID_MODE;
    } else if (atomic_get(&test_thread.thread_data.state) != IDLE) {
        return -TEST_RUNNING;
    } else if (atomic_get(&connection.mode) == mode) {
        return 0;
    }

    atomic_set(&connection.mode, mode);

    lte_lc_offline();
    lte_lc_system_mode_set(mode);
    lte_lc_normal();

    while (atomic_get(&connection.state) == LTE_LC_NW_REG_NOT_REGISTERED) {
        k_sleep(K_SECONDS(POLL_TIMEOUT_S));
    }

    return 0;
}

int get_network_state()
{
    return atomic_get(&connection.mode);
}

static void shell_check_and_print(const struct shell *shell, const char *fmt, ...)
{
    va_list args;
    char msg[BUF_SIZE];

    va_start(args, fmt);
    vsnprintf(msg, BUF_SIZE, fmt, args);
    va_end(args);

    if (shell == NULL) {
        printk("%s", msg);
    } else {
        shell_print(shell, "%s", msg);
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

static int create_send_buffer(const struct shell *shell, struct modem_param_info *modem_params, char *buffer, int timeout_s)
{
    int ret = 0;
    cJSON *root_obj = cJSON_CreateObject();
    cJSON *ip_obj;
    const char *delim = " ";
    const char *ip_strings[IP_STRINGS_COUNT];
    int ip_count = 0;

    if (root_obj == NULL) {
        shell_check_and_print(shell, "Failed to create json root object\n");
        return -ENOMEM;
    }

    char *token = strtok(modem_params->network.ip_address.value_string, delim);
    while (token != NULL) {
        if (ip_count >= ARRAY_SIZE(ip_strings)) {
            shell_check_and_print(shell, "More than %d adresses found. Remainder will not be added to json\n", IP_STRINGS_COUNT);
            break;
        }
        ip_strings[ip_count] = token;
        token = strtok(NULL, delim);
        ip_count++;
    }

    ip_obj = cJSON_CreateStringArray(ip_strings, ip_count);
    if (ip_obj == NULL) {
        shell_check_and_print(shell, "Failed to create json ip object\n");
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
        shell_check_and_print(shell, "Failed to add json value\n");
        ret = -ENOMEM;
        goto exit;
    }

    char *root_obj_string = cJSON_Print(root_obj);
    if (root_obj_string == NULL) {
        shell_check_and_print(shell, "Failed to print json object\n");
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

static int send_data(int client_fd, int timeout_s, const struct shell *shell)
{
    int err;
    char send_buf[BUF_SIZE] = {0};
    int send_len;
    struct modem_param_info modem_params = {0};

    err = modem_info_params_init(&modem_params);
    if (err) {
        shell_check_and_print(shell, "Modem info params could not be initialised: %d\n", err);
        return -1;
    }

    err = modem_info_params_get(&modem_params);
    if (err < 0) {
        shell_check_and_print(shell, "Unable to obtain modem parameters: %d\n", err);
        return -1;
    }

    send_len = create_send_buffer(shell, &modem_params, send_buf, timeout_s);
    if (send_len < 0) {
        shell_check_and_print(shell, "Error creating json object\n");
        return -1;
    }

    /* send len + 1 for null terminated packet */
    err = send(client_fd, send_buf, send_len + 1, 0);
    if (err < 0) {
        shell_check_and_print(shell, "Failed to send data, errno: %d\n", errno);

        return -ENOTCONN;
    }

    shell_check_and_print(shell, "Packet sent: %s\n", send_buf);
    return 0;
}

static int poll_and_read(int client_fd, int timeout_s, const struct shell *shell, atomic_t *state)
{
    int err;
    char recv_buf[BUF_SIZE] = {0};
    size_t ret_len;
    struct pollfd fds[] = {{.fd = client_fd, .events = POLLIN}};
    s64_t start_time_ms = k_uptime_get();
    s64_t per_log_poll_time_ms = 0;
    s64_t total_poll_time_ms = 0;

    while (1) {
        err = poll(fds, ARRAY_SIZE(fds), POLL_TIMEOUT_S * S_TO_MS_MULT);
        if (err < 0) {
            shell_check_and_print(shell, "poll, error: %d", err);

            return -1;
        } else if (err == 0) {
            if (atomic_get(state) == ABORT) {
                return 0;
            }

            int delta = k_uptime_delta(&start_time_ms);
            per_log_poll_time_ms += delta;
            total_poll_time_ms += delta;
            if (total_poll_time_ms <= (timeout_s + TIMEOUT_TOL_S) * S_TO_MS_MULT) {
                if (per_log_poll_time_ms >= WAIT_LOG_THRESHOLD_MS) {
                    shell_check_and_print(shell, "Elapsed time: %d seconds\n", total_poll_time_ms / S_TO_MS_MULT);
                    per_log_poll_time_ms = 0;
                }

                continue;
            }

            shell_check_and_print(shell, "No response from server\n");
            return 0;
        } else if ((fds[0].revents & POLLIN) == POLLIN) {
            ret_len = recv(client_fd, recv_buf, sizeof(recv_buf) - 1, 0);
            if (ret_len > 0 && ret_len < BUF_SIZE) {
                recv_buf[ret_len] = 0;

                if (strstr(recv_buf, "error") != NULL || strstr(recv_buf, "Error") != NULL) {
                    shell_check_and_print(shell, "Response: %s\n", recv_buf);
                    return -1;
                }

                shell_check_and_print(shell, "Response: %s\n", recv_buf);
                return 1;
            }
        }
    }
}

static void lte_handler(const struct lte_lc_evt *const evt)
{
    static bool init_connect = true;
    switch (evt->type) {
    case LTE_LC_EVT_NW_REG_STATUS:
        if (init_connect) {
            if ((evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME) || (evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_ROAMING)) {
                k_sem_give(&lte_connected);
                init_connect = false;
            }
        }
        atomic_set(&connection.state, evt->nw_reg_status);
        break;
    default:
        break;
    }
}

static int lte_connection_check(const struct shell *shell, atomic_t *state)
{
    enum lte_lc_nw_reg_status nw_reg_status = atomic_get(&connection.state);
    s64_t start_time_ms = k_uptime_get();
    s64_t reconnect_attempt_time_ms = 0;
    int log_counter = 0;
    int i = 0;

    if ((nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) && (nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
        shell_check_and_print(shell, "LTE link not maintained.\nAttempting to reconnect with %d min timeout (%d/%d)...\n", CONNECTION_TIMEOUT_M, i + 1, MAX_RECONNECT_ATTEMPTS);
    }

    while (i < MAX_RECONNECT_ATTEMPTS) {
        if (atomic_get(state) == ABORT) {
            return -1;
        }
        nw_reg_status = atomic_get(&connection.state);

        if ((nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) && (nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
            if ((nw_reg_status == LTE_LC_NW_REG_NOT_REGISTERED) || (nw_reg_status == LTE_LC_NW_REG_REGISTRATION_DENIED)) {
                i++;
                lte_lc_offline();
                lte_lc_system_mode_set(atomic_get(&connection.mode));
                lte_lc_normal();
                shell_check_and_print(shell, "LTE link not maintained.\nAttempting to reconnect with %d min timeout (%d/%d)...\n", CONNECTION_TIMEOUT_M, i + 1, MAX_RECONNECT_ATTEMPTS);
            }
        } else {
            return 0;
        }
        k_sleep(K_SECONDS(POLL_TIMEOUT_S));
        reconnect_attempt_time_ms += k_uptime_delta(&start_time_ms);

        if (reconnect_attempt_time_ms >= WAIT_LOG_THRESHOLD_MS) {
            log_counter++;
            shell_check_and_print(shell, "Elapsed time: %d seconds\n", (reconnect_attempt_time_ms / S_TO_MS_MULT) * log_counter);
            reconnect_attempt_time_ms = 0;
        }
    }
    shell_check_and_print(shell, "LTE link could not be established.\n");
    return -1;
}

static int setup_connection(int *client_fd, enum test_type type, const struct shell *shell, atomic_t *state)
{
    int err;
    struct addrinfo *res;
    struct addrinfo hints = {
        .ai_family = AF_INET,
    };

    err = lte_connection_check(shell, state);
    if (err < 0) {
#if defined(CONFIG_LTE_RESET_WHEN_UNABLE_TO_RECONNECT)
        shell_check_and_print(shell, "LTE link could not be established.\nResetting...\n");
        sys_reboot(SYS_REBOOT_WARM);
#endif
        return -1;
    }

    if (type == TEST_UDP) {
        *client_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (client_fd <= 0) {
            shell_check_and_print(shell, "socket() failed, errno: %d\n", errno);
            return -1;
        }

        hints.ai_socktype = SOCK_DGRAM;
    } else if (type == TEST_TCP) {
        *client_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (client_fd <= 0) {
            shell_check_and_print(shell, "socket() failed, errno: %d\n", errno);
            return -2;
        }

        hints.ai_socktype = SOCK_STREAM;
    } else {
        return -1;
    }

    err = getaddrinfo(SERVER_HOSTNAME, NULL, &hints, &res);
    if (err) {
        shell_check_and_print(shell, "getaddrinfo() failed, err %d\n", errno);
        return -1;
    }

    if (type == TEST_UDP) {
        ((struct sockaddr_in *)res->ai_addr)->sin_port = htons(UDP_PORT);
    } else if (type == TEST_TCP) {
        ((struct sockaddr_in *)res->ai_addr)->sin_port = htons(TCP_PORT);
    }

    err = connect(*client_fd, res->ai_addr, sizeof(struct sockaddr_in));
    if (err) {
        shell_check_and_print(shell, "connect failed, errno: %d\n\r", errno);
        return -1;
    }

    shell_check_and_print(shell, "Connected to server\n");

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

static void init_values(struct test_thread_timeout *timeout_data, enum test_type type)
{
    timeout_data->lower = 0;
    timeout_data->upper = 0;

    switch (type) {
    case TEST_UDP:
        timeout_data->timeout = udp_initial_timeout;
        timeout_data->multiplier = udp_timeout_multiplier;
        break;
    case TEST_TCP:
        timeout_data->timeout = tcp_initial_timeout;
        timeout_data->multiplier = tcp_timeout_multiplier;
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
static void nat_test_run_single(enum test_type type, const struct shell *shell, struct test_thread_timeout *timeout_data, atomic_t *state)
{
    int err;
    int client_fd;
    bool finished = false;
    bool using_binary_search = false;

    init_values(timeout_data, type);

    err = setup_connection(&client_fd, type, shell, state);
    if (err < 0) {
        return;
    }

    while (!finished) {
        if (atomic_get(state) == ABORT) {
            goto abort;
        }

        err = send_data(client_fd, timeout_data->timeout, shell);
        if (err < 0) {
            if (err == ENOTCONN) {
                goto reconnect;
            }
            goto abort;
        }

        err = poll_and_read(client_fd, timeout_data->timeout, shell, state);
        if (err < 0) {
            goto reconnect;
        } else if (err == 0) {
            using_binary_search = true;
            finished = get_timeout_binary_search(timeout_data, true);
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

        err = setup_connection(&client_fd, type, shell, state);
        if (err < 0) {
            return;
        }
    }

    shell_check_and_print(shell, "Finished NAT timeout measurements\nMax keep-alive time: %d seconds\n", timeout_data->timeout);
    (void)close(client_fd);
    return;

abort:
    (void)close(client_fd);
}

static void nat_test_run_both(struct test_thread_data *thread_data)
{
    nat_test_run_single(TEST_UDP, thread_data->shell, &thread_data->timeout_data, &thread_data->state);

    if (atomic_get(&thread_data->state) != ABORT) {
        nat_test_run_single(TEST_TCP, thread_data->shell, &thread_data->timeout_data, &thread_data->state);
    }
}

int nat_test_start(enum test_type type, const struct shell *shell)
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
    test_thread.thread_data.shell = shell;
    k_sem_give(&test_thread.thread_data.sem);

    return 0;
}

int nat_test_stop(void)
{
    switch (atomic_get(&test_thread.thread_data.state)) {
    case RUNNING:
        atomic_set(&test_thread.thread_data.state, ABORT);
    case ABORT:
        /* Make sure thread has enough time to detect abort request */
        k_sleep(K_SECONDS(POLL_TIMEOUT_S * 2));
        if (atomic_get(&test_thread.thread_data.state) != IDLE) {
            return -1;
        }
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

        shell_check_and_print(thread_data->shell, "Test started\n");
        switch (atomic_get(&thread_data->type)) {
        case TEST_UDP:
        case TEST_TCP:
            nat_test_run_single(thread_data->type, thread_data->shell, &thread_data->timeout_data, &thread_data->state);
            break;
        case TEST_UDP_AND_TCP:
            nat_test_run_both(thread_data);
            break;
        default:
            shell_check_and_print(thread_data->shell, "Thread with invalid type started");
            return;
        }
        atomic_set(&thread_data->state, IDLE);
    }
}

static void prepare_and_start_thread(struct test_thread *thread)
{
    k_sem_init(&thread->thread_data.sem, 0, 1);
    thread->stack_area = thread_stack_area;

    thread->tid = k_thread_create(
        &thread->thread, thread->stack_area, THREAD_STACK_SIZE,
        nat_test_thread_entry_point, (void *)&thread->thread_data,
        NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);
}

int nat_test_init()
{
    int err;

    printk("NAT-test client started\n");
    printk("Version: %s\n", CONFIG_NAT_TEST_VERSION);

    udp_initial_timeout = DEFAULT_UDP_INITIAL_TIMEOUT;
    tcp_initial_timeout = DEFAULT_TCP_INITIAL_TIMEOUT;
    udp_timeout_multiplier = DEFAULT_UDP_TIMEOUT_MULTIPLIER;
    tcp_timeout_multiplier = DEFAULT_TCP_TIMEOUT_MULTIPLIER;

    atomic_set(&connection.mode, DEFAULT_CONNECTION_MODE);
    atomic_set(&connection.state, DEFAULT_CONNECTION_STATE);

    cJSON_Init();

    err = modem_info_init();
    if (err) {
        printk("Modem info could not be initialised: %d\n", err);
        return -1;
    }

    prepare_and_start_thread(&test_thread);

    printk("Setting up LTE connection\n");

    err = lte_lc_init_and_connect_async(lte_handler);
    if (err) {
        printk("LTE link could not be established, error: %d\n", err);
        return -1;
    }

    if (k_sem_take(&lte_connected, K_MINUTES(CONNECTION_TIMEOUT_M)) != 0) {
        printk("LTE link could not be established\n");
        return -1;
    }

    printk("LTE connected\n");

    return 0;
}
