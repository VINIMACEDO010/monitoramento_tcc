#include <errno.h>
#include <mosquitto.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <libgen.h>
#include <unistd.h>

#include "biodata.h"
#include "util.c"

#define MQTT_SYNCHRONOUS 0

static bool mqtt_connacked = false;
static sig_atomic_t run = true;
static volatile bool mqtt_published = false;

static void
signal_handler(int32 unused) {
    (void)unused;
    run = false;
}

static void
publisher_connect_callback(struct mosquitto *mosquitto_client, void *obj,
                           int32 reason_code) {
    (void)obj;
    (void)mosquitto_client;

    if (reason_code != 0) {
        error("Error connecting: %s.\n", mosquitto_reason_string(reason_code));
        exit(EXIT_FAILURE);
    }

    error("Got CONNACK from broker.\n");
    mqtt_connacked = true;
}

static void
publisher_publish_callback(struct mosquitto *mosquitto_client, void *obj,
                           int32 message_id) {
    Payload *payload = obj;
    (void)mosquitto_client;
    (void)message_id;

    mqtt_published = true;
    error("QOS %d, %s | %ld: %s = %d\n",
          MQTT_PUBLISHER_QOS,
          program,
          payload->time,
          payload->plant_name,
          payload->data[0]);
}

int32
main(int32 argc, char *argv[]) {
    struct mosquitto *mosquitto_client;
    Payload payload;
    int32 mosq_errno;
    int32 timeout = MQTT_CONNACK_TIMEOUT;

    char mosquitto_client_id[40] = {0};
    bool clean_session = true;
    char *BIODATA_HOST;
    char *MQTT_PORT;
    int32 mqtt_port;

    srand((unsigned int)time(NULL));

    (void)argc;
    program = basename(argv[0]);

    mosquitto_lib_init();

    SNPRINTF(mosquitto_client_id, "publisher_%d", getpid());
    mosquitto_client =
        mosquitto_new(mosquitto_client_id, clean_session, &payload);

    if (mosquitto_client == NULL) {
        error("Error in mosquitto_new: %s.\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    mosquitto_connect_callback_set(mosquitto_client, publisher_connect_callback);
    mosquitto_publish_callback_set(mosquitto_client, publisher_publish_callback);

    BIODATA_HOST = xgetenv("BIODATA_HOST");
    MQTT_PORT = getenv("MQTT_PORT");
    mqtt_port = MQTT_PORT ? atoi(MQTT_PORT) : 1883;

    mosq_errno = mosquitto_connect(mosquitto_client, BIODATA_HOST, mqtt_port, 60);
    if (mosq_errno != MOSQ_ERR_SUCCESS) {
        mosquitto_destroy(mosquitto_client);
        error("Error connecting to broker: %s\n", mosquitto_strerror(mosq_errno));
        exit(EXIT_FAILURE);
    }

    if (!MQTT_SYNCHRONOUS) {
        mosq_errno = mosquitto_loop_start(mosquitto_client);
        if (mosq_errno != MOSQ_ERR_SUCCESS) {
            mosquitto_destroy(mosquitto_client);
            error("Error starting mosquitto loop: %s\n",
                  mosquitto_strerror(mosq_errno));
            exit(EXIT_FAILURE);
        }
    }

    while (!mqtt_connacked) {
        error("Waiting for CONNACK from broker...\n");
        timeout -= 1;
        if (timeout <= 0) {
            error("Got no CONNACK from broker after %d seconds. Exiting...\n",
                  MQTT_CONNACK_TIMEOUT);
            exit(EXIT_FAILURE);
        }

        if (MQTT_SYNCHRONOUS) {
            int err;
            if ((err = mosquitto_loop(mosquitto_client, -1, 1)) < 0) {
                error("Error in mosquitto_loop: %s.\n", mosquitto_strerror(err));
            }
        } else {
            sleep(1);
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    while (run) {
        static int32 counter = 0;
        time_t t = 0;
        struct timespec t0;
        struct timespec t1;
        struct timespec sleep_time;
        bool retain_message = false;

        if (clock_gettime(CLOCK_MONOTONIC, &t0) < 0) {
            error("Error getting time from CLOCK_MONOTONIC: %s.\n",
                  strerror(errno));
            exit(EXIT_FAILURE);
        }

        counter += 1;

        if ((t = time(NULL)) < 0) {
            error("Error getting time(NULL): %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        memset(payload.plant_name, 0, sizeof(payload.plant_name));
        strncpy(payload.plant_name, "caldeira", PLANT_NAME_MAX_LENGTH - 1);
        payload.time = (int64)t;

        {
            float temp  = 1200.0f + ((float)(rand() % 2001)) * 0.1f;
            float press = -7.0f   + ((float)(rand() % 71))   * 0.1f;
            float vazao = 10.0f   + ((float)(rand() % 21))   * 0.1f;
            float pvap  = 10.0f   + ((float)(rand() % 21))   * 0.1f;

            payload.data[0] = (int16)((temp  - 0.0f) * 10.0f);
            payload.data[1] = (int16)((press - 1.0f) * 10.0f);
            payload.data[2] = (int16)((vazao - 2.0f) * 10.0f);
            payload.data[3] = (int16)((pvap  - 3.0f) * 10.0f);
        }

        mosq_errno = mosquitto_publish(
            mosquitto_client,
            &counter,
            "caldeira/dados",
            sizeof(payload),
            &payload,
            MQTT_PUBLISHER_QOS,
            retain_message
        );

        if (mosq_errno != MOSQ_ERR_SUCCESS) {
            error("Error publishing: %s\n", mosquitto_strerror(mosq_errno));
            exit(EXIT_FAILURE);
        }

        if (MQTT_SYNCHRONOUS && (MQTT_PUBLISHER_QOS > 0)) {
            mqtt_published = false;
            while (!mqtt_published) {
                int err;
                if ((err = mosquitto_loop(mosquitto_client, -1, 1)) < 0) {
                    error("Error in mosquitto_loop: %s.\n",
                          mosquitto_strerror(err));
                }
            }
        }

        if (clock_gettime(CLOCK_MONOTONIC, &t1) < 0) {
            error("Error in clock_gettime(CLOCK_MONOTONIC): %s.\n",
                  strerror(errno));
            exit(EXIT_FAILURE);
        }

        {
            int64 elapsed_ns = (t1.tv_sec - t0.tv_sec) * 1000000000L
                             + (t1.tv_nsec - t0.tv_nsec);
            int64 interval = (int64)1e9;
            int64 remaining_ns = interval - elapsed_ns;

            if (remaining_ns > 0) {
                sleep_time.tv_sec = remaining_ns / 1000000000L;
                sleep_time.tv_nsec = remaining_ns % 1000000000L;
                nanosleep(&sleep_time, NULL);
            } else {
                error("Load warning: publisher spent %.6f seconds on loop.\n",
                      (double)elapsed_ns / 1e9);
            }
        }
    }

    if (!MQTT_SYNCHRONOUS) {
        mosquitto_loop_stop(mosquitto_client, true);
    }

    mosquitto_destroy(mosquitto_client);
    mosquitto_lib_cleanup();
    return EXIT_SUCCESS;
}