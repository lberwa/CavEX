#define NETWORK_H22
#include <network.h>
#include "server_comunication.h"
#include "../game/game_state.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include <gccore.h>
#include <sys/dir.h>  // für DIR, opendir, closedir

#include <sys/stat.h>
#include <zip.h>
#include <fat.h>

#define ZIP_PATH    "/test.zip"
#define SERVER_HOST "example.com"
#define SERVER_PORT 12345
#define SERVER_PORT_ZIP 12346

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

#define BUF_SIZE 20456
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
/*
bool server_data_init() {
    const char *tmp_path = config_read_string(&gstate.config_user, "paths.tmp", "tmp");

    DIR *dir = opendir(tmp_path);
    if (!dir) {
        debug_send("Ordner existiert nicht, erstelle...\n");
        if (!(mkdir(tmp_path, 0) == 0)) {
            return false;
        }
    }
    closedir(dir);

    s32 sock;
    struct sockaddr_in server2;
    char buffer[1024];

    // Socket erstellen
    sock = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        debug_send("Fehler beim Erstellen des Sockets");
        return 1;
    }
    
    // Server-Adresse
    memset(&server2, 0, sizeof(server2));
    server2.sin_family = AF_INET;
    server2.sin_port = htons(SERVER_PORT_ZIP);
    IP4_ADDR(&server2.sin_addr, 192, 168, 15, 152);

    debug_send("Connecting...\n");
    debug_send("Verbinde mit Server...");
    if (net_connect(sock, (struct sockaddr*)&server2, sizeof(server2)) < 0) {
        debug_send("Verbindung fehlgeschlagen");
        net_close(sock);
        return 1;
    }
    debug_send("Verbunden!");

    debug_send("Sending HTTP request...\n");
    char request[256];
    snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        ZIP_PATH, SERVER_HOST
    );

    net_send(sock, request, strlen(request), 0);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        debug_send("file open failed\n");
        close(sock);
        return 0;
    }

    char buf[2048];
    int r;
    int header_done = 0;

    while ((r = net_recv(sock, buf, sizeof(buf), 0)) > 0) {
        char *dataz = buf;
        int len = r;

        if (!header_done) {
            char *p = strstr(buf, "\r\n\r\n");
            if (!p) {
                continue;
            }
            header_done = 1;
            p += 4;
            dataz = p;
            len = r - (p - buf);
            debug_send("HTTP header skipped\n");
        }

        fwrite(dataz, 1, len, f);
    }

    fclose(f);
    close(sock);

    debug_send("Download finished\n Saved to: ");
    debug_send(tmp_path);

    struct zip *z = zip_open(tmp_path, 0, NULL);
    if (!z) { debug_send("zip_open failed\n"); return 1; }

    zip_int64_t n = zip_get_num_entries(z, 0);
    for (zip_uint64_t i = 0; i < n; i++) {
        const char *name = zip_get_name(z, i, 0);
        struct zip_file *zf = zip_fopen_index(z, i, 0);

        char outpath[256];
        snprintf(outpath, sizeof(outpath), "sd:/%s", name);
        FILE *out = fopen(outpath, "wb");

        char buf[1024];
        zip_int64_t r;
        while ((r = zip_fread(zf, buf, sizeof(buf))) > 0) {
            fwrite(buf, 1, r, out);
        }

        fclose(out);
        zip_fclose(zf);
    }

    zip_close(z);
}
*/
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