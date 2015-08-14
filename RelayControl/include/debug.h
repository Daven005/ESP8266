/*
 * debug.h
 *
 *  Created on: Dec 4, 2014
 *      Author: Minh
 */

#ifndef USER_DEBUG_H_
#define USER_DEBUG_H_

#define DEBUG 0

#ifndef INFO
#define INFO if (DEBUG>=1) os_printf
#endif

#define INFO2 if (DEBUG>=2) os_printf

#endif /* USER_DEBUG_H_ */
