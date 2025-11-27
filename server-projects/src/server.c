/* server.c
   TCP server that implements the weather service per specification.
*/

#define _WIN32_WINNT 0x0601


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "protocol.h"

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  typedef SOCKET sock_t;
  #define CLOSESOCK(s) closesocket(s)
#else
  #include <unistd.h>
  #include <errno.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netdb.h>
  #include <arpa/inet.h>
  #include <netinet/in.h>
  typedef int sock_t;
  #define INVALID_SOCKET (-1)
  #define CLOSESOCK(s) close(s)
#endif


float get_temperature(void);
float get_humidity(void);
float get_wind(void);
float get_pressure(void);


static int ci_equal(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) {
        char ca = *a; char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = ca - 'A' + 'a';
        if (cb >= 'A' && cb <= 'Z') cb = cb - 'A' + 'a';
        if (ca != cb) return 0;
    }
    return *a == *b;
}


static void trim_inplace(char* s) {
    char *p = s;
    while (*p == ' ') p++;
    if (p != s) memmove(s, p, strlen(p)+1);
    size_t len = strlen(s);
    while (len > 0 && s[len-1] == ' ') { s[len-1] = '\0'; len--; }
}


static const char* supported_cities[] = {
    "Bari","Roma","Milano","Napoli","Torino","Palermo","Genova","Bologna","Firenze","Venezia"
};
static const size_t supported_cities_count = sizeof(supported_cities)/sizeof(supported_cities[0]);

/* receive exactly n bytes */
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

/* send exactly n bytes */
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

#ifdef _WIN32

const char* inet_ntop_win(int af, const void* src, char* dst, size_t size) {
    if (af == AF_INET) {
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        memcpy(&sa.sin_addr, src, sizeof(struct in_addr));
        DWORD sz = (DWORD)size;
        if (WSAAddressToStringA((LPSOCKADDR)&sa, sizeof(sa), NULL, dst, &sz) != 0)
            return NULL;
    } else if (af == AF_INET6) {
        struct sockaddr_in6 sa6;
        memset(&sa6, 0, sizeof(sa6));
        sa6.sin6_family = AF_INET6;
        memcpy(&sa6.sin6_addr, src, sizeof(struct in6_addr));
        DWORD sz = (DWORD)size;
        if (WSAAddressToStringA((LPSOCKADDR)&sa6, sizeof(sa6), NULL, dst, &sz) != 0)
            return NULL;
    }
    return dst;
}
#define inet_ntop inet_ntop_win
#endif

int main(int argc, char* argv[]) {
    const char* port = DEFAULT_PORT;

    for (int i=1;i<argc;i++){
        if (strcmp(argv[i], "-p")==0 && i+1<argc) {
            port = argv[i+1];
            i++;
        } else {
            fprintf(stderr, "Uso: %s [-p port]\n", argv[0]);
            return 1;
        }
    }

    #ifdef _WIN32
      WSADATA wsa;
      if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
          fprintf(stderr, "WSAStartup failed\n");
          return 1;
      }
    #endif

    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints,0,sizeof(hints));
    hints.ai_family = AF_INET; /* IPv4 */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, port, &hints, &res) != 0) {
        perror("getaddrinfo");
        #ifdef _WIN32
          WSACleanup();
        #endif
        return 1;
    }

    sock_t listen_sock = INVALID_SOCKET;
    struct addrinfo *p;
    for (p = res; p != NULL; p = p->ai_next) {
        listen_sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listen_sock == INVALID_SOCKET) continue;
        int yes = 1;
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
        if (bind(listen_sock, p->ai_addr, (int)p->ai_addrlen) == 0) break;
        CLOSESOCK(listen_sock);
        listen_sock = INVALID_SOCKET;
    }
    freeaddrinfo(res);

    if (listen_sock == INVALID_SOCKET) {
        fprintf(stderr, "Impossibile bind sulla porta %s\n", port);
        #ifdef _WIN32
          WSACleanup();
        #endif
        return 1;
    }

    if (listen(listen_sock, BACKLOG) != 0) {
        perror("listen");
        CLOSESOCK(listen_sock);
        #ifdef _WIN32
          WSACleanup();
        #endif
        return 1;
    }

    srand((unsigned int)time(NULL));

    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        sock_t client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &addrlen);
        if (client_sock == INVALID_SOCKET) continue;

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));


        unsigned char reqbuf[1 + CITY_NAME_LEN];
        if (!recv_all(client_sock, reqbuf, sizeof(reqbuf))) {
            CLOSESOCK(client_sock);
            continue;
        }
        char rtype = (char)reqbuf[0];
        char citybuf[CITY_NAME_LEN+1];
        memcpy(citybuf, &reqbuf[1], CITY_NAME_LEN);
        citybuf[CITY_NAME_LEN] = '\0';
        citybuf[CITY_NAME_LEN-1] = citybuf[CITY_NAME_LEN-1] ? citybuf[CITY_NAME_LEN-1] : '\0';
        trim_inplace(citybuf);

        printf("Richiesta '%c %s' dal client ip %s\n", rtype, citybuf, client_ip);
        fflush(stdout);


        int type_ok = (rtype=='t' || rtype=='h' || rtype=='w' || rtype=='p');


        int city_ok = 0;
        for (size_t i=0;i<supported_cities_count;i++) {
            if (ci_equal(citybuf, supported_cities[i])) { city_ok = 1; break; }
        }

        uint32_t status;
        char resp_type = '\0';
        float value = 0.0f;

        if (!type_ok) {
            status = STATUS_INVALID_REQUEST;
        } else if (!city_ok) {
            status = STATUS_CITY_UNAVAILABLE;
        } else {
            status = STATUS_SUCCESS;
            resp_type = rtype;
            switch (rtype) {
                case 't': value = get_temperature(); break;
                case 'h': value = get_humidity(); break;
                case 'w': value = get_wind(); break;
                case 'p': value = get_pressure(); break;
                default: value = 0.0f; break;
            }
        }


        unsigned char respbuf[9];
        uint32_t net_status = htonl(status);
        memcpy(respbuf, &net_status, 4);
        respbuf[4] = resp_type;

        uint32_t value_u32;
        memcpy(&value_u32, &value, sizeof(value_u32));
        value_u32 = htonl(value_u32);
        memcpy(&respbuf[5], &value_u32, 4);

        send_all(client_sock, respbuf, sizeof(respbuf));
        CLOSESOCK(client_sock);
    }


    CLOSESOCK(listen_sock);
    #ifdef _WIN32
      WSACleanup();
    #endif
    return 0;
}


float rand_float_range(float lo, float hi) {
    float r = (float)rand() / (float)RAND_MAX;
    return lo + r * (hi - lo);
}

float get_temperature(void) { return rand_float_range(-10.0f, 40.0f); }
float get_humidity(void)    { return rand_float_range(20.0f, 100.0f); }
float get_wind(void)        { return rand_float_range(0.0f, 100.0f); }
float get_pressure(void)    { return rand_float_range(950.0f, 1050.0f); }

