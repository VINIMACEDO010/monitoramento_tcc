#ifndef BIODATA_H
#define BIODATA_H

#include <stdint.h>
#include <regex.h>

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef int8_t  int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

#ifndef BIODATA_DEBUG
#define BIODATA_DEBUG 0
#endif

#define MQTT_CONNACK_TIMEOUT 5

/* Intervalo entre duas leituras dos sensores simulados, em segundos. */
#define SAMPLE_INTERVAL_SECONDS 5
#define MQTT_PUBLISHER_QOS 1
#define MQTT_SUBSCRIBER_TOPIC "#"
#define MQTT_SUBSCRIBER_QOS 0

#define BIODATA_NODATA (int16)0

typedef struct Regex {
    regex_t regex;
    char *string;
} Regex;

#define MATCH_REGEX_SIMPLE(R, S) \
    !regexec(&R.regex, S, 0, NULL, 0)

#define RED "\x1b[31m"
#define GREEN "\x1b[32m"
#define RESET "\x1b[0m"

#define LENGTH(X) (int64)(sizeof(X) / sizeof(*X))

#ifndef SNPRINTF
#define SNPRINTF(BUFFER, FORMAT, ...) \
    snprintf2(BUFFER, sizeof(BUFFER), FORMAT, __VA_ARGS__)
#endif

/* Sensores da caldeira:
 *   temp_fornalha  : 1200 a 1400 graus C
 *   press_fornalha : -7 a 0 mmca
 *   vazao_caldeira : 10 a 12 t/h
 *   press_vapor    : 10 a 12 bar
 */
static char *template[] = {
    "temp_fornalha",
    "press_fornalha",
    "vazao_caldeira",
    "press_vapor",
};

#define NPARAMS LENGTH(template) + 1

#define MODBUS_SERVER_PORT 1502
#define MODBUS_START_ADDR 0
#define MODBUS_NREGS (int32)(sizeof(Payload)/sizeof(uint16))

#define PLANT_NAME_MAX_LENGTH 32

typedef struct Payload {
    char plant_name[PLANT_NAME_MAX_LENGTH];
    int64 time;
    int16 data[LENGTH(template)];
} Payload;

static const Payload dummy_payload = {0};
static char *program;

#endif