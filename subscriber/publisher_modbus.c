#include <arpa/inet.h>
#include <errno.h>
#include <modbus/modbus.h>
#include <netdb.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <libgen.h>
#include <unistd.h>

#include "util.c"

#define NTESTCLIENTS 5

static sig_atomic_t run = true;

static void
signal_handler(int32 unused) {
    (void)unused;
    run = false;
    return;
}

static int32
resolve(char *hostname, char *ip_adress, uint32 ip_adress_size) {
    struct addrinfo addr_hints;
    struct addrinfo *addr_result;
    struct addrinfo *addr_iter;
    int32 err;

    memset(&addr_hints, 0, sizeof(addr_hints));
    addr_hints.ai_family = AF_INET;
    addr_hints.ai_socktype = SOCK_STREAM;  // any type is fine for name lookup

    if ((err = getaddrinfo(hostname, NULL, &addr_hints, &addr_result))) {
        error("Error translating name '%s'"
              " to IP address using getaddrinfo: %s.\n",
              hostname, gai_strerror(err));
        return -1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    for (addr_iter = addr_result; addr_iter; addr_iter = addr_iter->ai_next) {
        void *addr;
        struct sockaddr_in *ipv4;

        switch (addr_iter->ai_family) {
        case AF_INET:
            ipv4 = (struct sockaddr_in *)addr_iter->ai_addr;
            addr = &(ipv4->sin_addr);
            break;
        default:
            error("ai_family is not IPv4.\n");
            exit(EXIT_FAILURE);
        }

        if (inet_ntop(addr_iter->ai_family, addr, ip_adress, ip_adress_size)) {
            freeaddrinfo(addr_result);
            return 0;
        } else {
            error("Error in inet_ntop: %s.\n", strerror(errno));
        }
    }

    freeaddrinfo(addr_result);
    return 0;
}

int32
main(int32 argc, char *argv[]) {
    char ip_adress[INET6_ADDRSTRLEN];
    char *BIODATA_HOST;
    modbus_t *modbus;
    char plant_names[NTESTCLIENTS][34];

    (void)argc;
    program = basename(argv[0]);

    BIODATA_HOST = xgetenv("BIODATA_HOST");
    if (resolve(BIODATA_HOST, ip_adress, sizeof(ip_adress)) < 0) {
        error("Error resolving hostname %s.\n", BIODATA_HOST);
        exit(EXIT_FAILURE);
    }

    modbus = modbus_new_tcp(ip_adress, MODBUS_SERVER_PORT);
    if (modbus == NULL) {
        error("Error in modbus_new_tcp: %s.\n", modbus_strerror(errno));
        exit(EXIT_FAILURE);
    }

    while (modbus_connect(modbus) < 0) {
        error("Error in modbus_connect: %s.\n", modbus_strerror(errno));
        sleep(1);
    }

    if (modbus_set_response_timeout(modbus, 2, 0) < 0) {
        error("Error in modbus_set_response_timeout: %s.\n",
              modbus_strerror(errno));
        exit(EXIT_FAILURE);
    }

    for (int32 i = 0; i < LENGTH(plant_names); i += 1) {
        util_generate_plant_name(plant_names[i], sizeof(plant_names[i]));
    }

    while (run) {
        static int32 nfailed = 0;
        Payload payload;
        struct timespec t0;
        struct timespec t1;
        static int32 counter = 0;
        uint16 modbus_registers[MODBUS_NREGS];
        int32 n;
        char *plant_name = plant_names[counter % (int32)LENGTH(plant_names)];

        if (clock_gettime(CLOCK_MONOTONIC, &t0) < 0) {
            error("Error getting time from CLOCK_MONOTONIC: %s.\n",
                  strerror(errno));
            exit(EXIT_FAILURE);
        }

        memset(payload.plant_name, 0, sizeof(payload.plant_name));
        memcpy(payload.plant_name, plant_name,
               MIN(PLANT_NAME_MAX_LENGTH, strlen(plant_name) + 1));
        payload.plant_name[PLANT_NAME_MAX_LENGTH - 1] = '\0';
        if ((payload.time = (int64)time(NULL)) < 0) {
            error("time() failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        memset(payload.data, 0x0C, sizeof(payload.data));

        memcpy(&modbus_registers, &payload, sizeof(modbus_registers));

        if ((n = modbus_write_registers(modbus, MODBUS_START_ADDR, MODBUS_NREGS,
                                        modbus_registers))
            < 0) {
            error("Error in modbus_write_registers: %s.\n",
                  modbus_strerror(errno));
            if (errno == EPIPE) {
                exit(EXIT_FAILURE);
            }

            nfailed += 1;
            if (BIODATA_DEBUG || (nfailed >= 50)) {
                exit(EXIT_FAILURE);
            }
            continue;
        }
        if (nfailed > 0) {
            nfailed -= 1;
        }
        if (n != MODBUS_NREGS) {
            error("Error: number of written registers is smaller than "
                  "requested.\n");
            exit(EXIT_FAILURE);
        }
        error("%s | %ld: %s = %d\n", program, payload.time, payload.plant_name,
              payload.data[0]);

        counter += 1;

        if (clock_gettime(CLOCK_MONOTONIC, &t1) < 0) {
            error("Error in clock_gettime(CLOCK_MONOTONIC): %s.\n",
                  strerror(errno));
            exit(EXIT_FAILURE);
        }

        {
            int64 elapsed_ns = (t1.tv_sec - t0.tv_sec)*1000000000L
                               + (t1.tv_nsec - t0.tv_nsec);
            int64 interval = (double)1e9 / (double)LENGTH(plant_names);

            int64 remaining_ns = interval - elapsed_ns;
            if (remaining_ns > 0) {
                struct timespec sleep_time;
                sleep_time.tv_sec = remaining_ns / 1000000000L;
                sleep_time.tv_nsec = remaining_ns % 1000000000L;
                while (nanosleep(&sleep_time, &sleep_time) < 0)
                    ;
            } else {
                error("Load warning: publisher spent %.6f seconds on loop.\n",
                      (double)elapsed_ns / 1e9);
            }
        }
    }

    modbus_close(modbus);
    modbus_free(modbus);
    exit(EXIT_SUCCESS);
}
