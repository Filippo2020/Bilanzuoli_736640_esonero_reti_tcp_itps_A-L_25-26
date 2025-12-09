#if defined(_WIN32)
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600
    #endif
#endif

#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#if defined _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define close closesocket
    #define strcasecmp _stricmp
    typedef int socklen_t;
    static void cleanup_winsock(void) { WSACleanup(); }
#else
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <strings.h>
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

#define DEFAULT_PORT 56700
#define CITY_LEN 64

typedef struct {
    char type;
    char city[CITY_LEN];
} weather_request_t;

typedef struct {
    unsigned int status;
    char type;
    float value;
} weather_response_t;

#endif


