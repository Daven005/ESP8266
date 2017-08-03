#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_

#define CFG_HOLDER	0x00FF55A6	/* Change this value to load default configurations */
#ifdef ESP01
#define CFG_LOCATION	0x78	/* Please don't change or if you know what you doing */
#else
#define CFG_LOCATION	0x78	/* Please don't change or if you know what you doing */
#endif
// Default CONFIG is Not Sleep, No Outputs

#define SLEEP_MODE
#define USE_DECODE
#define USE_TIME
#define USE_I2C
#undef OUTPUTS
#undef INPUTS
#undef USE_OUTSIDE_TEMP

#include "IOdefs.h"

/*DEFAULT MQTT CONFIGURATIONS*/

#define MQTT_HOST			"192.168.1.100" //"broker.mqttdashboard.com"
#include "password.h"
#define MQTT_PORT			1883
#define MQTT_BUF_SIZE		512
#define MQTT_KEEPALIVE		20 * 60	 // 20 mins to allow for deep sleep

#define DEVICE_PREFIX		"Hollies"
#define MQTT_USER			""
#define MQTT_PASS			""

#define STA_SSID "EE-berry"
#define STA_TYPE AUTH_WPA2_PSK

#define MQTT_RECONNECT_TIMEOUT 	10	/*second*/

#define DEFAULT_SECURITY	0
#define QUEUE_BUFFER_SIZE	4096

#define PROTOCOL_NAMEv31	/*MQTT version 3.1 compatible with Mosquitto v0.15*/
//PROTOCOL_NAMEv311			/*MQTT version 3.11 compatible with https://eclipse.org/paho/clients/testing/*/

#define NAME_SIZE 32
#define MAP_TEMP_SIZE 4
#define MAP_TEMP_LAST 3

#define SETTINGS_SIZE 10

#define INPUT_SENSOR_ID_START 10
#define OUTPUT_SENSOR_ID_START 20
#define MAX_TEMPERATURE_SENSOR 0

#define SET_PULSES_PER_KNOT 0
#define SET_MONITOR_TIME 1
#define SET_GATHER_TIME 2
#define SET_CUT_IN 3
#define SET_REPORTING_COUNT 4
#define SET_VBAT_CAL_MULT 5
#define SET_VBAT_CAL_DIV 6

#define DEFAULT_PULSES_PER_KNOT 10
#define DEFAULT_MONITOR_TIME 30
#define DEFAULT_GATHER_TIME 5
#define DEFAULT_CUT_IN 5
#define DEFAULT_REPORTING_COUNT 50
#define DEFAULT_VBAT_CAL_MULT 400
#define DEFAULT_VBAT_CAL_DIV 866

#define SET_MINIMUM 0
#define SET_MAXIMUM 1000

// Derived temperatures

#define UPDATES 60

#endif
