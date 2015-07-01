/*
 * wifi.h
 *
 *  Created on: Dec 30, 2014
 *      Author: Minh
 */

#ifndef USER_WIFI_H_
#define USER_WIFI_H_
#include "os_type.h"
typedef void (*WifiCallback)(uint8_t);
void ICACHE_FLASH_ATTR WIFI_Connect(uint8_t* ssid, uint8_t* pass, WifiCallback cb);
void ICACHE_FLASH_ATTR WIFI_Connect2(uint8_t* ssid1, uint8_t* pass1, uint8_t* ssid2, uint8_t* pass2, WifiCallback cb);


#endif /* USER_WIFI_H_ */
