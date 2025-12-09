/*
 ============================================================================
 Name        : server-project.c
 Author      : Bilanzuoli Filippo
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include "protocol.h"

float get_random_float(float min, float max) {
    return min + (rand() / (float)RAND_MAX) * (max - min);
}

float get_temperature() { return get_random_float(-10.0f, 40.0f); }
float get_humidity()   { return get_random_float(20.0f, 100.0f); }
float get_wind()       { return get_random_float(0.0f, 100.0f); }
float get_pressure()   { return get_random_float(950.0f, 1050.0f); }

int is_city_valid(const char* city) {
    const char* valid_cities[] = {
        "bari", "roma", "milano", "napoli", "torino",
        "palermo", "genova", "bologna", "firenze", "venezia"
    };
    for (int i = 0; i < 10; i++) {
        if (strcasecmp(city, valid_cities[i]) == 0) return 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    srand((unsigned int)time(NULL));

    int port = DEFAULT_PORT;
    if (argc == 3 && strcmp(argv[1], "-p") == 0) {
        port = atoi(argv[2]);
    }
    printf("Server avviato sulla porta %d\n", port);

#if defined _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2,2), &wsa_data) != 0) {
        printf("Errore critico: WSAStartup fallita.\n");
        return -1;
    }
#endif

    int server_socket = socket(PF_INET, SOCK_DGRAM, 0);
    if (server_socket < 0) {
        perror("Impossibile creare il socket");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Errore Bind. Porta occupata?\n");
        close(server_socket);
        return -1;
    }

    printf("In attesa di richieste...\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        char recv_buffer[512];

        ssize_t bytes_read = recvfrom(server_socket, recv_buffer, sizeof(recv_buffer), 0,
                                      (struct sockaddr*)&client_addr, &client_len);
        if (bytes_read <= 0) continue;

        char client_ip[INET_ADDRSTRLEN];
        char client_host[NI_MAXHOST];

        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

        if (getnameinfo((struct sockaddr*)&client_addr, client_len,
                        client_host, sizeof(client_host), NULL, 0, 0) != 0) {
            strcpy(client_host, client_ip);
        }

        weather_request_t req;
        int offset = 0;

        if (bytes_read < (ssize_t)sizeof(char)) continue;

        memcpy(&req.type, recv_buffer + offset, sizeof(char));
        offset += sizeof(char);

        int city_len = bytes_read - offset;
        if (city_len > CITY_LEN) city_len = CITY_LEN;

        memset(req.city, 0, CITY_LEN);
        memcpy(req.city, recv_buffer + offset, city_len);
        req.city[CITY_LEN - 1] = '\0';

        printf("Richiesta ricevuta da %s (ip %s): type='%c', city='%s'\n",
               client_host, client_ip, req.type, req.city);

        weather_response_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.type = req.type;

        int valid_syntax = 1;
        for (size_t k = 0; k < strlen(req.city); k++) {
            char c = req.city[k];
            if (c == '\t' || strchr("@#$%^&*()=<>[]{}\\|;:", c) != NULL) {
                valid_syntax = 0;
                break;
            }
        }

        if (!valid_syntax) {
            resp.status = 2;
        } else if (!is_city_valid(req.city)) {
            resp.status = 1;
        } else if (strchr("thwp", req.type) == NULL) {
            resp.status = 2;
        } else {
            resp.status = 0;
            if (req.type == 't') resp.value = get_temperature();
            else if (req.type == 'h') resp.value = get_humidity();
            else if (req.type == 'w') resp.value = get_wind();
            else if (req.type == 'p') resp.value = get_pressure();
        }

        char send_buffer[sizeof(uint32_t) + sizeof(char) + sizeof(float)];
        offset = 0;

        uint32_t net_status = htonl(resp.status);
        memcpy(send_buffer + offset, &net_status, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        memcpy(send_buffer + offset, &resp.type, sizeof(char));
        offset += sizeof(char);

        uint32_t net_value;
        memcpy(&net_value, &resp.value, sizeof(float));
        net_value = htonl(net_value);
        memcpy(send_buffer + offset, &net_value, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        sendto(server_socket, send_buffer, offset, 0,
               (struct sockaddr*)&client_addr, client_len);
    }

    close(server_socket);
#if defined _WIN32
    WSACleanup();
#endif

    return 0;
}
