#
# Copyright (c) 2020 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

cmake_minimum_required(VERSION 3.8.2)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(NAT_testFirmware)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/nat_cmd.c)
target_sources(app PRIVATE src/nat_test.c)
