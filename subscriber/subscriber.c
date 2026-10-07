#include <arpa/inet.h>
#include <errno.h>
#include <libpq-fe.h>
#include <modbus/modbus.h>
#include <mosquitto.h>
#include <poll.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>

#include "biodata.h"
#include "util.c"

#define MAX_MODBUS_CLIENTS 100

static char *POSTGRES_HOST;
static char *POSTGRES_PORT;
static char *POSTGRES_USER;
static char *POSTGRES_DB;
static char *POSTGRES_PASSWORD;

static bool run = true;

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
}

void
sub_mqtt_connected(struct mosquitto *mosquitto, void *user_data, int32 result) {
    (void)user_data;
    if (result != 0) {
        error("Broker refused connection: %s.\n", mosquitto_connack_string(result));
        return;
    }
    error("Connected to mosquitto broker.\n");
    /* Refaz a assinatura a cada conexão, inclusive depois de uma reconexão. */
    if (mosquitto_subscribe(mosquitto, NULL, MQTT_SUBSCRIBER_TOPIC,
                            MQTT_SUBSCRIBER_QOS) != MOSQ_ERR_SUCCESS) {
        error("Error subscribing to topic \"%s\".\n", MQTT_SUBSCRIBER_TOPIC);
    }
}

void
sub_mqtt_disconnected(struct mosquitto *mosquitto, void *user_data,
                      int32 result) {
    (void)mosquitto;
    (void)user_data;
    (void)result;
    error("Disconnected from mosquitto broker.\n");
}

ExecStatusType
sub_pg_simple(PGconn *pg_connection, const char *command) {
    PGresult *pg_result;
    ExecStatusType status;

    pg_result = PQexec(pg_connection, command);
    status = PQresultStatus(pg_result);

    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        error("Error running SQL query '\n%s\n': %s.\n",
              command, PQresultErrorMessage(pg_result));
    }

    PQclear(pg_result);
    return status;
}

static bool
sub_payload_insert_once(PGconn *pg_connection, Payload *payload) {
    char sql_query[2048] = {0};
    char sql_columns[512] = {0};
    char sql_values[512] = {0};
    char sql_add_columns[1024] = {0};
    static Regex regex_table_name = {0};

    int32 n0 = 0;
    int32 n1 = 0;
    int32 n2 = 0;
    int32 m = 0;
    int32 space;

    char time_str[64];
    char value_str[LENGTH(template)][64];
    const char *param_values[NPARAMS];

    error("%ld: %s = %d\n", payload->time, payload->plant_name, payload->data[0]);

    if (regex_table_name.string == NULL) {
        regex_table_name.string = "^[A-Za-z_][A-Za-z0-9_]*$";
        util_compile_regex(&regex_table_name);
    }

    if (!REGEX_MATCH_SIMPLE(regex_table_name, payload->plant_name)) {
        error("Invalid plant name: %s\n", payload->plant_name);
        return false;
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
             "CREATE TABLE IF NOT EXISTS %s ("
             "time int8 not null,"
             "primary key (time)"
             ");",
             payload->plant_name);

    if (sub_pg_simple(pg_connection, sql_query) != PGRES_COMMAND_OK) {
        error("Error creating table.\n");
        return false;
    }

    if (n1 > 0) {
        size_t len_add = strlen(sql_add_columns);

        if (len_add >= 2 && sql_add_columns[len_add - 2] == ',') {
            sql_add_columns[len_add - 2] = '\n';
        }

        SNPRINTF(sql_query,
                 "ALTER TABLE %s\n%s;",
                 payload->plant_name,
                 sql_add_columns);

        if (sub_pg_simple(pg_connection, sql_query) != PGRES_COMMAND_OK) {
            error("Error adding columns to table.\n");
            return false;
        }
    }

    snprintf(time_str, sizeof(time_str), "%lld", (long long)payload->time);
    param_values[0] = time_str;

    for (int64 i = 0; i < LENGTH(payload->data); i += 1) {
        float convert = ((float)(payload->data[i]) / 10.0f) + (float)i;
        snprintf(value_str[i], sizeof(value_str[i]), "%.2f", convert);
        param_values[i + 1] = value_str[i];
    }

    SNPRINTF(sql_query,
             "INSERT INTO %s (time%s) VALUES ($1%s) "
             "ON CONFLICT (time) DO NOTHING;",
             payload->plant_name, sql_columns, sql_values);

    {
        PGresult *pg_result;
        ExecStatusType status;

        pg_result = PQexecParams(pg_connection,
                                 sql_query,
                                 NPARAMS,
                                 NULL,
                                 param_values,
                                 NULL,
                                 NULL,
                                 0);
        status = PQresultStatus(pg_result);

        if (status != PGRES_COMMAND_OK) {
            error("Error running SQL query '\n%s\n': %s.\n",
                  sql_query, PQresultErrorMessage(pg_result));
        }

        PQclear(pg_result);
        return status == PGRES_COMMAND_OK;
    }
}

/* Garante que a conexão com o PostgreSQL está ativa, reconectando se preciso. */
static bool
sub_pg_ensure(PGconn *pg_connection) {
    if (PQstatus(pg_connection) == CONNECTION_OK) {
        return true;
    }
    error("Lost connection to postgres database. Reconnecting...\n");
    PQreset(pg_connection);
    if (PQstatus(pg_connection) != CONNECTION_OK) {
        error("Reconnection to postgres failed: %s", PQerrorMessage(pg_connection));
        return false;
    }
    error("Reconnected to postgres database.\n");
    return true;
}

static void
sub_payload_insert(PGconn *pg_connection, Payload *payload) {
    if (!sub_pg_ensure(pg_connection)) {
        error("Reading discarded: database unavailable.\n");
        return;
    }
    /* Se a gravação falhar porque a conexão caiu, reconecta e tenta mais uma vez. */
    if (!sub_payload_insert_once(pg_connection, payload)
        && PQstatus(pg_connection) != CONNECTION_OK
        && sub_pg_ensure(pg_connection)) {
        sub_payload_insert_once(pg_connection, payload);
    }
}

void
sub_mqtt_callback(struct mosquitto *mosquitto, void *user_data,
                  const struct mosquitto_message *message) {
    PGconn *pg_connection = user_data;
    Payload payload;

    (void)mosquitto;

    if (message->payloadlen != sizeof(payload)) {
        error("Received payload length (%d) does not match template size (%zu).\n",
              message->payloadlen, sizeof(payload));
        return;
    }

    memcpy(&payload, message->payload, sizeof(payload));
    sub_payload_insert(pg_connection, &payload);
}

static void
xmosquitto_reconnect(struct mosquitto *mosquitto) {
    int32 err;
    if ((err = mosquitto_reconnect(mosquitto)) != MOSQ_ERR_SUCCESS) {
        /* Broker indisponível: tenta de novo na próxima volta do laço. */
        error("Error reconnecting to broker: %s. Retrying...\n", mosquitto_strerror(err));
        sleep(1);
    }
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
    int32 mqtt_port;
    int32 mqtt_keepalive;

    struct passwd *sub_pw;

    (void)argc;
    program = argv[0];

    if (geteuid() == 0) {
        if ((sub_pw = getpwnam("subscriber")) == NULL) {
            error("Error getting uid for user subscriber: %s.\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (setuid(sub_pw->pw_uid) < 0) {
            error("Error setting uid to %d: %s.\n", sub_pw->pw_uid, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    memset(&modbus_sockets, -1, sizeof(modbus_sockets));

    POSTGRES_HOST = xgetenv("POSTGRES_HOST");
    POSTGRES_PORT = xgetenv("POSTGRES_PORT");
    POSTGRES_USER = xgetenv("POSTGRES_USER");
    POSTGRES_DB = xgetenv("POSTGRES_DB");
    POSTGRES_PASSWORD = xgetenv("POSTGRES_PASSWORD");

    SNPRINTF(pg_config_string,
             "host=%s port=%s user=%s password=%s dbname=%s connect_timeout=3",
             POSTGRES_HOST,
             POSTGRES_PORT,
             POSTGRES_USER,
             POSTGRES_PASSWORD,
             POSTGRES_DB);

    error("Postgres database parameters:\n%s\n", pg_config_string);

    while (true) {
        sleep(1);
        pg_connection = PQconnectdb(pg_config_string);
        if (PQstatus(pg_connection) == CONNECTION_BAD) {
            error("Error connecting to postgres database: %s",
                  PQerrorMessage(pg_connection));
            PQfinish(pg_connection);
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

        mqtt_port = atoi(MQTT_PORT);
        mqtt_keepalive = atoi(MQTT_KEEPALIVE);

        SNPRINTF(mosquitto_client_id, "sub_%d", getpid());

        mosquitto_lib_init();
        mosquitto = mosquitto_new(mosquitto_client_id, clean_session, pg_connection);
        if (!mosquitto) {
            error("Error creating new mosquitto connection: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    mosquitto_connect_callback_set(mosquitto, sub_mqtt_connected);
    mosquitto_disconnect_callback_set(mosquitto, sub_mqtt_disconnected);
    mosquitto_message_callback_set(mosquitto, sub_mqtt_callback);

    error("Connecting to %s:%d with timeout %ds.\n",
          MQTT_HOST, mqtt_port, mqtt_keepalive);

    mosq_errno = mosquitto_connect(mosquitto, MQTT_HOST, mqtt_port, mqtt_keepalive);
    if (mosq_errno != MOSQ_ERR_SUCCESS) {
        error("Error connecting to mqtt broker: %s\n", mosquitto_strerror(mosq_errno));
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

    modbus = modbus_new_tcp("0.0.0.0", MODBUS_SERVER_PORT);
    if (modbus == NULL) {
        error("Error in modbus_new_tcp: %s.\n", modbus_strerror(errno));
        exit(EXIT_FAILURE);
    }

    modbus_mapping = modbus_mapping_new(0, 0, MODBUS_NREGS, 0);
    if (modbus_mapping == NULL) {
        error("Error in modbus_mapping_new: %s.\n", modbus_strerror(errno));
        modbus_free(modbus);
        exit(EXIT_FAILURE);
    }

    modbus_listener = modbus_tcp_listen(modbus, MAX_MODBUS_CLIENTS);
    if (modbus_listener < 0) {
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
            error("Error in mosquitto_loop_write: %s.\n", mosquitto_strerror(err));
            if (err == MOSQ_ERR_NO_CONN || err == MOSQ_ERR_CONN_LOST) {
                xmosquitto_reconnect(mosquitto);
            }
        }

        if ((err = mosquitto_loop_misc(mosquitto)) != MOSQ_ERR_SUCCESS) {
            error("Error in mosquitto_loop_misc: %s.\n", mosquitto_strerror(err));
            if (err == MOSQ_ERR_NO_CONN || err == MOSQ_ERR_CONN_LOST) {
                xmosquitto_reconnect(mosquitto);
            }
        }

        mqtt_fd = mosquitto_socket(mosquitto);
        if (mqtt_fd < 0) {
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

        npolled = poll(pipes, nfds, poll_timeout);
        if (npolled == 0) {
            continue;
        }
        if (npolled < 0) {
            if (errno == EINTR) {
                continue;
            }
            error("Error in polling: %s.\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        idx0 = 0;
        if (mqtt_fd >= 0 && (pipes[idx0].revents & POLLIN)) {
            if ((err = mosquitto_loop_read(mosquitto, 1)) != MOSQ_ERR_SUCCESS) {
                error("Error in mosquitto_loop_read: %s.\n", mosquitto_strerror(err));
                if (err == MOSQ_ERR_NO_CONN || err == MOSQ_ERR_CONN_LOST) {
                    xmosquitto_reconnect(mosquitto);
                    continue;
                }
            }
        }
        idx0 += 1;

        if (modbus_listener >= 0 && (pipes[idx0].revents & POLLIN)) {
            struct sockaddr_in clientaddr;
            socklen_t addrlen = sizeof(clientaddr);
            int32 newfd;
            bool added = false;

            newfd = accept(modbus_listener, (struct sockaddr *)&clientaddr, &addrlen);
            if (newfd < 0) {
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
                close(newfd);
            }
        }

#define CLOSE_SOCKET(SOCKET, PIPE, MAP) \
    do { \
        if (close(SOCKET) < 0) { \
            error("Error closing modbus socket: %s.\n", strerror(errno)); \
        } \
        SOCKET = -1; \
        PIPE = -1; \
        MAP = -1; \
    } while (0)

        for (int32 i = 0; i < MAX_MODBUS_CLIENTS; i += 1) {
            Payload payload;
            int32 idx;

            if (modbus_sockets[i] < 0) {
                continue;
            }

            idx = map_idx[i];
            if (idx < 0) {
                continue;
            }

            if (pipes[idx].revents & (POLLNVAL | POLLHUP | POLLERR)) {
                CLOSE_SOCKET(modbus_sockets[i], pipes[idx].fd, map_idx[i]);
                continue;
            }

            if (pipes[idx].revents & POLLIN) {
                int32 nret;

                if (modbus_set_socket(modbus, modbus_sockets[i]) < 0) {
                    error("Error in modbus_set_socket: %s.\n", modbus_strerror(errno));
                    CLOSE_SOCKET(modbus_sockets[i], pipes[idx].fd, map_idx[i]);
                    continue;
                }

                nret = modbus_receive(modbus, query);
                if (nret == -1) {
                    error("Error in modbus_receive: %s.\n", modbus_strerror(errno));
                    CLOSE_SOCKET(modbus_sockets[i], pipes[idx].fd, map_idx[i]);
                    continue;
                }

                if (nret > 0) {
                    if (modbus_reply(modbus, query, nret, modbus_mapping) < 0) {
                        error("Error in modbus_reply: %s.\n", modbus_strerror(errno));
                        CLOSE_SOCKET(modbus_sockets[i], pipes[idx].fd, map_idx[i]);
                        continue;
                    }

                    memcpy(&payload, modbus_mapping->tab_registers, sizeof(payload));
                    sub_payload_insert(pg_connection, &payload);
                }
            }
        }

#undef CLOSE_SOCKET
    }

    mosquitto_destroy(mosquitto);
    PQfinish(pg_connection);
    mosquitto_lib_cleanup();

    close(modbus_listener);
    modbus_mapping_free(modbus_mapping);
    modbus_free(modbus);

    return EXIT_SUCCESS;
}