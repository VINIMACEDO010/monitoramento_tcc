#include <arpa/inet.h>
#include <errno.h>
#include <libpq-fe.h>
#include <modbus/modbus.h>
#include <mosquitto.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include </usr/include/postgresql/server/catalog/pg_type_d.h>

#include "biodata.h"
#include "util.c"

#define POSTGRES_FORMAT_BINARY 1

#define MAX_MODBUS_CLIENTS 100

static char *POSTGRES_HOST;
static char *POSTGRES_PORT;
static char *POSTGRES_USER;
static char *POSTGRES_DB;

static bool run = true;

static Oid pg_param_types[NPARAMS];
static int32 pg_param_formats[NPARAMS];
static char *pg_param_values[NPARAMS];
static float pg_param_values_values[NPARAMS];
static int32 pg_param_lengths[NPARAMS];

static void sub_handle_signal(int32);
static void sub_mqtt_connected(struct mosquitto *, void *, int32);
static void sub_mqtt_disconnected(struct mosquitto *, void *, int32);
static ExecStatusType sub_pg_simple(PGconn *, const char *);
void sub_mqtt_callback(struct mosquitto *, void *,
                       const struct mosquitto_message *);

void
sub_handle_signal(int32 number) {
    (void)number;
    run = false;
    return;
}

void
sub_mqtt_connected(struct mosquitto *mosquitto, void *user_data, int32 result) {
    (void)mosquitto;
    (void)user_data;
    (void)result;
    error("Connected to mosquitto broker.\n");
    return;
}

void
sub_mqtt_disconnected(struct mosquitto *mosquitto, void *user_data,
                      int32 result) {
    (void)mosquitto;
    (void)user_data;
    (void)result;
    error("Disconnected from mosquitto broker.\n");
    return;
}

ExecStatusType
sub_pg_simple(PGconn *pg_connection, const char *command) {
    PGresult *pg_result;
    ExecStatusType status;

    pg_result
        = PQexecParams(pg_connection, command, 0, NULL, NULL, NULL, NULL, 0);
    status = PQresultStatus(pg_result);

#define CASE(CODE)                                                             \
    case CODE:                                                                 \
        error("Error running SQL query '\n%s\n': (" #CODE "): %s.\n", command, \
              PQresultErrorMessage(pg_result));                                \
        break

    switch (status) {
    case PGRES_COMMAND_OK:
        break;
        CASE(PGRES_EMPTY_QUERY);
        CASE(PGRES_TUPLES_OK);
        CASE(PGRES_COPY_OUT);
        CASE(PGRES_COPY_IN);
        CASE(PGRES_BAD_RESPONSE);
        CASE(PGRES_NONFATAL_ERROR);
        CASE(PGRES_FATAL_ERROR);
        CASE(PGRES_COPY_BOTH);
        CASE(PGRES_SINGLE_TUPLE);
        CASE(PGRES_PIPELINE_SYNC);
        CASE(PGRES_PIPELINE_ABORTED);
        CASE(PGRES_TUPLES_CHUNK);
    default:
        error("Unhandled postgres error: %d.\n", status);
        exit(EXIT_FAILURE);
    }

#undef CASE

    return status;
}

static void
sub_payload_insert(PGconn *pg_connection, Payload *payload) {
    char sql_query[200*LENGTH(template)] = {0};
    char sql_columns[20*LENGTH(template)] = {0};
    char sql_values[20*LENGTH(template)] = {0};
    char sql_add_columns[100*LENGTH(template)] = {0};
    static Regex regex_table_name = {0};

    int32 n0 = 0;
    int32 n1 = 0;
    int32 n2 = 0;
    int32 m = 0;
    int32 space;

    error("%ld: %s = %d\n", payload->time, payload->plant_name,
          payload->data[0]);

    if (regex_table_name.string == NULL) {
        regex_table_name.string = "^[A-Za-z_][A-Za-z0-9_]*$";
        util_compile_regex(&regex_table_name);
    }

    if (!REGEX_MATCH_SIMPLE(regex_table_name, payload->plant_name)) {
        error("Invalid plant name: %s\n", payload->plant_name);
        if (BIODATA_DEBUG) {
            exit(EXIT_FAILURE);
        } else {
            int64 n = LENGTH(payload->plant_name);
            memmove(&(payload->plant_name[1]), payload->plant_name,
                    (size_t)n - 1);
            payload->plant_name[0] = '_';
            payload->plant_name[n - 1] = '\0';
        }
        return;
    }

    for (int64 i = 0; i < LENGTH(payload->data); i += 1) {
        if (payload->data[i] == BIODATA_NODATA) {
            continue;
        }
        space = (int32)sizeof(sql_columns) - n0;
        m = snprintf2(&sql_columns[n0], space, ",%s", template[i]);
        n0 += m;

        space = (int32)sizeof(sql_add_columns) - n1;
        m = snprintf2(&sql_add_columns[n1], space,
                      "ADD COLUMN IF NOT EXISTS %s float4,\n", template[i]);
        n1 += m;

        space = (int32)sizeof(sql_values) - n2;
        m = snprintf2(&sql_values[n2], space, ",$%d", (int32)i + 2);
        n2 += m;
    }

    SNPRINTF(sql_query,
             "CREATE TABLE IF NOT EXISTS %s (\n"
             "    time int8 not null,\n"
             "    primary key (time));\n",
             payload->plant_name);

    sub_pg_simple(pg_connection, sql_query);

    pg_param_values[0] = (char *)&(payload->time);
    network_int64(payload->time, &(payload->time));
    for (int64 i = 0; i < LENGTH(payload->data); i += 1) {
        float convert = ((float)(payload->data[i]) / 10.0f) + (float)i;
        network_float32(convert, (char *)(&pg_param_values_values[i + 1]));
    }

    do {
        PGresult *pg_result;
        ExecStatusType status;

        SNPRINTF(sql_query, "INSERT INTO %s (time%s) VALUES ($1%s);\n",
                 payload->plant_name, sql_columns, sql_values);
        pg_result
            = PQexecParams(pg_connection, sql_query, LENGTH(pg_param_types),
                           pg_param_types, (const char *const *)pg_param_values,
                           pg_param_lengths, pg_param_formats, 0);
        status = PQresultStatus(pg_result);

        if (status == PGRES_FATAL_ERROR) {
            int32 n;
            n = SNPRINTF(sql_query,
                         "ALTER TABLE %s\n"
                         "%s;\n",
                         payload->plant_name, sql_add_columns);
            sql_query[n - 4] = ';';
            sql_query[n - 3] = '\0';
            status = sub_pg_simple(pg_connection, sql_query);
            if (status != PGRES_COMMAND_OK) {
                error("Error adding columns to table.\n");
                break;
            }

            SNPRINTF(sql_query, "INSERT INTO %s (time%s) VALUES ($1%s);\n",
                     payload->plant_name, sql_columns, sql_values);
            pg_result = PQexecParams(pg_connection, sql_query,
                                     LENGTH(pg_param_types), pg_param_types,
                                     (const char *const *)pg_param_values,
                                     pg_param_lengths, pg_param_formats, 0);
            status = PQresultStatus(pg_result);

            if (status != PGRES_COMMAND_OK) {
                error("Error inserting values into table"
                      " even after adding the colums.\n");
                error("Error running SQL query '\n%s\n': %s.\n", sql_query,
                      PQresultErrorMessage(pg_result));
            }
        }
    } while (false);

    return;
}

void
sub_mqtt_callback(struct mosquitto *mosquitto, void *user_data,
                  const struct mosquitto_message *message) {
    PGconn *pg_connection = user_data;
    Payload payload;

    (void)mosquitto;
    if (message->payloadlen != sizeof(payload)) {
        error("Received payload length (%d)"
              " does not match template size (%zu).\n",
              message->payloadlen, sizeof(payload));
    }

    memcpy(&payload, message->payload, sizeof(payload));

    sub_payload_insert(pg_connection, &payload);
    return;
}

static void
xmosquitto_reconnect(struct mosquitto *mosquitto) {
    int32 err;
    if ((err = mosquitto_reconnect(mosquitto)) != MOSQ_ERR_SUCCESS) {
        error("Error reconnecting to broker: %s.\n", mosquitto_strerror(err));
        exit(EXIT_FAILURE);
    }
    return;
}

int32
main(int32 argc, char *argv[]) {
    PGconn *pg_connection;
    struct mosquitto *mosquitto;
    modbus_t *modbus;
    modbus_mapping_t *modbus_mapping;
    int32 modbus_listener;
    int32 modbus_sockets[MAX_MODBUS_CLIENTS];
    int32 mosq_errno;

    char pg_config_string[4096];

    char *MQTT_HOST;
    char *MQTT_PORT;
    char *MQTT_KEEPALIVE;
    char *MQTT_USE_TLS;
    int32 mqtt_port;
    int32 mqtt_keepalive;

    struct passwd *sub_pw;

    (void)argc;
    program = argv[0];

    if (geteuid() == 0) {
        if ((sub_pw = getpwnam("subscriber")) == NULL) {
            error("Error getting uid for user subscriber: %s.\n",
                  strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (setuid(sub_pw->pw_uid) < 0) {
            error("Error setting uid to %d: %s.\n", sub_pw->pw_uid,
                  strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    memset(&modbus_sockets, -1, sizeof(modbus_sockets));

    POSTGRES_HOST = xgetenv("POSTGRES_HOST");
    POSTGRES_PORT = xgetenv("POSTGRES_PORT");
    POSTGRES_USER = xgetenv("POSTGRES_USER");
    POSTGRES_DB = xgetenv("POSTGRES_DB");

    pg_param_types[0] = INT8OID;
    pg_param_formats[0] = POSTGRES_FORMAT_BINARY;
    pg_param_lengths[0] = sizeof(int64);
    pg_param_values[0] = NULL;
    for (int64 i = 0; i < LENGTH(dummy_payload.data); i += 1) {
        pg_param_types[i + 1] = FLOAT4OID;
        pg_param_formats[i + 1] = POSTGRES_FORMAT_BINARY;
        pg_param_values[i + 1] = (char *)(&pg_param_values_values[i + 1]);
        pg_param_lengths[i + 1] = sizeof(float);
    }

    SNPRINTF(pg_config_string, "host=%s port=%s user=%s dbname=%s",
             POSTGRES_HOST, POSTGRES_PORT, POSTGRES_USER, POSTGRES_DB);
    error("Postgres database parameters:\n%s\n", pg_config_string);

    while (true) {
        sleep(1);
        pg_connection = PQconnectdb(pg_config_string);
        if (PQstatus(pg_connection) == CONNECTION_BAD) {
            error("Error connecting to postgres database: %s",
                  PQerrorMessage(pg_connection));
            continue;
        } else {
            break;
        }
    }
    error("Connected to postgres database.\n");

    {
        bool clean_session = true;
        char mosquitto_client_id[40] = {0};

        MQTT_HOST = xgetenv("MQTT_HOST");
        MQTT_PORT = xgetenv("MQTT_PORT");
        MQTT_KEEPALIVE = xgetenv("MQTT_KEEPALIVE");
        MQTT_USE_TLS = getenv("MQTT_USE_TLS");

        mqtt_port = atoi(MQTT_PORT);
        mqtt_keepalive = atoi(MQTT_KEEPALIVE);

        SNPRINTF(mosquitto_client_id, "sub_%d", getpid());

        mosquitto_lib_init();
        if (!(mosquitto = mosquitto_new(mosquitto_client_id, clean_session,
                                        pg_connection))) {
            error("Error creating new mosquitto connection: %s\n",
                  strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    if (atoi(MQTT_USE_TLS)) {
        const char *MQTT_CA_CERT = xgetenv("MQTT_CA_CERT");
        const char *MQTT_CLIENT_CERT = xgetenv("MQTT_CLIENT_CERT");
        const char *MQTT_CLIENT_KEY = xgetenv("MQTT_CLIENT_KEY");

        if ((mosq_errno = mosquitto_tls_opts_set(mosquitto, SSL_VERIFY_PEER,
                                                 "tlsv1.3", NULL))
            != MOSQ_ERR_SUCCESS) {
            error("Error setting tls options: %s.\n",
                  mosquitto_strerror(mosq_errno));
            exit(EXIT_FAILURE);
        }
        if ((mosq_errno
             = mosquitto_tls_set(mosquitto, MQTT_CA_CERT, NULL,
                                 MQTT_CLIENT_CERT, MQTT_CLIENT_KEY, NULL))
            != MOSQ_ERR_SUCCESS) {
            error("Error setting tls files: %s\n",
                  mosquitto_strerror(mosq_errno));
            exit(EXIT_FAILURE);
        }
    }

    mosquitto_connect_callback_set(mosquitto, sub_mqtt_connected);
    mosquitto_disconnect_callback_set(mosquitto, sub_mqtt_disconnected);
    mosquitto_message_callback_set(mosquitto, sub_mqtt_callback);

    error("Connecting to %s:%d with timeout %ds.\n", MQTT_HOST, mqtt_port,
          mqtt_keepalive);
    mosq_errno
        = mosquitto_connect(mosquitto, MQTT_HOST, mqtt_port, mqtt_keepalive);
    if (mosq_errno != MOSQ_ERR_SUCCESS) {
        error("Error connecting to mqtt broker: %s\n",
              mosquitto_strerror(mosq_errno));
        exit(EXIT_FAILURE);
    }

    {
        int32 *message_id = NULL;
        error("Subscribing to topic \"%s\" ...\n", MQTT_SUBSCRIBER_TOPIC);
        mosq_errno = mosquitto_subscribe(
            mosquitto, message_id, MQTT_SUBSCRIBER_TOPIC, MQTT_SUBSCRIBER_QOS);
        if (mosq_errno != MOSQ_ERR_SUCCESS) {
            error("Error subscribing to topic \"%s\": %s.\n",
                  MQTT_SUBSCRIBER_TOPIC, mosquitto_strerror(mosq_errno));
            exit(EXIT_FAILURE);
        }
    }

    if ((modbus = modbus_new_tcp("0.0.0.0", MODBUS_SERVER_PORT)) == NULL) {
        error("Error in modbus_new_tcp: %s.\n", modbus_strerror(errno));
        exit(EXIT_FAILURE);
    }

    if ((modbus_mapping = modbus_mapping_new(0, 0, MODBUS_NREGS, 0)) == NULL) {
        error("Error in modbus_mapping_new: %s.\n", modbus_strerror(errno));
        modbus_free(modbus);
        exit(EXIT_FAILURE);
    }

    if ((modbus_listener = modbus_tcp_listen(modbus, MAX_MODBUS_CLIENTS)) < 0) {
        error("Error in modbus_tcp_listen: %s.\n", modbus_strerror(errno));
        modbus_mapping_free(modbus_mapping);
        modbus_free(modbus);
        exit(EXIT_FAILURE);
    }

    error("Modbus reader listening on TCP port %d.\n", MODBUS_SERVER_PORT);

    signal(SIGINT, sub_handle_signal);
    signal(SIGTERM, sub_handle_signal);

    while (run) {
        uint8 query[MODBUS_TCP_MAX_ADU_LENGTH];
        int32 mqtt_fd;
        nfds_t idx0;
        int32 npolled;
        int32 poll_timeout = 1000;
        int32 err;

        struct pollfd pipes[1 + 1 + MAX_MODBUS_CLIENTS];
        nfds_t nfds = 0;
        int32 map_idx[MAX_MODBUS_CLIENTS];

        if ((err = mosquitto_loop_write(mosquitto, 1)) != MOSQ_ERR_SUCCESS) {
            error("Error in mosquitto_loop_write: %s.\n",
                  mosquitto_strerror(err));
            switch (err) {
            case MOSQ_ERR_NO_CONN:
            case MOSQ_ERR_CONN_LOST:
                xmosquitto_reconnect(mosquitto);
                break;
            default:
                error("This error should not happen.\n");
                exit(EXIT_FAILURE);
            }
        }
        if ((err = mosquitto_loop_misc(mosquitto)) != MOSQ_ERR_SUCCESS) {
            error("Error in mosquitto_loop_misc: %s.\n",
                  mosquitto_strerror(err));
            switch (err) {
            case MOSQ_ERR_NO_CONN:
            case MOSQ_ERR_CONN_LOST:
                xmosquitto_reconnect(mosquitto);
                break;
            default:
                error("This error should not happen.\n");
                exit(EXIT_FAILURE);
            }
        }

        if ((mqtt_fd = mosquitto_socket(mosquitto)) < 0) {
            error("Trying to reconnect to broker...\n");
            xmosquitto_reconnect(mosquitto);
        }

        for (int32 i = 0; i < LENGTH(pipes); i += 1) {
            pipes[i].fd = -1;
            pipes[i].events = POLLIN;
        }

        if (mqtt_fd >= 0) {
            pipes[nfds].fd = mqtt_fd;
            pipes[nfds].events = POLLIN;
            nfds += 1;
        }

        if (modbus_listener >= 0) {
            pipes[nfds].fd = modbus_listener;
            pipes[nfds].events = POLLIN;
            nfds += 1;
        }

        for (int32 i = 0; i < MAX_MODBUS_CLIENTS; i += 1) {
            if (modbus_sockets[i] >= 0) {
                map_idx[i] = (int32)nfds;
                pipes[nfds].fd = modbus_sockets[i];
                pipes[nfds].events = POLLIN;
                nfds += 1;
            } else {
                map_idx[i] = -1;
            }
        }

        switch (npolled = poll(pipes, nfds, poll_timeout)) {
        case 0:
            if (BIODATA_DEBUG) {
                error("poll: timeout after %d ms.\n", poll_timeout);
            }
            continue;
        case -1:
            if (errno == EINTR) {
                error("Interruption while polling.\n");
                continue;
            } else {
                error("Error in polling: %s.\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
        default:
            if (BIODATA_DEBUG) {
                error("npolled=%d\n", npolled);
            }
            break;
        }

        idx0 = 0;
        if (mqtt_fd >= 0 && (pipes[idx0].revents & POLLIN)) {
            if ((err = mosquitto_loop_read(mosquitto, 1)) != MOSQ_ERR_SUCCESS) {
                error("Error in mosquitto_loop_read: %s.\n",
                      mosquitto_strerror(err));
                switch (err) {
                case MOSQ_ERR_NO_CONN:
                case MOSQ_ERR_CONN_LOST:
                    xmosquitto_reconnect(mosquitto);
                    continue;
                default:
                    error("Invalid condition.\n");
                    if (BIODATA_DEBUG) {
                        exit(EXIT_FAILURE);
                    }
                }
            }
        }
        idx0 += 1;

        if (modbus_listener >= 0 && (pipes[idx0].revents & POLLIN)) {
            struct sockaddr_in clientaddr;
            socklen_t addrlen = sizeof(clientaddr);
            int32 newfd;
            bool added = false;

            if ((newfd = accept(modbus_listener, (struct sockaddr *)&clientaddr,
                                &addrlen))
                < 0) {
                error("Error accepting modbus client: %s.\n", strerror(errno));
                continue;
            }

            for (int32 i = 0; i < MAX_MODBUS_CLIENTS; i += 1) {
                if (modbus_sockets[i] < 0) {
                    modbus_sockets[i] = newfd;
                    added = true;
                    break;
                }
            }
            if (!added) {
                if (close(newfd) < 0) {
                    error("Error closing newfd: %s.\n", strerror(errno));
                }
            }
        }

#define CLOSE(MODBUS, SOCKET, PIPE, MAP)                                       \
    do {                                                                       \
        if (close(SOCKET) < 0) {                                               \
            error("Error closing modbus socket: %s.\n", strerror(errno));      \
        }                                                                      \
        SOCKET = -1;                                                           \
        PIPE = -1;                                                             \
        MAP = -1;                                                              \
    } while (0)

        for (int32 i = 0; i < MAX_MODBUS_CLIENTS; i += 1) {
            Payload payload;
            int32 idx;

            if (modbus_sockets[i] < 0) {
                continue;
            }

            if ((idx = map_idx[i]) < 0) {
                continue;
            }

            if (pipes[idx].revents & POLLNVAL) {
                error("Error polling: Invalid fd = %d.\n", pipes[idx].fd);
                if (BIODATA_DEBUG) {
                    exit(EXIT_FAILURE);
                }

                CLOSE(modbus, modbus_sockets[i], pipes[idx].fd, map_idx[i]);
            } else if (pipes[idx].revents & POLLHUP) {
                error("Error polling: Client closed connection.\n");

                CLOSE(modbus, modbus_sockets[i], pipes[idx].fd, map_idx[i]);
            } else if (pipes[idx].revents & POLLERR) {
                error("Error polling: Error condition.\n");
                if (BIODATA_DEBUG) {
                    exit(EXIT_FAILURE);
                }

                CLOSE(modbus, modbus_sockets[i], pipes[idx].fd, map_idx[i]);
            } else if (pipes[idx].revents & POLLIN) {
                int32 nret;

                if (modbus_set_socket(modbus, modbus_sockets[i]) < 0) {
                    error("Error in modbus_set_socket: %s.\n",
                          modbus_strerror(errno));
                    CLOSE(modbus, modbus_sockets[i], pipes[idx].fd, map_idx[i]);
                    continue;
                }

                switch (nret = modbus_receive(modbus, query)) {
                case -1:
                    error("Error in modbus_receive: %s.\n",
                          modbus_strerror(errno));
                    CLOSE(modbus, modbus_sockets[i], pipes[idx].fd, map_idx[i]);
                    break;
                case 0:
                    break;
                default:
                    if (modbus_reply(modbus, query, nret, modbus_mapping) < 0) {
                        error("Error in modbus_reply: %s.\n",
                              modbus_strerror(errno));
                        CLOSE(modbus, modbus_sockets[i], pipes[idx].fd,
                              map_idx[i]);
                        break;
                    }
                    memcpy(&payload, modbus_mapping->tab_registers,
                           sizeof(payload));
                    sub_payload_insert(pg_connection, &payload);
                    break;
                }
            }
        }
    }

#undef CLOSE

    mosquitto_destroy(mosquitto);
    PQfinish(pg_connection);

    mosquitto_lib_cleanup();

    close(modbus_listener);
    modbus_mapping_free(modbus_mapping);
    modbus_free(modbus);
    exit(EXIT_SUCCESS);
}
