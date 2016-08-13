#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_

#define CFG_HOLDER	0x00FF55A3	/* Change this value to load default configurations */
#define CFG_LOCATION	0x78	/* Please don't change or if you know what you doing */
#define CLIENT_SSL_ENABLE

/*DEFAULT CONFIGURATIONS*/
#include"IOdefs.h"
#include "password.h"

#define SLEEP_MODE 0
#undef USE_TEMPERATURE
#define USE_WIFI_SCAN

#define RF433_TX 4
#define RF433_RX 13

#define LED 5
#ifdef LED
#define SWITCH 0 			// Switch requires LED (for feedback)
#ifdef SWITCH
#define USE_SMART_CONFIG 	// Requires Switch && LED
#define USE_WEB_CONFIG		// Requires Switch && LED
#endif
#endif

#define MAX_TEMPERATURE_SENSOR 0

#define MQTT_HOST			"192.168.1.100" //"broker.mqttdashboard.com"
#define MQTT_PORT			1883
#define MQTT_BUF_SIZE		512
#define MQTT_KEEPALIVE		120	 /*second*/

#define MQTT_CLIENT_ID		"Hollies%lx"
#define MQTT_USER			""
#define MQTT_PASS			""

#define STA_SSID "EE-berry"
#define STA_TYPE AUTH_WPA2_PSK

#define MQTT_RECONNECT_TIMEOUT 	10	/*second*/

#define DEFAULT_SECURITY	0
#define QUEUE_BUFFER_SIZE		 		2048

#define PROTOCOL_NAMEv31	/*MQTT version 3.1 compatible with Mosquitto v0.15*/
//PROTOCOL_NAMEv311			/*MQTT version 3.11 compatible with https://eclipse.org/paho/clients/testing/*/

#define NAME_SIZE 32
#define MAP_SIZE 1
#define MAP_TEMP_SIZE 1
#define SETTINGS_SIZE 10
#define MAX_OUTPUT 20

#define SET_REPEAT_A 0
#define SET_REPEAT_B 1

#define DEFAULT_REPEAT_A 30
#define DEFAULT_REPEAT_B 30

#define SET_MINIMUM 0
#define SET_MAXIMUM 95

#define UPDATES 60

typedef enum {TYPE_A, TYPE_B} xmitType;

#endif
