/*
 * sysCfg.h
 *
 *  Created on: 9 Nov 2016
 *      Author: User
 */

#ifndef USER_INCLUDE_SYSCFG_H_
#define USER_INCLUDE_SYSCFG_H_
#include "user_conf.h"

typedef struct{
	uint32_t cfg_holder;
	uint8_t device_id[16];

	uint8_t sta_ssid[64];
	uint8_t sta_pwd[64];
	uint32_t sta_type;

	uint8_t mqtt_host[64];
	uint32_t mqtt_port;
	uint8_t mqtt_user[32];
	uint8_t mqtt_pass[32];
	uint32_t mqtt_keepalive;
	uint8_t security;

	uint16 updates;
	uint8 inputs;
	char deviceID_prefix[8];
	char deviceName[NAME_SIZE];
	char deviceLocation[NAME_SIZE];
	uint16_t settings[SETTINGS_SIZE];
	uint8_t mapping[MAP_TEMP_SIZE];
	uint8_t mappingName[MAP_TEMP_SIZE][NAME_SIZE];
	uint8 outputs;
	uint32 sumcheck;
} SYSCFG;

void initSysCfg(void);
extern SYSCFG sysCfg;

#endif /* USER_INCLUDE_SYSCFG_H_ */
