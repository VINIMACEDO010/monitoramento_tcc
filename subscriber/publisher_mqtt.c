#include <errno.h>
#include <mosquitto.h>
#include <openssl/ssl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <libgen.h>
#include <unistd.h>

#include "biodata.h"
#include "util.c"

#define NTESTCLIENTS 400
#define MQTT_SYNCHRONOUS 0

static bool mqtt_connacked = false;
static sig_atomic_t run = true;
static volatile bool mqtt_published = false;

static void
signal_handler(int32 unused) {
    (void)unused;
    run = false;
    return;
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
    return;
}

static void
publisher_publish_callback(struct mosquitto *mosquitto_client, void *obj,
                           int32 message_id) {
    Payload *payload = obj;
    (void)mosquitto_client;
    (void)message_id;
    /* This callback means the following:
     * QOS 0: Message was written to operating system
     * QOS 1: Received PUBACK from broker
     * QOS 2: Received PUBCOMP from the broker */
    mqtt_published = true;
    error("QOS %d, %s | %ld: %s = %d\n", MQTT_PUBLISHER_QOS, program,
          payload->time, payload->plant_name, payload->data[0]);
    return;
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
    char plant_names[NTESTCLIENTS][34];

    (void)argc;
    program = basename(argv[0]);

    mosquitto_lib_init();

    SNPRINTF(mosquitto_client_id, "publisher_%d", getpid());
    mosquitto_client
        = mosquitto_new(mosquitto_client_id, clean_session, &payload);
    if (mosquitto_client == NULL) {
        error("Error in mosquitto_new: %s.\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    mosquitto_connect_callback_set(mosquitto_client,
                                   publisher_connect_callback);
    mosquitto_publish_callback_set(mosquitto_client,
                                   publisher_publish_callback);

    if (true) {
        char *MQTT_CA_CERT = xgetenv("MQTT_CA_CERT");
        char *MQTT_CLIENT_CERT = xgetenv("MQTT_CLIENT_CERT");
        char *MQTT_CLIENT_KEY = xgetenv("MQTT_CLIENT_KEY");

        if ((mosq_errno = mosquitto_tls_opts_set(
                 mosquitto_client, SSL_VERIFY_NONE, "tlsv1.3", NULL))
            != MOSQ_ERR_SUCCESS) {
            error("Error setting tls options: %s.\n",
                  mosquitto_strerror(mosq_errno));
            exit(EXIT_FAILURE);
        }
        if ((mosq_errno
             = mosquitto_tls_set(mosquitto_client, MQTT_CA_CERT, NULL,
                                 MQTT_CLIENT_CERT, MQTT_CLIENT_KEY, NULL))
            != MOSQ_ERR_SUCCESS) {
            error("Error setting tls: %s\n", mosquitto_strerror(mosq_errno));
            exit(EXIT_FAILURE);
        }
    }

    BIODATA_HOST = xgetenv("BIODATA_HOST");
    mosq_errno = mosquitto_connect(mosquitto_client, BIODATA_HOST, 8883, 60);
    if (mosq_errno != MOSQ_ERR_SUCCESS) {
        mosquitto_destroy(mosquitto_client);
        error("Error connecting to broker: %s\n",
              mosquitto_strerror(mosq_errno));
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
            error("Got no CONNACK from broker after %d seconds."
                  "Exiting...\n",
                  MQTT_CONNACK_TIMEOUT);
            exit(EXIT_FAILURE);
        }
        if (MQTT_SYNCHRONOUS) {
            int err;
            if ((err = mosquitto_loop(mosquitto_client, -1, 1)) < 0) {
                error("Error in mosquitto_loop: %s.\n",
                      mosquitto_strerror(err));
            }
        } else {
            sleep(1);
        }
    }

    for (int32 i = 0; i < LENGTH(plant_names); i += 1) {
        util_generate_plant_name(plant_names[i], sizeof(plant_names[i]));
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
        char *plant_name = plant_names[counter % (int32)LENGTH(plant_names)];

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

        memcpy(&payload.plant_name, plant_name, PLANT_NAME_MAX_LENGTH);
        payload.plant_name[PLANT_NAME_MAX_LENGTH - 1] = '\0';
        payload.time = (int64)t;
        for (int32 i = 0; i < LENGTH(payload.data); i += 1) {
            payload.data[i] = 123;
        }

        mosq_errno = mosquitto_publish(mosquitto_client, &counter,
                                       "dummy_topic", sizeof(payload), &payload,
                                       MQTT_PUBLISHER_QOS, retain_message);
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
            int64 elapsed_ns = (t1.tv_sec - t0.tv_sec)*1000000000L
                               + (t1.tv_nsec - t0.tv_nsec);
            int64 interval = ((double)1e9 / (double)LENGTH(plant_names));

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

    mosquitto_lib_cleanup();
    exit(EXIT_SUCCESS);
}
