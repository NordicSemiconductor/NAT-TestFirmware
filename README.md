# NAT Test Firmware

![Build and Release](https://github.com/bifravst/firmware/workflows/Build%20and%20Release/badge.svg?branch=saga)
[![semantic-release](https://img.shields.io/badge/%20%20%F0%9F%93%A6%F0%9F%9A%80-semantic--release-e10079.svg)](https://github.com/semantic-release/semantic-release)
[![Commitizen friendly](https://img.shields.io/badge/commitizen-friendly-brightgreen.svg)](http://commitizen.github.io/cz-cli/)
[![Nordic ClangFormat](https://img.shields.io/static/v1?label=Nordic&message=ClangFormat&labelColor=00A9CE&color=337ab7)](https://github.com/nrfconnect/sdk-nrf/blob/master/.clang-format)
[![Zephyr compliance](https://img.shields.io/static/v1?label=Zephry&message=compliance&labelColor=4e109e&color=337ab7)](https://docs.zephyrproject.org/latest/contribute/index.html#coding-style)

This application is built with [sdk-nrf](https://github.com/nrfconnect/sdk-nrf).

Used to test NAT timeouts in different networks, sends messages to [NAT-TestServer](https://github.com/NordicSemiconductor/NAT-TestServer).

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
