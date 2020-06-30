# NAT Test Firmware

![Build and Release](https://github.com/bifravst/firmware/workflows/Build%20and%20Release/badge.svg?branch=saga)
[![semantic-release](https://img.shields.io/badge/%20%20%F0%9F%93%A6%F0%9F%9A%80-semantic--release-e10079.svg)](https://github.com/semantic-release/semantic-release)
[![Commitizen friendly](https://img.shields.io/badge/commitizen-friendly-brightgreen.svg)](http://commitizen.github.io/cz-cli/)
[![Nordic ClangFormat](https://img.shields.io/static/v1?label=Nordic&message=ClangFormat&labelColor=00A9CE&color=337ab7)](https://github.com/nrfconnect/sdk-nrf/blob/master/.clang-format)
[![Zephyr compliance](https://img.shields.io/static/v1?label=Zephry&message=compliance&labelColor=4e109e&color=337ab7)](https://docs.zephyrproject.org/latest/contribute/index.html#coding-style)

This firmware determines the NAT timeouts in cellular networks by sending messages to the [NAT-TestServer](https://github.com/NordicSemiconductor/NAT-TestServer) and waiting for the reply.

This application is built with [sdk-nrf](https://github.com/nrfconnect/sdk-nrf).

## Getting started

Support this project by giving more insight into the NAT timeouts in cellular networks.
The following steps describe the testing process:

1. Find the latest firmware build in the [releases](https://github.com/NordicSemiconductor/NAT-TestFirmware/releases) and flash it onto your nRF9160 Development Kit.
1. Insert the SIM card of your choice and power on the development kit. The test starts automatically. **Do not change the location of the development kit during testing, and avoid switching mobile cells.**
1. Optionally, you can connect the development kit via USB and observe the test status in a terminal.
1. Wait until the test finishes (This is indicated by the 4 LEDs, blinking in a rotating pattern). If the TCP test continues to run for more than 24 hours, you can abort it. It is generally assumed that a test run duration exceeding 24 hours indicates a network providing sufficient power savings for majority of the use case scenarios.
1. Register an account on <https://cellprobe.thingy.rocks/> and login to see your test results (These results are updated every hour).
1. If your SIM does not show up after login, it could mean that the ICCID is unknown. In this case, open an issue in the [TestServer repository](https://github.com/NordicSemiconductor/NAT-TestServer/issues/new).
1. Optionally, repeat the steps from Step 2 for every SIM you would like to test.

If you have any questions, open an issue [in the TestServer repository](https://github.com/NordicSemiconductor/NAT-TestServer/issues/new).

## Automated releases

This project uses [Semantic Release](https://github.com/semantic-release/semantic-release) to automate releases. Every commit is run using [GitHub Actions](https://github.com/features/actions) and depending on the commit message an new GitHub [release](https://github.com/NordicSemiconductor/NAT-TestFirmware/releases) is created and pre-build hex-files for all supported boards are attached.

## Shell commands

The NAT-test client can be configured through UART shell (115200 baudrate) with the following commands:

- start
  - udp
  - tcp
  - udp_and_tcp
- stop_running_test
- config
  - test
    - udp
      - initial_timeout
        - get
        - set <value>
      - timeout_multiplier
        - get
        - set <value>
    - tcp
      - initial_timeout
        - get
        - set <value>
      - timeout_multiplier
        - get
        - set <value>
  - network
    - mode
      - get
      - set
    - state
      - get

Additionally one can send AT-cmds with `at <AT cmd>`

## LED status indication

- LED 1 blinking: Test in progress
- LED 1-4 blinking in a rotating pattern: Test is done
