/*
 * http.c
 *
 *  Created on: 25 Jan 2016
 *      Author: User
 */
#include <c_types.h>
#include <mem.h>
#include <os_type.h>
#include <osapi.h>
#include "ip_addr.h"
#include "espconn.h"
#include "debug.h"
#include "config.h"

#include "espmissingincludes.h"

#define HTTPD_METHOD_GET 1
#define HTTPD_METHOD_POST 2

typedef struct HttpdConnData {
	struct espconn *conn;
	char requestType;
	char *url;
	char *getArgs;
	const void *cgiArg;
	void *cgiData;
	void *cgiPrivData; // Used for streaming handlers storing state between requests
	char *hostName;
} HttpdConnData;

static struct espconn httpConn;
static esp_tcp httpTcp;
extern bool setupMode;
static bool reboot = false;

static void ICACHE_FLASH_ATTR httpdParseHeader(char *h, HttpdConnData *conn) {
	int i;
	char first_line = false;

	if (os_strncmp(h, "GET ", 4)==0) {
		conn->requestType = HTTPD_METHOD_GET;
		first_line = true;
	} else if (os_strncmp(h, "Host:", 5)==0) {
		i=5;
		while (h[i]==' ') i++;
		conn->hostName=&h[i];
	} else if (os_strncmp(h, "POST ", 5)==0) {
		conn->requestType = HTTPD_METHOD_POST;
		first_line = true;
	}

	if (first_line) {
		char *e;

		//Skip past the space after POST/GET
		i=0;
		while (h[i]!=' ') i++;
		conn->url=h+i+1;

		//Figure out end of url.
		e=(char*)os_strstr(conn->url, " ");
		if (e==NULL) return; // ?
		*e=0; //terminate url part

		//Parse out the URL part before the GET parameters.
		conn->getArgs=(char*)os_strstr(conn->url, "?");
		if (conn->getArgs!=0) {
			*conn->getArgs=0;
			conn->getArgs++;
			TESTP("GET args = %s\n", conn->getArgs);
		} else {
			conn->getArgs=NULL;
		}
	}
}

static int ICACHE_FLASH_ATTR httpdHexVal(char c) {
	if (c>='0' && c<='9') return c-'0';
	if (c>='A' && c<='F') return c-'A'+10;
	if (c>='a' && c<='f') return c-'a'+10;
	return 0;
}

int ICACHE_FLASH_ATTR httpdUrlDecode(char *val, int valLen, char *ret, int retLen) {
	int s=0, d=0;
	int esced=0, escVal=0;
	while (s<valLen && d<retLen) {
		if (esced==1)  {
			escVal=httpdHexVal(val[s])<<4;
			esced=2;
		} else if (esced==2) {
			escVal+=httpdHexVal(val[s]);
			ret[d++]=escVal;
			esced=0;
		} else if (val[s]=='%') {
			esced=1;
		} else if (val[s]=='+') {
			ret[d++]=' ';
		} else {
			ret[d++]=val[s];
		}
		s++;
	}
	if (d<retLen) ret[d]=0;
	return d;
}

int ICACHE_FLASH_ATTR httpdFindArg(char *line, char *arg, char *buff, int buffLen) {
	char *p, *e;
	if (line==NULL) return 0;
	p = line;
	while(p!=NULL && *p!='\n' && *p!='\r' && *p!=0) {
		INFOP("findArg: %s\n", p);
		if (os_strncmp(p, arg, os_strlen(arg))==0 && p[strlen(arg)]=='=') {
			p += os_strlen(arg)+1; //move p to start of value
			e = (char*)os_strstr(p, "&");
			if (e==NULL) e = p+os_strlen(p);
			INFOP("findArg: val %s len %d\n", p, (e-p));
			return httpdUrlDecode(p, (e-p), buff, buffLen);
		}
		p = (char*)os_strstr(p, "&");
		if (p!=NULL) p += 1;
	}
	TESTP("Finding %s in %s: Not found\n", arg, line);
	return -1; //not found
}

void ICACHE_FLASH_ATTR tcp_sent_cb(void *arg) {
	struct espconn *conn = (struct espconn*) arg;
	TESTP("Sent CB\n");
	espconn_disconnect(conn);
}

void ICACHE_FLASH_ATTR replyFail(struct espconn *conn) {
	char bfr[2048];
	int l;
	l = os_sprintf(bfr, "HTTP/1.0 404 Not Found\r\nServer: esp8266-http/0.4\r\n"
			"Connection: close\r\nContent-Type: text/plain\r\n"
			"Content-Length: 12\r\n\r\nNot Found.\r\n");
	espconn_sent(conn, bfr, l);
}

void ICACHE_FLASH_ATTR replyOK(struct espconn *conn) {
	char bfr[2048];
	int l;
	l = os_sprintf(bfr, "HTTP/1.0 %d OK\r\nServer: esp8266-http/0.4\r\nConnection: close\r\n\r\n", 200);
	l += os_sprintf(&bfr[l], "<html><body>");
	l += os_sprintf(&bfr[l], "<h3>MQTT Setup</h3><form action=\"/setup\">");
	l += os_sprintf(&bfr[l], "<p>MQTT host: <input type=\"text\" name=\"MQTThost\" value=\"%s\"></p>", sysCfg.mqtt_host);
	l += os_sprintf(&bfr[l], "<p>MQTT Port: <input type=\"text\" name=\"MQTTport\" value=\"%d\" size=\"4\"></p>", sysCfg.mqtt_port);
	l += os_sprintf(&bfr[l], "<p>MQTT UserID: <input type=\"text\" name=\"MQTTuser\" value=\"%s\"></p>", sysCfg.mqtt_user);
	l += os_sprintf(&bfr[l], "<p>MQTT Pwd: <input type=\"text\" name=\"MQTTpass\" value=\"%s\"></p>", sysCfg.mqtt_pass);
	l += os_sprintf(&bfr[l], "<p>Device ID prefix: <input type=\"text\" name=\"DevPrefix\" value=\"%s\"></p>", sysCfg.deviceID_prefix);
	l += os_sprintf(&bfr[l], "Reboot: <input type=\"checkbox\" name=\"reboot\" value=\"yes\" %s> ", reboot ? "checked" : "");
	l += os_sprintf(&bfr[l], "<input type=\"submit\" name=\"Action\" value=\"Update\" style=\"border-radius: 8px; background-color: #CAD0E6\">");
	l += os_sprintf(&bfr[l], "</form></body></html>");
	espconn_sent(conn, bfr, l);
}

void ICACHE_FLASH_ATTR tcp_receive_cb(void *arg, char *pData, unsigned short len) {
	HttpdConnData c;
	char bfr[100];
	struct espconn *conn = (struct espconn*) arg;

	httpdParseHeader(pData, &c);
	TESTP("URL=%s\n", c.url);
	if (setupMode && os_strncmp(c.url, "/setup", 5) == 0) {
		if (httpdFindArg(c.getArgs, "Action", bfr, sizeof(bfr)) >= 0) {
			TESTP("Action=%s\n", bfr);
			if (os_strncmp(bfr, "Update", 6) == 0) {
				if (httpdFindArg(c.getArgs, "MQTThost", bfr, sizeof(bfr)) >= 0) {
					TESTP("MQTThost=%s\n", bfr);
					if (7 < strlen(bfr) && strlen(bfr) < 64) {
						strcpy(sysCfg.mqtt_host, bfr);
					}
				}
				if (httpdFindArg(c.getArgs, "MQTTport", bfr, sizeof(bfr)) >= 0) {
					TESTP("MQTTport=%s\n", bfr);
					if (1 <= strlen(bfr) && strlen(bfr) <= 4) {
						sysCfg.mqtt_port = atoi(bfr);
					}
				}
				if (httpdFindArg(c.getArgs, "MQTTuser", bfr, sizeof(bfr)) >= 0) {
					TESTP("MQTTuser=%s\n", bfr);
					if (0 <= strlen(bfr) && strlen(bfr) < 32) {
						strcpy(sysCfg.mqtt_user, bfr);
					}
				}
				if (httpdFindArg(c.getArgs, "MQTTpass", bfr, sizeof(bfr)) >= 0) {
					TESTP("MQTTpass=%s\n", bfr);
					if (0 <= strlen(bfr) && strlen(bfr) < 32) {
						strcpy(sysCfg.mqtt_pass, bfr);
					}
				}
				if (httpdFindArg(c.getArgs, "DevPrefix", bfr, sizeof(bfr)) >= 0) {
					TESTP("DevPrefix=%s\n", bfr);
					if (2 <= strlen(bfr) && strlen(bfr) <= 8) {
						strcpy(sysCfg.deviceID_prefix, bfr);
					}
				}
				if (httpdFindArg(c.getArgs, "reboot", bfr, sizeof(bfr)) >= 0) {
					TESTP("reboot=%s\n", bfr);
					if (strcmp(bfr, "yes") == 0) {
						reboot = true;
					}
				}
				CFG_Save();
			}
		}
		replyOK(conn);
	} else {
		replyFail(conn);
	}
}

void ICACHE_FLASH_ATTR tcp_connect_cb(void *arg) {
	struct espconn *conn = (struct espconn*) arg;
	espconn_regist_recvcb(conn, tcp_receive_cb);
	espconn_regist_sentcb(conn, tcp_sent_cb);
}

void ICACHE_FLASH_ATTR tcp_disconnect_cb(void *arg) {
	struct espconn *conn = (struct espconn*) arg;
	TESTP("Disconnected %s\n", reboot ? "rebooting" : "");
	ets_delay_us(5000);
	if (reboot) system_restart();
}

void ICACHE_FLASH_ATTR tcp_reconnect_cb(void *arg, sint8 err) {
	TESTP("Reconnected\n");
}

bool ICACHE_FLASH_ATTR tcp_listen(unsigned int port) {
	int ret;

	httpConn.type = ESPCONN_TCP;
	httpConn.state = ESPCONN_NONE;
	httpConn.proto.tcp = &httpTcp;
	httpConn.proto.tcp->local_port = port;

	espconn_regist_connectcb(&httpConn, tcp_connect_cb);
	espconn_regist_reconcb(&httpConn, tcp_reconnect_cb);
	espconn_regist_disconcb(&httpConn, tcp_disconnect_cb);

	ret = espconn_accept(&httpConn);
	espconn_regist_time(&httpConn, 15, 0); //timeout

	if (ret != ESPCONN_OK) {
		TESTP("Error when starting the listener: %d.\n", ret);
		ets_delay_us(2000);
		return false;
	}
	return true;
}
