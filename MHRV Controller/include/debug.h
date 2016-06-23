/*
 * debug.h
 *
 *  Created on: Dec 4, 2014
 *      Author: Minh
 */

#ifndef USER_DEBUG_H_
#define USER_DEBUG_H_

#define DEBUG 1

#ifndef INFOP
#define INFOP if (DEBUG>=2) os_printf
#endif
#ifndef INFO
#define INFO(x) if (DEBUG>=2) do { x; } while (0)
#endif

#ifndef TESTP
#define TESTP if (DEBUG>=1) os_printf
#endif

#ifndef TEST
#define TEST(x) if (DEBUG>=1) do { x; } while (0)
#endif

#endif /* USER_DEBUG_H_ */
