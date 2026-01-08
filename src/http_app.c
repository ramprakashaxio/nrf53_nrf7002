
#include <zephyr/logging/log.h>

#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/sys/printk.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
/* STEP 1.2 - Include the header file of the HTTP client library */
#include <zephyr/net/http/client.h>

#include "http_app.h"

LOG_MODULE_REGISTER(http_app, CONFIG_LOG_DEFAULT_LEVEL);

volatile bool got_ip = false;

#define RECV_BUF_SIZE 4096
char recv_buf[RECV_BUF_SIZE];

struct sockaddr_storage server_info;

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

//____________________ HTTP Application _________________________


#define HTTP_HOST "api1.prescoipd.com"
#define HTTP_PORT 443
#define LOGIN_PATH "/api/login"

/* Login credentials */
#define USERNAME "hope_watch"
#define PASSWORD "HopeWatch@2025"

int sock_fd = -1;
char token[2048];
struct sockaddr_in addr;
char send_buf[2048];

uint8_t http_state = 0;

// Function Prototype
#if 0
static void send_http_request(void);
static int send_http_token_req(void);
void send_patient_data();
#endif
int resolve_server(void);
void register_certificate(void);
static int http_login(void);
static int http_post_patient_data(void);


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
		switch (http_state)
		{
		case 0 :
			/* code */
			printk("\r\nresolve_server()");
			if(resolve_server()== 0)
			{
				http_state = 1;
			}
			break;
		case 1:
			LOG_INF("Login  request");
			// send_http_token_req();
            http_login();
			http_state = 2;
		case 2:
			LOG_INF("Patient Data");
			// send_patient_data();
            http_post_patient_data();
		default:
			break;
		}
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
		 return; 
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
		printk("Failed to setup peer verification, err %d, %s\n", errno, strerror(errno));
		return -errno;
	}

	/* Associate the socket with the security tag
	 * we have provisioned the certificate with.
	 */
	err = setsockopt(fd, SOL_TLS, TLS_SEC_TAG_LIST, tls_sec_tag, sizeof(tls_sec_tag));
	if (err) {
		printk("Failed to setup TLS sec tag, err %d, %s\n", errno, strerror(errno));
		return -errno;
	}

	err = setsockopt(fd, SOL_TLS, TLS_HOSTNAME,
			HTTP_HOST,
			sizeof(HTTP_HOST) - 1);
	if (err) {
		printk("Failed to setup TLS hostname, err %d, %s\n", errno, strerror(errno));
		return -errno;
	}
	return 0;
}

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

static int response_cb(struct http_response *rsp, enum http_final_call final_data, void *user_data)
{
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
	// LOG_INF("Closing socket: %d", sock_fd);
	// close(sock_fd);
	return 0;
}

static int response_patient_cb(struct http_response *rsp, enum http_final_call final_data, void *user_data)
{	
	LOG_INF("Response status: %s", rsp->http_status);

	if (rsp->body_frag_len > 0) {
		char body_buf[rsp->body_frag_len];
		strncpy(body_buf, rsp->body_frag_start, rsp->body_frag_len);
		body_buf[rsp->body_frag_len] = '\0';
		LOG_INF("Received: %s", body_buf);
	}
    // LOG_INF("Closing socket: %d", sock_fd);
	// close(sock_fd);
	return 0;
}

int server_connect(void)
{	
    int err ;
	sock_fd = socket(AF_INET,SOCK_STREAM, IPPROTO_TLS_1_3);
	if (sock_fd == -1) {
		printk("Failed to open socket!\n");
		return sock_fd;
	}

	/* Setup TLS socket options */
	err = tls_setup(sock_fd);
	if (err<0) {
		printk("Failed to Setup TLS!\n");
		return err;
	}

	printk("Connecting to %s:%d\n", HTTP_HOST,
	       HTTP_PORT);
	err = connect(sock_fd,(struct sockaddr *)&server_info, sizeof(struct sockaddr_in));
	if (err < 0) {
		printk("Connecting to server failed, err: %d, %s", errno, strerror(errno));
		return errno;
	}
	printk("Connected to server");
	return 0;
}


static int http_login(void)
{
	int err;	
	if(server_connect()!=0){
		return -1;
	}
	const char *headers[] = {"Content-Type: application/x-www-form-urlencoded\r\n","Connection: close\r\n", NULL};

	struct http_request req;
	memset(&req, 0, sizeof(req));
 	static char body[128];
	memset(body,0x00,128);
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

	LOG_INF("Login  request");
	err = http_client_req(sock_fd, &req, 5000, NULL);
	if (err < 0) {
		LOG_ERR("Failed to send HTTP POST login request, err: %d", err);
	}
	if (sock_fd > 0)
	{
  	  LOG_INF("Closing socket: %d", sock_fd);
	  close(sock_fd);
	  sock_fd = -1;
	}
	
	return err;
}

static int http_post_patient_data(void)
{
	/* STEP 8 - Define the function to send a GET request to the HTTP server */

	int err;
	// register_certificate();

	if(server_connect()!=0)
	{
        printk("Server Not Conneted\n");
		return -1;
	}

	static char auth_buf[1500];
	memset(auth_buf, 0x00, 1500);
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

	LOG_INF("Patient request");
	err = http_client_req(sock_fd, &req, 5000, NULL);
	if (err < 0) {
		LOG_ERR("Failed to send HTTP POST Data, err: %d", err);
	}
    LOG_INF("Closing socket: %d", sock_fd);
	close(sock_fd);
    sock_fd = -1;
	return err;
}


#if 0

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
	if (err < 0 ) {
		printk("TLS setup Failed\n");
		return err;
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
	int bytes;
	size_t off;
	sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TLS_1_3);
	if (sock_fd == -1) {
		printk("Failed to open socket!\n");
		goto clean_up;
	}

	/* Setup TLS socket options */
	err = tls_setup(sock_fd);
	if (err < 0) {
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
#endif
