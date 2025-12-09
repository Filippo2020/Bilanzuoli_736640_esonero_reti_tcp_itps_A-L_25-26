/*
 ============================================================================
 Name        : client-project.c
 Author      : Bilanzuoli Filippo
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */


#include "protocol.h"

#if defined(_WIN32)
#ifndef inet_ntop
// Wrapper inet_ntop per Windows MinGW
const char* inet_ntop(int af, const void* src, char* dst, socklen_t size) {
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = af;
    memcpy(&sa.sin_addr, src, sizeof(struct in_addr));
    DWORD dst_len = (DWORD)size;
    if (WSAAddressToStringA((struct sockaddr*)&sa, sizeof(sa), NULL, dst, &dst_len) != 0) {
        return NULL;
    }
    return dst;
}
#endif
#endif

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);

    char server_host[256] = "localhost";
    int port = DEFAULT_PORT;
    weather_request_t req;
    memset(&req, 0, sizeof(req));
    int request_provided = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            strncpy(server_host, argv[i + 1], 255);
            server_host[255] = '\0';
            i++;
        } else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            char* input_str = argv[i + 1];
            char* space_ptr = strchr(input_str, ' ');
            if (!space_ptr) { printf("Errore: formato richiesta invalido.\n"); return -1; }
            int type_len = space_ptr - input_str;
            if (type_len != 1) { printf("Errore: il tipo deve essere un singolo carattere.\n"); return -1; }
            req.type = input_str[0];
            char* city_ptr = space_ptr + 1;
            if (strlen(city_ptr) >= CITY_LEN) { printf("Errore: nome città troppo lungo.\n"); return -1; }
            strncpy(req.city, city_ptr, CITY_LEN - 1);
            req.city[CITY_LEN - 1] = '\0';
            request_provided = 1;
            i++;
        }
    }

    if (!request_provided) {
        printf("Uso: %s [-s server] [-p port] -r \"type city\"\n", argv[0]);
        return -1;
    }

#if defined(_WIN32)
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) return -1;
#endif

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    char port_str[10];
    sprintf(port_str, "%d", port);

    if (getaddrinfo(server_host, port_str, &hints, &res) != 0) {
        printf("Errore risoluzione DNS per %s\n", server_host);
#if defined(_WIN32)
        WSACleanup();
#endif
        return -1;
    }

    char resolved_ip[INET_ADDRSTRLEN];
    char resolved_name[NI_MAXHOST];
    struct sockaddr_in* saddr = (struct sockaddr_in*)res->ai_addr;

    if (!inet_ntop(AF_INET, &(saddr->sin_addr), resolved_ip, INET_ADDRSTRLEN))
        strcpy(resolved_ip, "IP non risolto");

    if (getnameinfo((struct sockaddr*)saddr, sizeof(struct sockaddr_in),
                    resolved_name, sizeof(resolved_name), NULL, 0, 0) != 0) {
        strcpy(resolved_name, resolved_ip);
    }

#if defined(_WIN32)
    SOCKET client_socket = socket(PF_INET, SOCK_DGRAM, 0);
    if (client_socket == INVALID_SOCKET) { perror("Socket error"); freeaddrinfo(res); WSACleanup(); return -1; }
#else
    int client_socket = socket(PF_INET, SOCK_DGRAM, 0);
    if (client_socket < 0) { perror("Socket error"); freeaddrinfo(res); return -1; }
#endif

    char send_buffer[sizeof(char) + CITY_LEN];
    int offset = 0;
    memcpy(send_buffer + offset, &req.type, sizeof(char)); offset += sizeof(char);
    memcpy(send_buffer + offset, req.city, CITY_LEN); offset += CITY_LEN;

    ssize_t sent_bytes = sendto(client_socket, send_buffer, offset, 0, res->ai_addr, res->ai_addrlen);
    if (sent_bytes != offset) {
        printf("Errore invio.\n");
#if defined(_WIN32)
        closesocket(client_socket); WSACleanup();
#else
        close(client_socket);
#endif
        freeaddrinfo(res);
        return -1;
    }

    char recv_buffer[512];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    ssize_t received_bytes = recvfrom(client_socket, recv_buffer, sizeof(recv_buffer), 0,
                                      (struct sockaddr*)&from_addr, &from_len);
    if (received_bytes <= 0) {
        printf("Errore ricezione o timeout.\n");
#if defined(_WIN32)
        closesocket(client_socket); WSACleanup();
#else
        close(client_socket);
#endif
        freeaddrinfo(res);
        return -1;
    }

    weather_response_t resp;
    offset = 0;

    uint32_t net_status;
    memcpy(&net_status, recv_buffer + offset, sizeof(uint32_t));
    resp.status = ntohl(net_status);
    offset += sizeof(uint32_t);

    memcpy(&resp.type, recv_buffer + offset, sizeof(char));
    offset += sizeof(char);

    uint32_t net_value;
    memcpy(&net_value, recv_buffer + offset, sizeof(uint32_t));
    net_value = ntohl(net_value);
    memcpy(&resp.value, &net_value, sizeof(float));

    printf("Ricevuto risultato dal server %s (IP %s). ", resolved_name, resolved_ip);

    if (resp.status == 1) printf("Città non disponibile\n");
    else if (resp.status == 2) printf("Richiesta non valida\n");
    else if (resp.status == 0) {
        char formatted_city[CITY_LEN];
        strcpy(formatted_city, req.city);
        if (formatted_city[0] >= 'a' && formatted_city[0] <= 'z') formatted_city[0] -= ('a' - 'A');
        printf("%s: ", formatted_city);
        switch (resp.type) {
            case 't': printf("Temperatura = %.1f°C\n", resp.value); break;
            case 'h': printf("Umidita' = %.1f%%\n", resp.value); break;
            case 'w': printf("Vento = %.1f km/h\n", resp.value); break;
            case 'p': printf("Pressione = %.1f hPa\n", resp.value); break;
            default: printf("Dato sconosciuto = %.1f\n", resp.value);
        }
    }

    freeaddrinfo(res);
#if defined(_WIN32)
    closesocket(client_socket); WSACleanup();
#else
    close(client_socket);
#endif

    return 0;
}
