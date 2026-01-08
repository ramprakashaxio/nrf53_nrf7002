#ifndef HTTP_APP_H_
#define HTTP_APP_H_

#include <zephyr/kernel.h>

extern struct k_sem run_app;
extern const k_tid_t start_http_thread_id;
extern volatile bool got_ip;

void start_http_thread();

#endif