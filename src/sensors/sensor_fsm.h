/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SENSOR_FSM_H
#define SENSOR_FSM_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

/* Sensor data structure */
typedef struct {
	float temperature;
	uint16_t heart_rate;
	uint8_t hr_confidence;
	uint16_t spo2;
	uint8_t spo2_confidence;
	bool sos_alert;
	bool fall_detected;
	int16_t accel_x, accel_y, accel_z;
	uint8_t battery_percent;
	uint64_t timestamp;
} sensor_data_t;

/* FSM functions */
int sensor_fsm_init(void);
void sensor_thread_entry(void *p1, void *p2, void *p3);

#endif /* SENSOR_FSM_H */
