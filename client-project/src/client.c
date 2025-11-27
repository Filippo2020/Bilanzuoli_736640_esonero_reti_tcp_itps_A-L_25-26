/*
 ============================================================================
 Name        : client.c
 Author      : Bilanzuoli Filippo
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */


// Forza funzioni moderne Winsock solo su Windows
#ifdef _WIN32
#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
#else
// Linux / Unix
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef int sock_t;
#define INVALID_SOCKET (-1)
#define CLOSESOCK close
#endif

#include "protocol.h"

/* helpers to receive/send exact bytes */
static int recv_all(sock_t s, void* buf, size_t n) {
    size_t got = 0;
    char* p = (char*)buf;
    while (got < n) {
#ifdef _WIN32
        int r = recv(s, p + got, (int)(n - got), 0);
#else
        ssize_t r = recv(s, p + got, n - got, 0);
#endif
        if (r <= 0) return 0;
        got += (size_t)r;
    }
    return 1;
}

static int send_all(sock_t s, const void* buf, size_t n) {
    size_t sent = 0;
    const char* p = (const char*)buf;
    while (sent < n) {
#ifdef _WIN32
        int r = send(s, p + sent, (int)(n - sent), 0);
#else
        ssize_t r = send(s, p + sent, n - sent, 0);
#endif
        if (r <= 0) return 0;
        sent += (size_t)r;
    }
    return 1;
}

static void usage(const char* prog) {
    fprintf(stderr, "Uso: %s [-s server] [-p port] -r \"type city\"\n", prog);
    exit(1);
}

/* Trim leading/trailing spaces */
static void trim_inplace(char* s) {
    char *p = s;
    while (*p == ' ') p++;
    if (p != s) memmove(s, p, strlen(p)+1);
    size_t len = strlen(s);
    while (len > 0 && s[len-1] == ' ') { s[len-1] = '\0'; len--; }
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
            have_request = 1;
            i++;
        } else {
            usage(argv[0]);
        }
    }

    if (!have_request) usage(argv[0]);

    trim_inplace(request_raw);
    if (strlen(request_raw) < 1) usage(argv[0]);

    char rtype = request_raw[0];
    char city[CITY_NAME_LEN];
    memset(city, 0, sizeof(city));
    const char* rest = request_raw + 1;
    while (*rest == ' ') rest++;
    if (*rest == '\0') {
        city[0] = '\0';
    } else {
        strncpy(city, rest, CITY_NAME_LEN-1);
        city[CITY_NAME_LEN-1] = '\0';
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "Errore inizializzazione Winsock\n");
        return 1;
    }
#endif

    struct addrinfo hints, *res, *p;
    memset(&hints,0,sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int gai = getaddrinfo(server, port, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "Errore risoluzione host: %s\n",
                gai == EAI_NONAME ? "Nome non trovato" :
                gai == EAI_AGAIN  ? "Temporary failure" :
                "Errore risoluzione");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    sock_t sock = INVALID_SOCKET;
    char ipstr[INET6_ADDRSTRLEN] = {0};

    for (p = res; p != NULL; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock == INVALID_SOCKET) continue;
        if (connect(sock, p->ai_addr, p->ai_addrlen) == 0) {
            void* addrptr = NULL;
            if (p->ai_family == AF_INET) {
                addrptr = &((struct sockaddr_in*)p->ai_addr)->sin_addr;
            } else {
                addrptr = &((struct sockaddr_in6*)p->ai_addr)->sin6_addr;
            }
            inet_ntop(p->ai_family, addrptr, ipstr, sizeof(ipstr));
            break;
        }
        CLOSESOCK(sock);
        sock = INVALID_SOCKET;
    }

    freeaddrinfo(res);

    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Errore connessione al server %s:%s\n", server, port);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    /* Build request: 1 byte type + 64 bytes city */
    unsigned char reqbuf[1 + CITY_NAME_LEN];
    memset(reqbuf, 0, sizeof(reqbuf));
    reqbuf[0] = (unsigned char)rtype;
    strncpy((char*)&reqbuf[1], city, CITY_NAME_LEN-1);

    if (!send_all(sock, reqbuf, sizeof(reqbuf))) {
        fprintf(stderr, "Errore invio richiesta\n");
        CLOSESOCK(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    /* Response: 4B status + 1B type + 4B float */
    unsigned char respbuf[9];
    if (!recv_all(sock, respbuf, sizeof(respbuf))) {
        fprintf(stderr, "Errore ricezione risposta\n");
        CLOSESOCK(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    CLOSESOCK(sock);

    uint32_t net_status;
    memcpy(&net_status, respbuf, 4);
    uint32_t status = ntohl(net_status);
    char resp_type = (char)respbuf[4];

    uint32_t net_value_u32;
    memcpy(&net_value_u32, &respbuf[5], 4);
    uint32_t value_u32 = ntohl(net_value_u32);
    float value;
    memcpy(&value, &value_u32, sizeof(value));

    char message[256] = {0};
    if (status == STATUS_SUCCESS) {
        switch (resp_type) {
            case 't':
                snprintf(message, sizeof(message), "%s: Temperatura = %.1f°C", city, value);
                break;
            case 'h':
                snprintf(message, sizeof(message), "%s: Umidità = %.1f%%", city, value);
                break;
            case 'w':
                snprintf(message, sizeof(message), "%s: Vento = %.1f km/h", city, value);
                break;
            case 'p':
                snprintf(message, sizeof(message), "%s: Pressione = %.1f hPa", city, value);
                break;
            default:
                snprintf(message, sizeof(message), "Richiesta non valida");
                break;
        }
    } else if (status == STATUS_CITY_UNAVAILABLE) {
        snprintf(message, sizeof(message), "Città non disponibile");
    } else {
        snprintf(message, sizeof(message), "Richiesta non valida");
    }

    printf("Ricevuto risultato dal server ip %s. %s\n",
           ipstr[0] ? ipstr : server, message);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
