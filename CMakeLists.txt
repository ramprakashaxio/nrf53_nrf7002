#
# Copyright (c) 2022 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nrf_wifi_sta)

target_include_directories(app PUBLIC ${ZEPHYR_BASE}/subsys/net/ip)

# Generate hex files from pem files
set(gen_dir ${CMAKE_CURRENT_BINARY_DIR}/certs)
zephyr_include_directories(${gen_dir})
generate_inc_file_for_target(
    app
    cert/DigiCertGlobalG3.pem
    ${gen_dir}/DigiCertGlobalG3.pem.inc
    )

target_sources(app PRIVATE
	src/main.c
	src/wifi/wifi_sta.c
	src/wifi/http_app.c
	# Future sensor and BLE files will be added here
	# src/sensors/sensor_fsm.c
	# src/sensors/sensor_data.c
	# src/ble/ble_service.c
	# src/ble/ble_advertising.c
	# src/common/data_manager.c
)
