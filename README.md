# NAT Test Firmware

![Build and Release](https://github.com/bifravst/firmware/workflows/Build%20and%20Release/badge.svg?branch=saga)
[![semantic-release](https://img.shields.io/badge/%20%20%F0%9F%93%A6%F0%9F%9A%80-semantic--release-e10079.svg)](https://github.com/semantic-release/semantic-release)
[![Commitizen friendly](https://img.shields.io/badge/commitizen-friendly-brightgreen.svg)](http://commitizen.github.io/cz-cli/)

This application is built with [fw-nrfconnect-nrf](https://github.com/NordicPlayground/fw-nrfconnect-nrf).

Used to test NAT timeouts in different networks, sends messages to [NAT-TestServer](https://github.com/NordicSemiconductor/NAT-TestServer).

## Automated releases

This project uses [Semantic Release](https://github.com/semantic-release/semantic-release) to automate releases. Every commit is run using [GitHub Actions](https://github.com/features/actions) and depending on the commit message an new GitHub [release](https://github.com/NordicSemiconductor/NAT-TestFirmware/releases) is created and pre-build hex-files for all supported boards are attached.
