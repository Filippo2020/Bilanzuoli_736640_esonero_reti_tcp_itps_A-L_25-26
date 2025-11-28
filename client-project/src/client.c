/*
 ============================================================================
 Name        : client.c
 Author      : Bilanzuoli Filippo
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
typedef int sock_t;
#define INVALID_SOCKET (-1)
#define CLOSESOCK close
#endif

#include "protocol.h"

static int recv_all(sock_t s, void* buf, size_t n) {
    size_t got = 0;
    char* p = (char*)buf;
    while (got < n) {
        int r = recv(s, p + got, (int)(n - got), 0);
        if (r <= 0) return 0;
        got += (size_t)r;
    }
    return 1;
}

static int send_all(sock_t s, const void* buf, size_t n) {
    size_t sent = 0;
    const char* p = (const char*)buf;
    while (sent < n) {
        int r = send(s, p + sent, (int)(n - sent), 0);
        if (r <= 0) return 0;
        sent += (size_t)r;
    }
    return 1;
}

static void usage(const char* prog) {
    fprintf(stderr, "Uso: %s [-s server] [-p port] -r \"type city\"\n", prog);
    exit(1);
}

static void trim_inplace(char* s) {
    char *p = s;
    while (*p == ' ') p++;
    if (p != s) memmove(s, p, strlen(p)+1);
    size_t len = strlen(s);
    while (len > 0 && s[len-1] == ' ') { s[len-1] = '\0'; len--; }
}

static void normalize_city(char* city) {
    if (city[0] >= 'a' && city[0] <= 'z') city[0] -= 32;
    for (int i = 1; city[i]; i++) {
        if (city[i] >= 'A' && city[i] <= 'Z') city[i] += 32;
    }
}

int main(int argc, char* argv[]) {

    const char* server = DEFAULT_SERVER;
    const char* port = DEFAULT_PORT;
    char request_raw[1024] = {0};
    int have_request = 0;

    for (int i=1;i<argc;i++) {
        if (strcmp(argv[i], "-s")==0 && i+1<argc) {
            server = argv[i+1]; i++;
        } else if (strcmp(argv[i], "-p")==0 && i+1<argc) {
            port = argv[i+1]; i++;
        } else if (strcmp(argv[i], "-r")==0 && i+1<argc) {
            strncpy(request_raw, argv[i+1], sizeof(request_raw)-1);
            request_raw[sizeof(request_raw)-1] = '\0';
            have_request = 1;
            i++;
        } else {
            usage(argv[0]);
        }
    }

    if (!have_request) usage(argv[0]);

    trim_inplace(request_raw);
    if (strlen(request_raw) < 1) usage(argv[0]);

    /* tipo richiesta */
    char rtype = request_raw[0];

    char city[CITY_NAME_LEN];
    memset(city, 0, sizeof(city));

    const char* rest = request_raw + 1;
    while (*rest == ' ') rest++;
    strncpy(city, rest, CITY_NAME_LEN - 1);

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) return 1;
#endif
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)atoi(port));
    sa.sin_addr.s_addr = inet_addr(server);

    if (sa.sin_addr.s_addr == INADDR_NONE) {
        fprintf(stderr, "Errore: indirizzo server non valido\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    /* --- CREO SOCKET --- */
    sock_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Errore: impossibile creare socket\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    /* --- CONNESSIONE --- */
    if (connect(sock, (struct sockaddr*)&sa, sizeof(sa)) != 0) {
        fprintf(stderr, "Errore: connessione fallita\n");
        CLOSESOCK(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    /* --- PREPARO BUFFER RICHIESTA --- */
    unsigned char reqbuf[1 + CITY_NAME_LEN] = {0};
    reqbuf[0] = (unsigned char)rtype;
    strncpy((char*)&reqbuf[1], city, CITY_NAME_LEN - 1);

    /* --- INVIO --- */
    if (!send_all(sock, reqbuf, sizeof(reqbuf))) {
        CLOSESOCK(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    /* --- RICEZIONE RISPOSTA DAL SERVER --- */
    unsigned char respbuf[9];
    if (!recv_all(sock, respbuf, sizeof(respbuf))) {
        CLOSESOCK(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    CLOSESOCK(sock);

    /* --- PARSING RISPOSTA --- */

    uint32_t net_status, net_value_u32;
    memcpy(&net_status, respbuf, 4);
    memcpy(&net_value_u32, &respbuf[5], 4);

    uint32_t status = ntohl(net_status);
    uint32_t value_u32 = ntohl(net_value_u32);

    float value;
    memcpy(&value, &value_u32, sizeof(value));

    char resp_type = (char)respbuf[4];

    normalize_city(city);

    /* --- OUTPUT --- */

    if (status == STATUS_SUCCESS) {
        switch (resp_type) {
            case 't': printf("Temperatura = %.1fC\n", value); break;
            case 'h': printf("Umidità = %.1f%%\n", value); break;
            case 'w': printf("Vento = %.1f km/h\n", value); break;
            case 'p': printf("Pressione = %.1f hPa\n", value); break;
            default:  printf("Richiesta non valida\n"); break;
        }
    } else if (status == STATUS_CITY_UNAVAILABLE) {
        printf("Città non disponibile\n");
    } else {
        printf("Richiesta non valida\n");
    }

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
