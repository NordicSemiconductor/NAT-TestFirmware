/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <stdio.h>

#include "nat_test.h"

void main(void)
{
    int err;

    err = nat_test_init();
    if (err < 0) {
        return;
    }
    nat_test_start(TEST_UDP_AND_TCP, NULL);
}
