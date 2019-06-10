#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_

#include "IOdefs.h"

#define CFG_HOLDER	0x00FF55A6	/* Change this value to load default configurations */
#define CFG_LOCATION	0x78	/* Please don't change or if you know what you doing */
#define CLIENT_SSL_ENABLE

/*DEFAULT CONFIGURATIONS*/

//#define STA_SSID "EE-berry"
//#include "password.h"
#define STA_TYPE AUTH_WPA2_PSK

#define MQTT_RECONNECT_TIMEOUT 	10	/*second*/
#define MQTT_BUF_SIZE 0
#define NAME_SIZE 0
#undef USE_WIFI
#define SETTINGS_SIZE 0

#undef USE_PRESSURE_SENSOR
#endif
