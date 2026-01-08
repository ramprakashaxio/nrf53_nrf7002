/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 * @brief HopeWatch Multi-Service Device Main
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main_app, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "wifi/wifi_sta.h"
// #include "sensors/sensor_fsm.h"
// #include "ble/ble_service.h"
// #include "common/data_manager.h"

/* Thread definitions */
K_THREAD_DEFINE(wifi_thread, CONFIG_STA_SAMPLE_START_WIFI_THREAD_STACK_SIZE, 
		wifi_thread_entry, NULL, NULL, NULL, 5, 0, 0);
// K_THREAD_DEFINE(sensor_thread, 2048, sensor_thread_entry, NULL, NULL, NULL, 6, 0, 0);
// K_THREAD_DEFINE(ble_thread, 2048, ble_thread_entry, NULL, NULL, NULL, 7, 0, 0);

int main(void)
{
	printk("==================================================\n");
	printk("HopeWatch Multi-Service Device Starting...\n");
	printk("nRF7002DK - WiFi + Sensor + BLE Integration\n");
	printk("==================================================\n");
	
	/* Initialize subsystems */
	wifi_sta_init();
	// data_manager_init();
	// sensor_fsm_init();
	// ble_service_init();
	
	/* Register data flow callbacks (for future integration) */
	// data_manager_register_wifi_callback(http_app_send_data);
	// data_manager_register_ble_callback(ble_notify_data);
	
	LOG_INF("All subsystems initialized");
	LOG_INF("WiFi Thread will start automatically");
	
	/* Main thread can handle system monitoring or go to sleep */
	while (1) {
		k_sleep(K_SECONDS(10));
		LOG_INF("System running - WiFi connected: %s", got_ip ? "Yes" : "No");
	}
	
	return 0;
}
