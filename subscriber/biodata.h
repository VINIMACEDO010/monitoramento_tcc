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

static char *template[] = {
    "PIT3101",
    "LIT3101",
    "PIT6101",
    "FIT5101",
    "PIT3101a",
    "LIT3101a",
    "PIT6101a",
    "FIT5101a",
    "PIT3101b",
    "LIT3101b",
    "PIT6101b",
    "FIT5101b",
    "PIT3101ax",
    "LIT3101ax",
    "PIT6101ax",
    "FIT5101ax",
    "PIT3101aa",
    "LIT3101aa",
    "PIT6101aa",
    "FIT5101aa",
    "PIT3101ab",
    "LIT3101ab",
    "PIT6101ab",
    "FIT5101ab",
    "PIT3101ca",
    "LIT3101ca",
    "PIT6101ca",
    "FIT5101ca",
    "PIT3101caa",
    "LIT3101caa",
    "PIT6101caa",
    "FIT5101caa",
    "PIT3101cab",
    "LIT3101cab",
    "PIT6101cab",
    "FIT5101cab",
};

#define NPARAMS LENGTH(template) + 1

#define MODBUS_SERVER_ADDR "testingbiodata.duckdns.org"
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
