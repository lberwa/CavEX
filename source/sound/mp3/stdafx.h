#ifndef __STDAFX_H_
#define __STDAFX_H_

/* Minimaler Ersatz für den WiiXplorer stdafx.h — nur Audio-relevante Includes. */

#include <gccore.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#ifdef __cplusplus
#include <string>
#include <vector>
#include "Settings.h"
#endif

#define UNUSED __attribute__((unused))

#endif /* __STDAFX_H_ */
