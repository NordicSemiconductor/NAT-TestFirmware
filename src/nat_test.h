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

enum test_type
{
    TEST_UDP = 0,
    TEST_TCP = 1,
    TEST_UDP_AND_TCP = 2
};

/**
 * @brief Function to get initial timeout
 * 
 * @param type Test type
 */
int get_initial_timeout(const enum test_type type);

/**
 * @brief Function set initial timeout
 * 
 * @param type Test type
 * @param value New value timeout
 */
void set_initial_timeout(const enum test_type type, const int value);

/**
 * @brief Function to get timeout multiplier
 * 
 * @param type Test type
 */
float get_timeout_multiplier(const enum test_type type);

/**
 * @brief Function to set timeout multiplier
 * 
 * @param type Test type
 * @param value New timeout multiplier value
 */
void set_timeout_multiplier(const enum test_type type, const float value);

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
