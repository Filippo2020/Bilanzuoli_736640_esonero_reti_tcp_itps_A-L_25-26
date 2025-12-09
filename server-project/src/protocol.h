#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#if defined(_WIN32)
    // Assicurati che Windows supporti inet_ntop e altre funzioni
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600
    #endif

    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define close closesocket
    #define strcasecmp _stricmp
    typedef int socklen_t;

    // Funzione di pulizia Winsock
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

// Configurazioni generali
#define DEFAULT_PORT 56700
#define CITY_LEN 64

// Struttura richiesta meteo
typedef struct {
    char type;              // 't' = temperatura, 'h' = umidità, 'w' = vento, 'p' = pressione
    char city[CITY_LEN];    // Nome città
} weather_request_t;

// Struttura risposta meteo
typedef struct {
    uint32_t status;        // 0 = OK, 1 = città non disponibile, 2 = richiesta non valida
    char type;              // stesso tipo della richiesta
    float value;            // valore associato
} weather_response_t;

#endif // PROTOCOL_H_
