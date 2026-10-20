/* carga: gerador de carga para a avaliacao A3 (nao faz parte do prototipo).
 * Publica no topico caldeira/dados, no mesmo formato Payload dos publicadores,
 * leituras de N plantas (caldeira_c01, c02, ...) a cada I milissegundos, por D segundos.
 * Uso: carga -n plantas -i intervalo_ms -d duracao_s */
#include <errno.h>
#include <math.h>
#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "biodata.h"
#include "util.c"

static volatile bool connacked = false;
static volatile long publicadas = 0;
static void on_connect(struct mosquitto *m, void *o, int32 rc) { (void)m; (void)o; if (rc == 0) connacked = true; }
static void on_publish(struct mosquitto *m, void *o, int32 mid) { (void)m; (void)o; (void)mid; publicadas += 1; }
static double agora(void) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t); return (double)t.tv_sec + t.tv_nsec / 1e9; }

int32 main(int32 argc, char *argv[]) {
    int32 n = 10, intervalo = 1000, duracao = 600, opt;
    program = argv[0];
    while ((opt = getopt(argc, argv, "n:i:d:")) != -1) {
        if (opt == 'n') n = atoi(optarg); else if (opt == 'i') intervalo = atoi(optarg); else if (opt == 'd') duracao = atoi(optarg);
    }
    float lo[4] = {1200, -7, 10, 13}, hi[4] = {1400, 0, 15, 15};
    mosquitto_lib_init();
    struct mosquitto *c = mosquitto_new(NULL, true, NULL);
    mosquitto_connect_callback_set(c, on_connect);
    mosquitto_publish_callback_set(c, on_publish);
    mosquitto_max_inflight_messages_set(c, 0);
    if (mosquitto_connect(c, getenv("BIODATA_HOST"), 1883, 60) != MOSQ_ERR_SUCCESS) { error("sem broker\n"); return 1; }
    mosquitto_loop_start(c);
    while (!connacked) usleep(10000);
    srand((unsigned)time(NULL));
    double inicio = agora(), proximo = inicio;
    long enviadas = 0;
    while (agora() - inicio < duracao) {
        int64 t = (int64)time(NULL);
        for (int32 p = 1; p <= n; p++) {
            Payload pl; memset(&pl, 0, sizeof(pl));
            snprintf(pl.plant_name, PLANT_NAME_MAX_LENGTH, "caldeira_c%02d", p);
            pl.time = t;
            for (int32 i = 0; i < 4; i++) {
                float v = lo[i] + (hi[i] - lo[i]) * (float)rand() / (float)RAND_MAX;
                v = roundf(v * 10.0f) / 10.0f;
                pl.data[i] = (int16)lroundf((v - (float)i) * 10.0f);
            }
            if (mosquitto_publish(c, NULL, "caldeira/dados", sizeof(pl), &pl, 1, false) == MOSQ_ERR_SUCCESS) enviadas++;
        }
        proximo += intervalo / 1000.0;
        double espera = proximo - agora();
        if (espera > 0) usleep((useconds_t)(espera * 1e6));
    }
    sleep(3);
    printf("enviadas=%ld confirmadas_pelo_broker=%ld\n", enviadas, publicadas);
    mosquitto_loop_stop(c, true);
    return 0;
}
