#ifndef __TP_DEF_H
#define __TP_DEF_H

#define LINUX

#ifdef LINUX
	#include "TPLinuxDef.h"
#elif defined(WIN32)
	#include "TPWinDef.h"
#endif

#if defined(WIN32)
	#ifndef LITTLE_ENDIAN
		#define LITTLE_ENDIAN 1234
		#define BIG_ENDIAN 4321
		#define BYTE_ORDER LITTLE_ENDIAN 
	#endif
#endif

#ifdef LINUX
#include <endian.h>
#endif

#endif /* __TP_DEF_H */
