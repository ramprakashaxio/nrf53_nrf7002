/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

/* Forward declaration */
typedef struct sensor_data sensor_data_t;

/* Data manager functions */
void data_manager_init(void);
void data_manager_update_sensors(const sensor_data_t *data);
const sensor_data_t* data_manager_get_latest(void);
bool data_manager_has_new_data(void);
void data_manager_mark_sent(void);

/* Callback registrations */
void data_manager_register_wifi_callback(void (*callback)(const sensor_data_t*));
void data_manager_register_ble_callback(void (*callback)(const sensor_data_t*));

#endif /* DATA_MANAGER_H */
