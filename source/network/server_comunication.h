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

bool debug_init(int a, int b, int c, int d);
int debug_send(void *data);
int debug_close(void);

#endif