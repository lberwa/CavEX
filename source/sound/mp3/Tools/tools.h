/***************************************************************************
 * Copyright (C) 2010
 * by Dimok
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any
 * damages arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any
 * purpose, including commercial applications, and to alter it and
 * redistribute it freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you
 * must not claim that you wrote the original software. If you use
 * this software in a product, an acknowledgment in the product
 * documentation would be appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and
 * must not be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 * distribution.
 *
 * for WiiXplorer 2010
 ***************************************************************************/
#ifndef __TOOLS_H
#define __TOOLS_H

#ifdef __cplusplus
extern "C" {
#endif

#define KBSIZE		  1024.0f
#define MBSIZE		  1048576.0f
#define GBSIZE		  1073741824.0f

#define le16(i) ((((u16) ((i) & 0xFF)) << 8) | ((u16) (((i) & 0xFF00) >> 8)))
#define le32(i) ((((u32)le16((i) & 0xFFFF)) << 16) | ((u32)le16(((i) & 0xFFFF0000) >> 16)))
#define le64(i) ((((u64)le32((i) & 0xFFFFFFFFLL)) << 32) | ((u64)le32(((i) & 0xFFFFFFFF00000000LL) >> 32)))

#define GXCOLORTORGBA(x) ((u32) (x.r << 24 | x.g << 16 | x.b << 8 | x.a))
#define RGBATOGXCOLOR(x) ((GXColor) {(x & 0xFF000000) >> 24, (x & 0x00FF0000) >> 16, (x & 0x0000FF00) >> 8, (x & 0x000000FF)})

#define LIMIT(x, min, max)																	\
	({																						\
		typeof( x ) _x = x;																	\
		typeof( min ) _min = min;															\
		typeof( max ) _max = max;															\
		( ( ( _x ) < ( _min ) ) ? ( _min ) : ( ( _x ) > ( _max ) ) ? (( _min ) > ( _max ) ? ( _min ) : ( _max )) : ( _x ) );	\
	})

#define ALIGN(x) (((x) + 3) & ~3)
#define ALIGN32(x) (((x) + 31) & ~31)

#define coordsRGBA8(x, y, w) (((((y >> 2) * (w >> 2) + (x >> 2)) << 5) + ((y & 3) << 2) + (x & 3)) << 1)
#define datasizeRGBA8(w, h) ALIGN32(((w+3)>>2)*((h+3)>>2)*32*2)

void ShowError(const char * format, ...);
void ShowMsg(const char * title, const char * format, ...);
void ThrowMsg(const char * title, const char * format, ...);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif /* SVNREV_H */
