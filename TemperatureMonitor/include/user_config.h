#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_

#define CFG_HOLDER	0x00FF55A2	/* Change this value to load default configurations */
#define CFG_LOCATION	0x3C	/* Please don't change or if you know what you doing */
#define CLIENT_SSL_ENABLE

/*DEFAULT CONFIGURATIONS*/

#define MQTT_HOST			"192.168.1.100" //"broker.mqttdashboard.com"
#define MQTT_PORT			1883
#define MQTT_BUF_SIZE		512
#define MQTT_KEEPALIVE		120	 /*second*/

#define MQTT_CLIENT_ID		"Hollies3"
#define MQTT_USER			""
#define MQTT_PASS			""

#define STA_SSID "EE-berry"
#define STA_PASS "fog-phone-deep"
#define STA_TYPE AUTH_WPA2_PSK

#define MQTT_RECONNECT_TIMEOUT 	10	/*second*/

#define DEFAULT_SECURITY	0
#define QUEUE_BUFFER_SIZE		 		2048

#define PROTOCOL_NAMEv31	/*MQTT version 3.1 compatible with Mosquitto v0.15*/
//PROTOCOL_NAMEv311			/*MQTT version 3.11 compatible with https://eclipse.org/paho/clients/testing/*/

#define MAP_TS_TOP 0
#define MAP_TS_MIDDLE 1
#define MAP_TS_BOTTOM 2
#define MAP_WOODBURNER 3
#define MAP_SIZE 4

#define MAP_STR_TS_TOP "Monitor1"
#define MAP_STR_TS_MIDDLE "Monitor2"
#define MAP_STR_TS_BOTTOM "Monitor3"
#define MAP_STR_WOODBURNER "Monitor4"

#define SET_DHW_TEMP 0
#define SET_DHW_HYSTERESIS 1
#define SET_DHW_MIN 2
#define SETTINGS_SIZE 3

#define SET_STR_DHW_TEMP "DHW_TargetTemp"
#define SET_STR_DHW_HYSTERESIS "DHW_Hysteresis"
#define SET_STR_DHW_MIN "DHW_Minimum"

#endif
