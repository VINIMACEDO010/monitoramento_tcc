#if !defined(UTIL_C)
#define UTIL_C

#include <arpa/inet.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "biodata.h"

static void
error(char *format, ...) {
    char buffer[BUFSIZ*10];
    char buffer2[BUFSIZ*10];
    va_list args;
    int32 n;

    va_start(args, format);
    n = vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    va_end(args);

    if (n < 0 || n > (int32)sizeof(buffer)) {
        fprintf(stderr,
                "Error in vsnprintf():"
                "buffer[%lu] is not large enough for error message\n",
                sizeof(buffer));
        exit(EXIT_FAILURE);
    }

    n = snprintf(buffer2, sizeof(buffer2), "%s", buffer);

    if (n < 0 || n > (int32)sizeof(buffer2)) {
        fprintf(stderr, "Error in vsnprintf()\n");
        exit(EXIT_FAILURE);
    }

    buffer2[n] = '\0';
    write(STDERR_FILENO, buffer2, (size_t)n);
    fsync(STDERR_FILENO);
    fsync(STDOUT_FILENO);
    return;
}

static int32
snprintf2(char *buffer, int64 size, char *format, ...) {
    int32 n;
    va_list args;

    assert(size >= (int64)sizeof(buffer));

    va_start(args, format);
    n = vsnprintf(buffer, (size_t)size, format, args);
    va_end(args);

    if (n >= size) {
        error("Error in snprintf: buffer is too small.\n");
        exit(EXIT_FAILURE);
    }
    if (n <= 0) {
        error("Error in snprintf.\n");
        exit(EXIT_FAILURE);
    }
    return n;
}

static void
network_double(double in, double *out) {
    uint64 *i = (uint64 *)&in;
    uint32 *r = (uint32 *)out;

    r[0] = htonl((uint32)((*i) >> 32));
    r[1] = htonl((uint32)*i);
    return;
}

static void
network_int64(int64 in, int64 *out) {
    uint64 *i = (uint64 *)&in;
    uint32 *r = (uint32 *)out;

    r[0] = htonl((uint32)((*i) >> 32));
    r[1] = htonl((uint32)*i);
    return;
}

static int64
util_4int16_int64(const uint16 registers[4]) {
    int64 v = 0;
    v |= ((int64)registers[0] << 48);
    v |= ((int64)registers[1] << 32);
    v |= ((int64)registers[2] << 16);
    v |= ((int64)registers[3]);
    return v;
}

static void
network_int16(int16 in, int16 *out) {
    uint16 tmp = (uint16)in;
    tmp = htons(tmp);
    *out = (int16)tmp;
    return;
}

static void
network_float32(float in, char out[4]) {
    uint32_t tmp;
    memcpy(&tmp, &in, sizeof(tmp));
    tmp = htonl(tmp);
    memcpy(out, &tmp, sizeof(tmp));
}

static void *
xmalloc(const int64 size) {
    void *p;

    if (size <= 0) {
        error("Error in xmalloc(%ld): Invalid size.\n", size);
        exit(EXIT_FAILURE);
    }
    if ((p = malloc((size_t)size)) == NULL) {
        error("Failed to allocate %zu bytes.\n", size);
        exit(EXIT_FAILURE);
    }
    return p;
}

static char *
util_random_string(int32 max_length) {
    char allowed[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                     "abcdefghijklmnopqrstuvwxyz";
    int32 n;
    char *string;

    max_length -= 1;
    n = (rand() % (max_length - 5)) + 5;

    string = xmalloc(n + 1);

    for (int32 i = 0; i < n; i += 1) {
        char c = allowed[(size_t)rand() % (sizeof(allowed) - 1)];
        string[i] = c;
    }
    string[n] = '\0';
    return string;
}

static void
util_generate_plant_name(char *buffer, int64 size) {
    char *plant_type[] = {
        "HRX",
        "HRS",
        "HRZ",
        "VRT",
    };
    int32 max_length = 16;

    char *type = plant_type[rand() % LENGTH(plant_type)];
    char *name = util_random_string(max_length);
    int32 number = rand() % 10000;
    int32 number2 = rand() % 5000;

    snprintf2(buffer, size, "_2%05d_%s%05d_%s", number, type, number2, name);
    free(name);
    return;
}

static char *
xgetenv(char *name) {
    char *variable;
    if ((variable = getenv(name)) == NULL) {
        error("%s environment variable is not set.\n", name);
        exit(EXIT_FAILURE);
    }
    return variable;
}

static void
util_compile_regex(Regex *regex) {
    if (regcomp(&regex->regex, regex->string, REG_EXTENDED)) {
        error("Could not compile regex %s.\n", regex->string);
        exit(EXIT_FAILURE);
    }
    return;
}

#define REGEX_MATCH_SIMPLE(R, S) !regexec(&R.regex, S, 0, NULL, 0)

#endif /* UTIL_C */
