/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef NAT_TEST_H_
#define NAT_TEST_H_

#include <shell/shell.h>

#define SERVER_HOSTNAME "nat-test.thingy.rocks"
#define BUF_SIZE 512
#define THREAD_PRIORITY 5
#define S_TO_MS_MULT 1000

enum test_type { TEST_UDP = 0, TEST_TCP = 1, TEST_UDP_AND_TCP = 2 };

enum test_state { UNINITIALIZED, IDLE, RUNNING, ABORT };

extern volatile int udp_initial_timeout;
extern volatile int tcp_initial_timeout;
extern volatile float udp_timeout_multiplier;
extern volatile float tcp_timeout_multiplier;

/**
 * @brief Function to get current test state
 */
int get_test_state(void);

/**
 * @brief Function to get network status
 */
int get_network_status(void);

/**
 * @brief Function to stop running test
 */
int nat_test_stop(void);

/**
 * @brief Function to start test
 *
 * @param type Test type
 */
int nat_test_start(enum test_type type);

/**
 * @brief Function for initializing the NAT-test client.
 */
void nat_test_init(void);

#endif /* NAT_TEST_H_ */
