/*
 * decodeMessage.h
 *
 *  Created on: 18 Jul 2016
 *      Author: User
 */

#ifndef USER_INCLUDE_DECODEMESSAGE_H_
#define USER_INCLUDE_DECODEMESSAGE_H_

typedef void (*extraDecode)(char *s);

void decodeMessage(MQTT_Client* client, char* topic, char* data);
void setExtraDecode(extraDecode f);

#endif /* USER_INCLUDE_DECODEMESSAGE_H_ */
