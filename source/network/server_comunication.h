#ifndef SERVER_COMUNICATION_H
#define SERVER_COMUNICATION_H

#include <stdbool.h>

bool server_init(int a, int b, int c, int d);
int server_send(void *data, size_t size);
u8* server_receive(int* out_len);
int server_close(void);
char* server_get_mac_address(void);

#endif