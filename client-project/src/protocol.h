#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define CITY_NAME_LEN 64

#ifndef DEFAULT_SERVER
#define DEFAULT_SERVER "127.0.0.1"
#endif

#ifndef DEFAULT_PORT
#define DEFAULT_PORT "8080"
#endif
typedef struct {
    char type;
    char city[CITY_NAME_LEN];
} weather_request_t;


typedef struct {
    uint32_t status;
    char type;
    float value;
} weather_response_t;


#define STATUS_SUCCESS 0u
#define STATUS_CITY_UNAVAILABLE 1u
#define STATUS_INVALID_REQUEST 2u


float get_temperature(void);
float get_humidity(void);
float get_wind(void);
float get_pressure(void);

#endif
