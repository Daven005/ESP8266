#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_

#define LED 5

enum backgroundEvent_t {
	EVENT_MQTT_CONNECTED = 1,
	EVENT_RX,
	EVENT_RX_OVERFLOW
};

/*DEFAULT MQTT CONFIGURATIONS*/

#define MQTT_HOST			"192.168.1.100" //"broker.mqttdashboard.com"
#define MQTT_PORT			1883
#define MQTT_BUF_SIZE		512
#define MQTT_KEEPALIVE		20 * 60	 // 20 mins to allow for deep sleep

#define MQTT_RECONNECT_TIMEOUT 	10	/*second*/

#define DEFAULT_SECURITY	0
#define QUEUE_BUFFER_SIZE	4096

#define PROTOCOL_NAMEv31	/*MQTT version 3.1 compatible with Mosquitto v0.15*/
//PROTOCOL_NAMEv311			/*MQTT version 3.11 compatible with https://eclipse.org/paho/clients/testing/*/

#endif
