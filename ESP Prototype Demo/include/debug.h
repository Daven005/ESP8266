/*
 * debug.h
 *
 *  Created on: Dec 4, 2014
 *      Author: Minh
 */

#ifndef USER_DEBUG_H_
#define USER_DEBUG_H_

#ifndef DEBUG
#define DEBUG 0
#endif
#ifndef INFO
#define INFO if (DEBUG>=1) os_printf
#endif
#ifndef INFO2
#define INFO2 if (DEBUG>=2) os_printf
#endif

#endif /* USER_DEBUG_H_ */
