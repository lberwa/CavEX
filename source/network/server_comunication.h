/*
	Copyright (c) 2022 ByteBit/xtreme8000

	This file is part of CavEX.

	CavEX is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	CavEX is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with CavEX.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SERVER_COMUNICATION_H
#define SERVER_COMUNICATION_H

#include <stdbool.h>

#ifdef PLATFORM_PC
#include <stdint.h>

typedef uint8_t u8;
#endif

bool server_init(int a, int b, int c, int d);
int server_send(void *data, size_t size);
u8* server_receive(int* out_len);
int server_close(void);
char* server_get_mac_address(void);

/* Netzwerk-Remote-Debug (sendet Log-Strings an einen Dev-Rechner).
 * Standardmaessig AUS: debug_init macht einen blockierenden net_connect zu
 * einer fest verdrahteten IP und friert sonst jede Wii ein, die kein
 * lauschendes Dev-Terminal im Netzwerk hat. Mit -DNET_DEBUG aktivierbar. */
#ifdef NET_DEBUG
bool debug_init(int a, int b, int c, int d);
int debug_send(void *data);
int debug_close(void);
#else
static inline bool debug_init(int a, int b, int c, int d) {
	(void)a; (void)b; (void)c; (void)d;
	return false;
}
static inline int debug_send(void *data) {
	(void)data;
	return 0;
}
static inline int debug_close(void) {
	return 0;
}
#endif

#endif
