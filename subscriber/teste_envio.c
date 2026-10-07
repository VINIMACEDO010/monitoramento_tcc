/* teste_envio: ferramenta de teste do protótipo.
 *
 * Envia ao broker MQTT uma leitura montada à mão, no mesmo formato (Payload)
 * usado pelos publicadores. Serve para produzir situações que o funcionamento
 * normal não gera, como uma leitura fora da faixa, uma leitura repetida
 * ou uma planta nova.
 *
 * Uso:
 *   teste_envio [-p planta] [-t instante] [-n vezes] [-T temp] [-F press_fornalha]
 *               [-V vazao] [-S press_vapor]
 *
 *   -p planta    nome da planta (padrão: caldeira)
 *   -t instante  instante da leitura em segundos Unix (padrão: agora)
 *   -n vezes     quantas vezes enviar a MESMA leitura (padrão: 1)
 *   -T -F -V -S  valores das variáveis (padrão: o meio de cada faixa,
 *                1300 °C, -3,5 mmca, 11 t/h e 11 bar)
 *
 * Exemplos:
 *   teste_envio -T 1450                 (temperatura acima da faixa)
 *   teste_envio -t 1000000000 -n 3      (a mesma leitura enviada 3 vezes)
 *   teste_envio -p caldeira_teste       (planta nova)
 */
#include <errno.h>
#include <math.h>
#include <mosquitto.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "biodata.h"
#include "util.c"

static volatile bool connacked = false;
static volatile int32 published = 0;

static void
on_connect(struct mosquitto *m, void *obj, int32 rc) {
    (void)m;
    (void)obj;
    if (rc != 0) {
        error("Erro ao conectar: %s.\n", mosquitto_reason_string(rc));
        exit(EXIT_FAILURE);
    }
    connacked = true;
}

static void
on_publish(struct mosquitto *m, void *obj, int32 mid) {
    (void)m;
    (void)obj;
    (void)mid;
    published += 1;
}

static void
usage(void) {
    error("Uso: %s [-p planta] [-t instante] [-n vezes] [-T temp] "
          "[-F press_fornalha] [-V vazao] [-S press_vapor]\n", program);
    exit(EXIT_FAILURE);
}

int32
main(int32 argc, char *argv[]) {
    Payload payload;
    struct mosquitto *client;
    char *plant = "caldeira";
    int64 when = (int64)time(NULL);
    int32 times = 1;
    float values[4] = {1300.0f, -3.5f, 11.0f, 11.0f};
    int32 opt;
    int32 err;
    char *host;
    char *port;

    program = argv[0];

    while ((opt = getopt(argc, argv, "p:t:n:T:F:V:S:")) != -1) {
        switch (opt) {
        case 'p': plant = optarg; break;
        case 't': when = atoll(optarg); break;
        case 'n': times = atoi(optarg); break;
        case 'T': values[0] = strtof(optarg, NULL); break;
        case 'F': values[1] = strtof(optarg, NULL); break;
        case 'V': values[2] = strtof(optarg, NULL); break;
        case 'S': values[3] = strtof(optarg, NULL); break;
        default: usage();
        }
    }
    if (optind != argc || times < 1) {
        usage();
    }

    memset(&payload, 0, sizeof(payload));
    strncpy(payload.plant_name, plant, PLANT_NAME_MAX_LENGTH - 1);
    payload.time = when;

    /* Mesma codificação dos publicadores: (valor - índice) x 10, em int16. */
    for (int64 i = 0; i < LENGTH(template); i += 1) {
        float value = values[i];
        payload.data[i] = (int16)lroundf((value - (float)i) * 10.0f);
        error("%s = %.1f\n", template[i], (double)value);
    }

    host = xgetenv("BIODATA_HOST");
    port = getenv("MQTT_PORT");

    mosquitto_lib_init();
    client = mosquitto_new(NULL, true, NULL);
    if (client == NULL) {
        error("Erro em mosquitto_new: %s.\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    mosquitto_connect_callback_set(client, on_connect);
    mosquitto_publish_callback_set(client, on_publish);

    err = mosquitto_connect(client, host, port ? atoi(port) : 1883, 60);
    if (err != MOSQ_ERR_SUCCESS) {
        error("Erro ao conectar ao broker: %s.\n", mosquitto_strerror(err));
        exit(EXIT_FAILURE);
    }
    mosquitto_loop_start(client);

    for (int32 w = 0; !connacked; w += 1) {
        if (w > 50) {
            error("Sem resposta do broker.\n");
            exit(EXIT_FAILURE);
        }
        usleep(100000);
    }

    for (int32 k = 0; k < times; k += 1) {
        err = mosquitto_publish(client, NULL, "caldeira/dados", sizeof(payload),
                                &payload, MQTT_PUBLISHER_QOS, false);
        if (err != MOSQ_ERR_SUCCESS) {
            error("Erro ao publicar: %s.\n", mosquitto_strerror(err));
            exit(EXIT_FAILURE);
        }
        error("Enviada %d/%d | planta %s | instante %lld\n",
              k + 1, times, payload.plant_name, (long long)payload.time);
        if (k + 1 < times) {
            sleep(1);
        }
    }

    for (int32 w = 0; published < times && w < 50; w += 1) {
        usleep(100000);
    }

    mosquitto_disconnect(client);
    mosquitto_loop_stop(client, false);
    mosquitto_destroy(client);
    mosquitto_lib_cleanup();
    return published == times ? EXIT_SUCCESS : EXIT_FAILURE;
}
