/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <zephyr/kernel.h>

/* Forward declaration */
typedef struct sensor_data sensor_data_t;

/* BLE functions */
int ble_service_init(void);
void ble_thread_entry(void *p1, void *p2, void *p3);
void ble_notify_data(const sensor_data_t *data);

#endif /* BLE_SERVICE_H */
