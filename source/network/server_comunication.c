#include <network.h>
#include "server_comunication.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#define SERVER_PORT 12345

static s32 sock = -1;

bool server_init(int a, int b, int c, int d) {

    sock = net_socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0) {
        return false;
    }


    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = SERVER_PORT; // network.h nimmt Port direkt als int
    IP4_ADDR(&server.sin_addr, a,b,c,d);

    if(net_connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        net_close(sock);
        return false;
    }
    return true;
}

int server_send(void *data, size_t size) {
    int send = net_send(sock, data, size, 0);
    return send;
}

#define BUF_SIZE 512
static u8 recv_buffer[BUF_SIZE];

u8* server_receive(int* out_len) {
    int bytes_received = net_recv(sock, recv_buffer, BUF_SIZE, 0);

    if(bytes_received > 0) {
        *out_len = bytes_received;
        return recv_buffer;  // gültig, weil static
    }

    *out_len = 0;
    return NULL;
}

int server_close() {
    int back = net_close(sock);
    return back;
}

char* server_get_mac_address() {
    u8 mac[6];
    s32 res = net_get_mac_address(mac);

    if(res < 0) {
        return NULL; // Fehler beim Auslesen
    }

    // Speicher für MAC-String reservieren: "XX:XX:XX:XX:XX:XX\0" -> 17 Zeichen
    char* mac_str = malloc(18);
    if(!mac_str) return NULL;

    // MAC in String umwandeln
    sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return mac_str;
}

//-----------------
//debugging
//-----------------

#define DEBUG_SERVER_PORT 12344

static s32 dsock = -1;
bool debug_init(int a, int b, int c, int d) {

    dsock = net_socket(AF_INET, SOCK_STREAM, 0);
    if(dsock < 0) {
        return false;
    }


    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = DEBUG_SERVER_PORT; // network.h nimmt Port direkt als int
    IP4_ADDR(&server.sin_addr, a,b,c,d);

    if(net_connect(dsock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        net_close(dsock);
        return false;
    }
    return true;
}

int debug_send(void *data) {
    int send = net_send(dsock, data, strlen(data), 0);
    return send;
}

int debug_close() {
    int back = net_close(dsock);
    return back;
}