/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 * @brief WiFi station sample
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sta, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>
#include <zephyr/init.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
/* STEP 1.2 - Include the header file of the HTTP client library */
#include <zephyr/net/http/client.h>
#ifdef CONFIG_WIFI_READY_LIB
#include <net/wifi_ready.h>
#endif /* CONFIG_WIFI_READY_LIB */

#if defined(CONFIG_BOARD_NRF7002DK_NRF5340_CPUAPP_NRF7001) || \
	defined(CONFIG_BOARD_NRF7002DK_NRF5340_CPUAPP)
#include <zephyr/drivers/wifi/nrf_wifi/bus/qspi_if.h>
#endif

#include "net_private.h"

#define WIFI_SHELL_MODULE "wifi"

#define WIFI_SHELL_MGMT_EVENTS (NET_EVENT_WIFI_CONNECT_RESULT |		\
				NET_EVENT_WIFI_DISCONNECT_RESULT)

#define MAX_SSID_LEN        32
#define STATUS_POLLING_MS   300

/* 1000 msec = 1 sec */
#define LED_SLEEP_TIME_MS   100

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)
/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

static struct net_mgmt_event_callback wifi_shell_mgmt_cb;
static struct net_mgmt_event_callback net_shell_mgmt_cb;

#ifdef CONFIG_WIFI_READY_LIB
static K_SEM_DEFINE(wifi_ready_state_changed_sem, 0, 1);
static bool wifi_ready_status;
#endif /* CONFIG_WIFI_READY_LIB */

static struct {
	const struct shell *sh;
	union {
		struct {
			uint8_t connected	: 1;
			uint8_t connect_result	: 1;
			uint8_t disconnect_requested	: 1;
			uint8_t _unused		: 5;
		};
		uint8_t all;
	};
} context;

volatile bool got_ip = false;

#define RECV_BUF_SIZE 4096
char recv_buf[RECV_BUF_SIZE];

#if 0
char root_ca [] ="";
#else
static const char root_ca[] = {
	#include "DigiCertGlobalG3.pem.inc"

	/* Null terminate certificate if running Mbed TLS on the application core.
	 * Required by TLS credentials API.
	 */
	IF_ENABLED(CONFIG_TLS_CREDENTIALS, (0x00))
};
#endif

static void do_http_post(void);
static void send_http_request(void);
static int send_http_token_req(void);
void send_patient_data();
int resolve_server(void);
void register_certificate(void);
void start_http_thread();
static int client_http_get(void);
static int client_http_post_data(void);

K_THREAD_DEFINE(start_http_thread_id, CONFIG_STA_SAMPLE_START_WIFI_THREAD_STACK_SIZE,
		start_http_thread, NULL, NULL, NULL,
		7, 0, -1);

void toggle_led(void)
{
	int ret;

	if (!device_is_ready(led.port)) {
		LOG_ERR("LED device is not ready");
		return;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Error %d: failed to configure LED pin", ret);
		return;
	}

	while (1) {
		if (context.connected) {
			gpio_pin_toggle_dt(&led);
			k_msleep(LED_SLEEP_TIME_MS);
		} else {
			gpio_pin_set_dt(&led, 0);
			k_msleep(LED_SLEEP_TIME_MS);
		}
	}
}

K_THREAD_DEFINE(led_thread_id, 1024, toggle_led, NULL, NULL, NULL,
		7, 0, 0);

static int cmd_wifi_status(void)
{
	struct net_if *iface = net_if_get_default();
	struct wifi_iface_status status = { 0 };

	if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status,
				sizeof(struct wifi_iface_status))) {
		LOG_INF("Status request failed");

		return -ENOEXEC;
	}

	LOG_INF("==================");
	LOG_INF("State: %s", wifi_state_txt(status.state));

	if (status.state >= WIFI_STATE_ASSOCIATED) {
		uint8_t mac_string_buf[sizeof("xx:xx:xx:xx:xx:xx")];

		LOG_INF("Interface Mode: %s",
		       wifi_mode_txt(status.iface_mode));
		LOG_INF("Link Mode: %s",
		       wifi_link_mode_txt(status.link_mode));
		LOG_INF("SSID: %.32s", status.ssid);
		LOG_INF("BSSID: %s",
		       net_sprint_ll_addr_buf(
				status.bssid, WIFI_MAC_ADDR_LEN,
				mac_string_buf, sizeof(mac_string_buf)));
		LOG_INF("Band: %s", wifi_band_txt(status.band));
		LOG_INF("Channel: %d", status.channel);
		LOG_INF("Security: %s", wifi_security_txt(status.security));
		LOG_INF("MFP: %s", wifi_mfp_txt(status.mfp));
		LOG_INF("RSSI: %d", status.rssi);
	}
	return 0;
}

static void handle_wifi_connect_result(struct net_mgmt_event_callback *cb)
{
	const struct wifi_status *status =
		(const struct wifi_status *) cb->info;

	if (context.connected) {
		return;
	}

	if (status->status) {
		LOG_ERR("Connection failed (%d)", status->status);
	} else {
		LOG_INF("Connected");
		context.connected = true;
		
	}

	context.connect_result = true;
}

static void handle_wifi_disconnect_result(struct net_mgmt_event_callback *cb)
{
	const struct wifi_status *status =
		(const struct wifi_status *) cb->info;

	if (!context.connected) {
		return;
	}

	if (context.disconnect_requested) {
		LOG_INF("Disconnection request %s (%d)",
			 status->status ? "failed" : "done",
					status->status);
		context.disconnect_requested = false;
	} else {
		LOG_INF("Received Disconnected");
		context.connected = false;
	}

	cmd_wifi_status();
}

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
				     uint32_t mgmt_event, struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT:
		handle_wifi_connect_result(cb);
		break;
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		handle_wifi_disconnect_result(cb);
		got_ip = false;
		break;
	default:
		break;
	}
}

static void print_dhcp_ip(struct net_mgmt_event_callback *cb)
{
	/* Get DHCP info from struct net_if_dhcpv4 and print */
	const struct net_if_dhcpv4 *dhcpv4 = cb->info;
	const struct in_addr *addr = &dhcpv4->requested_ip;
	char dhcp_info[128];

	net_addr_ntop(AF_INET, addr, dhcp_info, sizeof(dhcp_info));

	LOG_INF("DHCP IP address: %s", dhcp_info);
}
static void net_mgmt_event_handler(struct net_mgmt_event_callback *cb,
				    uint32_t mgmt_event, struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_IPV4_DHCP_BOUND:
		print_dhcp_ip(cb);
		got_ip = true;
		// // do_http_post();
		// // send_http_request();
		// send_http_token_req();
		
		break;
	default:
		break;
	}
}

static int wifi_connect(void)
{
	struct net_if *iface = net_if_get_first_wifi();

	context.connected = false;
	context.connect_result = false;

	if (net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0)) {
		LOG_ERR("Connection request failed");

		return -ENOEXEC;
	}

	LOG_INF("Connection requested");

	return 0;
}

int bytes_from_str(const char *str, uint8_t *bytes, size_t bytes_len)
{
	size_t i;
	char byte_str[3];

	if (strlen(str) != bytes_len * 2) {
		LOG_ERR("Invalid string length: %zu (expected: %d)\n",
			strlen(str), bytes_len * 2);
		return -EINVAL;
	}

	for (i = 0; i < bytes_len; i++) {
		memcpy(byte_str, str + i * 2, 2);
		byte_str[2] = '\0';
		bytes[i] = strtol(byte_str, NULL, 16);
	}

	return 0;
}

int start_app(void)
{
#if defined(CONFIG_BOARD_NRF7002DK_NRF5340_CPUAPP_NRF7001) || \
	defined(CONFIG_BOARD_NRF7002DK_NRF5340_CPUAPP)
	if (strlen(CONFIG_NRF70_QSPI_ENCRYPTION_KEY)) {
		int ret;
		char key[QSPI_KEY_LEN_BYTES];

		ret = bytes_from_str(CONFIG_NRF70_QSPI_ENCRYPTION_KEY, key, sizeof(key));
		if (ret) {
			LOG_ERR("Failed to parse encryption key: %d\n", ret);
			return 0;
		}

		LOG_DBG("QSPI Encryption key: ");
		for (int i = 0; i < QSPI_KEY_LEN_BYTES; i++) {
			LOG_DBG("%02x", key[i]);
		}
		LOG_DBG("\n");

		ret = qspi_enable_encryption(key);
		if (ret) {
			LOG_ERR("Failed to enable encryption: %d\n", ret);
			return 0;
		}
		LOG_INF("QSPI Encryption enabled");
	} else {
		LOG_INF("QSPI Encryption disabled");
	}
#endif /* CONFIG_BOARD_NRF7002DK_NRF5340_CPUAPP_NRF7001 || CONFIG_BOARD_NRF7002DK_NRF5340_CPUAPP */

	LOG_INF("Static IP address (overridable): %s/%s -> %s",
		CONFIG_NET_CONFIG_MY_IPV4_ADDR,
		CONFIG_NET_CONFIG_MY_IPV4_NETMASK,
		CONFIG_NET_CONFIG_MY_IPV4_GW);

	while (1) {
#ifdef CONFIG_WIFI_READY_LIB
		int ret;

		LOG_INF("Waiting for Wi-Fi to be ready");
		ret = k_sem_take(&wifi_ready_state_changed_sem, K_FOREVER);
		if (ret) {
			LOG_ERR("Failed to take semaphore: %d", ret);
			return ret;
		}

check_wifi_ready:
		if (!wifi_ready_status) {
			LOG_INF("Wi-Fi is not ready");
			/* Perform any cleanup and stop using Wi-Fi and wait for
			 * Wi-Fi to be ready
			 */
			continue;
		}
#endif /* CONFIG_WIFI_READY_LIB */
		wifi_connect();

		while (!context.connect_result) {
			cmd_wifi_status();
			k_sleep(K_MSEC(STATUS_POLLING_MS));
		}

		if (context.connected) {
			cmd_wifi_status();
#ifdef CONFIG_WIFI_READY_LIB
			ret = k_sem_take(&wifi_ready_state_changed_sem, K_FOREVER);
			if (ret) {
				LOG_ERR("Failed to take semaphore: %d", ret);
				return ret;
			}
			goto check_wifi_ready;
#else
			k_sleep(K_FOREVER);
#endif /* CONFIG_WIFI_READY_LIB */
		}
	}

	return 0;
}

#ifdef CONFIG_WIFI_READY_LIB
void start_wifi_thread(void);
#define THREAD_PRIORITY K_PRIO_COOP(CONFIG_NUM_COOP_PRIORITIES - 1)
K_THREAD_DEFINE(start_wifi_thread_id, CONFIG_STA_SAMPLE_START_WIFI_THREAD_STACK_SIZE,
		start_wifi_thread, NULL, NULL, NULL,
		THREAD_PRIORITY, 0, -1);

void start_wifi_thread(void)
{
	start_app();
}

void wifi_ready_cb(bool wifi_ready)
{
	LOG_DBG("Is Wi-Fi ready?: %s", wifi_ready ? "yes" : "no");
	wifi_ready_status = wifi_ready;
	k_sem_give(&wifi_ready_state_changed_sem);
}
#endif /* CONFIG_WIFI_READY_LIB */

void net_mgmt_callback_init(void)
{
	memset(&context, 0, sizeof(context));

	net_mgmt_init_event_callback(&wifi_shell_mgmt_cb,
				     wifi_mgmt_event_handler,
				     WIFI_SHELL_MGMT_EVENTS);

	net_mgmt_add_event_callback(&wifi_shell_mgmt_cb);

	net_mgmt_init_event_callback(&net_shell_mgmt_cb,
				     net_mgmt_event_handler,
				     NET_EVENT_IPV4_DHCP_BOUND);

	net_mgmt_add_event_callback(&net_shell_mgmt_cb);

	LOG_INF("Starting %s with CPU frequency: %d MHz", CONFIG_BOARD, SystemCoreClock/MHZ(1));
	k_sleep(K_SECONDS(1));
}

#ifdef CONFIG_WIFI_READY_LIB
static int register_wifi_ready(void)
{
	int ret = 0;
	wifi_ready_callback_t cb;
	struct net_if *iface = net_if_get_first_wifi();

	if (!iface) {
		LOG_ERR("Failed to get Wi-Fi interface");
		return -1;
	}

	cb.wifi_ready_cb = wifi_ready_cb;

	LOG_DBG("Registering Wi-Fi ready callbacks");
	ret = register_wifi_ready_callback(cb, iface);
	if (ret) {
		LOG_ERR("Failed to register Wi-Fi ready callbacks %s", strerror(ret));
		return ret;
	}

	return ret;
}
#endif /* CONFIG_WIFI_READY_LIB */

int main(void)
{
	int ret = 0;

	net_mgmt_callback_init();
	// register_certificate();
k_thread_start(start_http_thread_id);
#ifdef CONFIG_WIFI_READY_LIB
	ret = register_wifi_ready();
	if (ret) {
		return ret;
	}
	k_thread_start(start_wifi_thread_id);
#else
	
	start_app();

#endif /* CONFIG_WIFI_READY_LIB */
	return ret;
}

//____________________ HTTP Application _________________________


#define HTTP_HOST "api1.prescoipd.com"
#define HTTP_PORT 443
#define LOGIN_PATH "/api/login"

/* Login credentials */
#define USERNAME "hope_watch"
#define PASSWORD "HopeWatch@2025"

uint8_t http_state = 0;

void start_http_thread()
{
	printk("\r \tstart_http_thread\t \r\n");
	register_certificate();
	
	while(1)
	{
		while(!got_ip){
			k_sleep(K_MSEC(1000));
			http_state = 0;
		}
client_http_get();
client_http_post_data();
		// switch (http_state)
		// {
		// case 0 :
		// 	/* code */
		// 	printk("\r\nresolve_server()");
		// 	resolve_server();
		// 	http_state = 1;
		// 	break;
		// case 1:
		// 	printk("\r\nsend_http_token_req");
		// 	send_http_token_req();
		// 	http_state = 2;
		// case 2:
		// 	printk("\r\nsend_patient_data\r\n");
		// 	send_patient_data();
		// default:
		// 	break;
		// }
		k_sleep(K_MSEC(10000));
	}
}

void register_certificate(void)
{
	/* Security tag that we have provisioned the certificate with */

	int ret = tls_credential_add(1, TLS_CREDENTIAL_CA_CERTIFICATE, 
		root_ca, sizeof(root_ca)); 
	if (ret < 0) {
		 printk("Failed to register public certificate: %d", ret); 
		 return ret; 
	}

}

/* Setup TLS options on a given socket */
int tls_setup(int fd)
{
	int err;
	int verify;
 	

	const sec_tag_t tls_sec_tag[] = {
		1
	};
	
	/* Set up TLS peer verification */
	enum {
		NONE = 0,
		OPTIONAL = 1,
		REQUIRED = 2,
	};

	verify = NONE;

	err = setsockopt(fd, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));
	if (err) {
		printk("Failed to setup peer verification, err %d\n", errno);
		return err;
	}

	/* Associate the socket with the security tag
	 * we have provisioned the certificate with.
	 */
	err = setsockopt(fd, SOL_TLS, TLS_SEC_TAG_LIST, tls_sec_tag, sizeof(tls_sec_tag));
	if (err) {
		printk("Failed to setup TLS sec tag, err %d\n", errno);
		return err;
	}

	err = setsockopt(fd, SOL_TLS, TLS_HOSTNAME,
			HTTP_HOST,
			sizeof(HTTP_HOST) - 1);
	if (err) {
		printk("Failed to setup TLS hostname, err %d\n", errno);
		return err;
	}
	return 0;
}

int sock_fd = -1;
char token[2048];
struct sockaddr_in addr;
char send_buf[2048];


static const char json_payload[] ="{"
"\"BLE_FW\": \"1\","
"\"WIFI_FW\": \"1\","
"\"Battery %\": 100,"
"\"Patient ID\": \"12345\","
"\"Patient Data\": ["
"{"
"\"Temperature\": 98.6,"
"\"Heart Rate\": 72,"
"\"Heart Rate Confidence Index\": 95,"
"\"SPIO2 Level\": 98,"
"\"SPIO2 Level Confidence Index\": 90,"
"\"SOS Alert\": 0,"
"\"Fall Detection\": 0,"
"\"Accel X\": 12345,"
"\"Accel Y\": 12345,"
"\"Accel Z\": 12345"
"}"
"]"
"}";

struct sockaddr_storage server_info;

int resolve_server(void)
{
	char peer_addr[INET_ADDRSTRLEN];

	printk("Looking up %s\n", HTTP_HOST);

	 /* Resolve hostname */
    struct addrinfo hints = { .ai_family = AF_INET };
    struct addrinfo *res;
    if (getaddrinfo(HTTP_HOST, NULL, &hints, &res) != 0) {
        LOG_ERR("DNS lookup failed");
        return -1;
    }
	// struct sockaddr_in addr;
	struct sockaddr_in *addr_in = (struct sockaddr_in *)&server_info;
    addr_in->sin_addr = ((struct sockaddr_in *)(res->ai_addr))->sin_addr;
	addr_in->sin_family = ((struct sockaddr_in *)(res->ai_addr))->sin_family;
	addr_in->sin_port = htons(HTTP_PORT);

	inet_ntop(res->ai_family, &((struct sockaddr_in *)(res->ai_addr))->sin_addr, peer_addr,
		INET_ADDRSTRLEN);
		printk("Resolved %s (%s)\n", peer_addr, net_family2str(res->ai_family));
	freeaddrinfo(res);
	return 0;
}

static void send_http_request(void)
{
	printk("Looking up %s\n", HTTP_HOST);
	resolve_server();
	send_http_token_req();
	send_patient_data();
}

static int send_http_token_req(void)
{
	int err;
	char *p;
	int bytes;
	size_t off;
	int ret = 0;
	
	// struct sockaddr_in *addr_in = (struct sockaddr_in *)&server_info;

	sock_fd = socket(AF_INET,SOCK_STREAM, IPPROTO_TLS_1_3);
	if (sock_fd == -1) {
		printk("Failed to open socket!\n");
		ret =  -1;
		goto clean_up;
	}

	/* Setup TLS socket options */
	err = tls_setup(sock_fd);
	if (err) {
		ret =  -1;
		goto clean_up;
	}

	printk("Connecting to %s:%d\n", HTTP_HOST,
	       HTTP_PORT);
	err = connect(sock_fd,(struct sockaddr *)&server_info, sizeof(struct sockaddr_in));
	if (err) {
		printk("connect() failed, err: %d\n", errno);
		ret =  -1;
		goto clean_up;
	}

	 /* Prepare x-www-form-urlencoded body */
    char body[128];
    snprintk(body, sizeof(body), "username=%s&password=%s", USERNAME, PASSWORD);

    /* Prepare POST request */
	
    snprintk(send_buf, sizeof(send_buf),
             "POST " LOGIN_PATH " HTTP/1.1\r\n"
             "Host: " HTTP_HOST "\r\n"
             "Content-Type: application/x-www-form-urlencoded\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             (int)strlen(body), body);
    /* Send request */
    err = send(sock_fd, send_buf, strlen(send_buf), 0);
	if (err < 0) {
		    printk("Send failed: %d", errno);
		    close(sock_fd);
			ret =  -1;
		    return -1;
		}
    printk("POST request sent");

	off = 0;
	do {
		bytes = recv(sock_fd, &recv_buf[off], 1024, 0);
		if (bytes < 0) {
			printk("recv() failed, err %d\n", errno);
			ret =  -1;
			goto clean_up;
		}
		off += bytes;			
	} while (bytes != 0 || off>RECV_BUF_SIZE);

	printk("Received %d bytes\n", off);
	if (off < sizeof(recv_buf)) {
		recv_buf[off] = '\0';
	} else {
		recv_buf[sizeof(recv_buf) - 1] = '\0';
	}
	
	char *tok_start = strstr(recv_buf, "\"token\":\"");
	if (tok_start) {
		tok_start += strlen("\"token\":\"");
		char *tok_end = strchr(tok_start, '"');
		if (tok_end) {
			size_t tok_len = tok_end - tok_start;
			strncpy(token, tok_start, tok_len);
			token[tok_len] = '\0';
			printf("Token: %s", (token));
		}
	}
	clean_up:
	(void)close(sock_fd);
	return ret;
}

void send_patient_data()
{
	int err;
	char *p;
	int bytes;
	size_t off;
	sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TLS_1_3);
	if (sock_fd == -1) {
		printk("Failed to open socket!\n");
		goto clean_up;
	}

	/* Setup TLS socket options */
	err = tls_setup(sock_fd);
	if (err) {
		goto clean_up;
	}

	err = connect(sock_fd,(struct sockaddr *)&server_info, sizeof(struct sockaddr_in));
	if (err) {
		printk("connect() failed, err: %d\n", errno);
		goto clean_up;
	}

	#if 0
	printk("get Patient list data\n");
	// Build HTTP request with JSON + Bearer token
	snprintk(send_buf, sizeof(send_buf),
			"POST /api/get-patient-list-data HTTP/1.1\r\n"
			"Host: " HTTP_HOST "\r\n"
			"Authorization: Bearer %s\r\n"
			"Content-Type: application/x-www-form-urlencoded\r\n"
			"Content-Length: 0\r\n"
			"Connection: close\r\n"
			"\r\n",
			token);
	#else
		printk("get Patient list data\n");
	// Build HTTP request with JSON + Bearer token
	snprintk(send_buf, sizeof(send_buf),
			"POST /api/add-patient-clinical-data HTTP/1.1\r\n"
			"Host: " HTTP_HOST "\r\n"
			"Authorization: Bearer %s\r\n"
			"Content-Type: application/json\r\n"
			"Content-Length: %d\r\n"
			"Connection: close\r\n"
			"\r\n"
			"%s",token, strlen(json_payload),json_payload);

	#endif

	int ret = send(sock_fd, send_buf, strlen(send_buf), 0);
	if (ret < 0) {
		    printk("Send failed: %d", errno);
		    close(sock_fd);
		    return;
		}
	
	off = 0;
	do {
		bytes = recv(sock_fd, &recv_buf[off], 1024, 0);
		if (bytes < 0) {
			printk("recv() failed, err %d\n", errno);
			goto clean_up;
		}
		off += bytes;			
	} while (bytes != 0 || off>RECV_BUF_SIZE);

	printk("Received %d bytes\n", off);
	if (off < sizeof(recv_buf)) {
		recv_buf[off] = '\0';
	} else {
		recv_buf[sizeof(recv_buf) - 1] = '\0';
	}
	for (int i = 0; i < off; i++) {
		printk("%c", recv_buf[i]);
	}

	printk("Finished, closing socket.\n");

clean_up:
	(void)close(sock_fd);
}

static int response_cb(struct http_response *rsp, enum http_final_call final_data, void *user_data)
{
	/* STEP 9 - Define the callback function to print the body */
	LOG_INF("Response status: %s", rsp->http_status);

	if (rsp->body_frag_len > 0) {
		char body_buf[rsp->body_frag_len];
		strncpy(body_buf, rsp->body_frag_start, rsp->body_frag_len);
		body_buf[rsp->body_frag_len] = '\0';
		LOG_INF("Received: %s", body_buf);
		char *tok_start = strstr(body_buf, "\"token\":\"");
		if (tok_start) {
			tok_start += strlen("\"token\":\"");
			char *tok_end = strchr(tok_start, '"');
			if (tok_end) {
				size_t tok_len = tok_end - tok_start;
				strncpy(token, tok_start, tok_len);
				token[tok_len] = '\0';
				printf("Token: %s", (token));
			}
		}
	}
	LOG_INF("Closing socket: %d", sock_fd);
	close(sock_fd);
	return 0;
}

static int response_patient_cb(struct http_response *rsp, enum http_final_call final_data, void *user_data)
{
	/* STEP 9 - Define the callback function to print the body */
	LOG_INF("Response status: %s", rsp->http_status);

	if (rsp->body_frag_len > 0) {
		char body_buf[rsp->body_frag_len];
		strncpy(body_buf, rsp->body_frag_start, rsp->body_frag_len);
		body_buf[rsp->body_frag_len] = '\0';
		LOG_INF("Received: %s", body_buf);
	}
	LOG_INF("Closing socket: %d", sock_fd);
	close(sock_fd);
	return 0;
}


static int client_http_get(void)
{
	/* STEP 8 - Define the function to send a GET request to the HTTP server */

	int err;
	char *p;
	int bytes;
	size_t off;
	int ret = 0;
	// register_certificate();
	resolve_server();
	
	// struct sockaddr_in *addr_in = (struct sockaddr_in *)&server_info;

	sock_fd = socket(AF_INET,SOCK_STREAM, IPPROTO_TLS_1_3);
	if (sock_fd == -1) {
		printk("Failed to open socket!\n");
		return sock_fd;
	}

	/* Setup TLS socket options */
	err = tls_setup(sock_fd);
	if (err) {
		ret =  -1;
		return -1;
	}

	printk("Connecting to %s:%d\n", HTTP_HOST,
	       HTTP_PORT);
	err = connect(sock_fd,(struct sockaddr *)&server_info, sizeof(struct sockaddr_in));
	if (err) {
		printk("connect() failed, err: %d\n", errno);
		ret =  -1;
		return -1;
	}
	
	const char *headers[] = {"Content-Type: application/x-www-form-urlencoded\r\n","Connection: close\r\n", NULL};

	struct http_request req;
	memset(&req, 0, sizeof(req));
 	char body[128];
    snprintk(body, sizeof(body), "username=%s&password=%s", USERNAME, PASSWORD);
	req.header_fields = headers;
	req.method = HTTP_POST;
	req.url = LOGIN_PATH;
	req.host = HTTP_HOST;
	req.payload = body;
	req.payload_len = strlen(req.payload);
	req.protocol = "HTTP/1.1";
	req.response = response_cb;
	req.recv_buf = recv_buf;
	req.recv_buf_len = sizeof(recv_buf);

	LOG_INF("HTTP POST TOKEN  request");
	err = http_client_req(sock_fd, &req, 5000, NULL);
	if (err < 0) {
		LOG_ERR("Failed to send HTTP POST request, err: %d", err);
	}

	return err;
}



static int client_http_post_data(void)
{
	/* STEP 8 - Define the function to send a GET request to the HTTP server */

	int err;
	char *p;
	int bytes;
	size_t off;
	int ret = 0;
	// register_certificate();
	resolve_server();
	
	// struct sockaddr_in *addr_in = (struct sockaddr_in *)&server_info;

	sock_fd = socket(AF_INET,SOCK_STREAM, IPPROTO_TLS_1_3);
	if (sock_fd == -1) {
		printk("Failed to open socket!\n");
		return sock_fd;
	}

	/* Setup TLS socket options */
	err = tls_setup(sock_fd);
	if (err) {
		ret =  -1;
		return -1;
	}

	printk("Connecting to %s:%d\n", HTTP_HOST,
	       HTTP_PORT);
	err = connect(sock_fd,(struct sockaddr *)&server_info, sizeof(struct sockaddr_in));
	if (err) {
		printk("connect() failed, err: %d\n", errno);
		ret =  -1;
		return -1;
	}
	
	char auth_buf[1500];
	sprintf(auth_buf,"Authorization: Bearer %s\r\n",token);
	const char *headers[] = {"Content-Type: application/json\r\n",auth_buf,"Connection: close\r\n", NULL};

	struct http_request req;
	memset(&req, 0, sizeof(req));
 	
	req.header_fields = headers;
	req.method = HTTP_POST;
	req.url = "/api/add-patient-clinical-data";
	req.host = HTTP_HOST;
	req.payload = json_payload;
	req.payload_len = strlen(req.payload);
	req.protocol = "HTTP/1.1";
	req.response = response_patient_cb;
	req.recv_buf = recv_buf;
	req.recv_buf_len = sizeof(recv_buf);

	LOG_INF("HTTP POST TOKEN  request");
	err = http_client_req(sock_fd, &req, 5000, NULL);
	if (err < 0) {
		LOG_ERR("Failed to send HTTP POST request, err: %d", err);
	}

	return err;
}

