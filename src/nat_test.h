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

enum test_type {
    TEST_UDP = 0,
    TEST_TCP = 1,
    TEST_UDP_AND_TCP = 2
};

enum set_network_mode_error {
    SUCCESS = 0,
    INVALID_MODE = 1,
    TEST_RUNNING = 2
};

/**
 * @brief Function to get initial timeout
 * 
 * @param type Test type
 */
int get_initial_timeout(enum test_type type);

/**
 * @brief Function set initial timeout
 * 
 * @param type Test type
 * @param value New value timeout
 */
void set_initial_timeout(enum test_type type, int value);

/**
 * @brief Function to get timeout multiplier
 * 
 * @param type Test type
 */
float get_timeout_multiplier(enum test_type type);

/**
 * @brief Function to set timeout multiplier
 * 
 * @param type Test type
 * @param value New timeout multiplier value
 */
void set_timeout_multiplier(enum test_type type, float value);

/**
 * @brief Function to get network mode
 */
int get_network_mode();

/**
 * @brief Function to set network mode
 * 
 * @param mode New network mode. LTE_LC_SYSTEM_MODE_LTEM = 1 or LTE_LC_SYSTEM_MODE_NB_IOT = 2
 */
int set_network_mode(int mode);

/**
 * @brief Function to get network state
 */
int get_network_state();

/**
 * @brief Function to stop running test
 */
int nat_test_stop(void);

/**
 * @brief Function to start test
 * 
 * @param type Test type
 * @param shell Pointer to active shell
 */
int nat_test_start(enum test_type type, const struct shell *shell);

/**
 * @brief Function for initializing the NAT-test client.
 */
int nat_test_init(void);

#endif /* NAT_TEST_H_ */
