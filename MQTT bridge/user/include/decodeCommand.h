/*
 * decodeCommand.h
 *
 *  Created on: 2 Aug 2016
 *      Author: User
 */

#ifndef USER_DECODECOMMAND_H_
#define USER_DECODECOMMAND_H_


typedef struct {
	bool isWiFi;
	bool isInit;
	bool isClient;
	bool isLWT;
	bool isConnected;
} myMqttState_t;

void decodeCommand(char *bfr);
void mqttStatus(char *cmd);
void okMessage(char *cmd);
void errorMessage(char *cmd, char *msg);

#endif /* USER_DECODECOMMAND_H_ */
