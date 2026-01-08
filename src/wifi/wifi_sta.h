/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef WIFI_STA_H
#define WIFI_STA_H

#include <zephyr/kernel.h>
#include <stdbool.h>

/* WiFi connection status */
extern volatile bool got_ip;

/* WiFi functions */
int wifi_sta_init(void);
void wifi_thread_entry(void *p1, void *p2, void *p3);

/* Thread definition */
extern struct k_thread wifi_thread_data;
extern k_tid_t wifi_thread_id;

#endif /* WIFI_STA_H */
