#define _POSIX_C_SOURCE 200809L         /* clock_gettime y nanosleep con -std=c11 */

#include "envio.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "comandos.h"
#include "senal.h"
#include "sensor.h"

#define INTERVALO_DEFECTO_S     5
#define LATIDO_DEFECTO_S        10
#define PAUSA_ENTRE_ENVIOS_MS   250     /* sin esto el servidor responde 429 al vaciar la cola */
#define MARGEN_RETRY_AFTER_MS   500     /* se espera un poco mas de lo que pide el servidor */
#define ESPERA_503_DEFECTO_S    5       /* si el 503 no trae Retry-After */
#define ESPERA_RED_MAX_S        30      /* los reintentos por fallo de red: 1, 2, 4... hasta 30 s */
#define SIN_ENVIO               LLONG_MAX   /* la cola esta vacia: nada que enviar por ahora */
#define MAX_SUEO_MS             200     /* para revisar Ctrl+C a menudo */

/* Todo lo que el bucle necesita recordar entre vueltas. Los tiempos van en milisegundos
 * del reloj monotonico (no se ve afectado si alguien cambia la hora del sistema). */
typedef struct {
    long long t_lectura;        /* cuando toca generar la proxima lectura */
    long long t_latido;         /* cuando toca el proximo latido */
    long long t_envio;          /* cuando se puede intentar el proximo envio */
    long      intervalo_ms;
    long      latido_ms;
    int       fallas_red;       /* fallos de red seguidos, para la espera creciente */
    int       fatal;            /* 1 si hay que parar (token rechazado, base danada) */
    long long ultimo_seq;
    long long enviadas;
} Estado;

static long long ahora_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void dormir_ms(long long ms)
{
    if (ms <= 0)
        return;
    struct timespec t = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
    nanosleep(&t, NULL);                /* un Ctrl+C la interrumpe y salimos rapido */
}

/* ---------- Configuracion ---------- */

/* Lee un entero de la tabla config; si falta o es invalido usa el valor por defecto. */
static long leer_config_entero(sqlite3 *db, const char *clave, long defecto, long minimo, long maximo)
{
    char texto[32];

    if (datos_leer_config(db, clave, texto, sizeof texto) != 0)
        return defecto;

    char *fin = NULL;
    errno = 0;
    long v = strtol(texto, &fin, 10);
    if (errno != 0 || fin == texto || *fin != '\0' || v < minimo || v > maximo) {
        fprintf(stderr, "Aviso: %s='%s' no es valido, uso %ld\n", clave, texto, defecto);
        return defecto;
    }
    return v;
}

/* ---------- Lecturas ---------- */

/* Genera una lectura y la guarda en la cola con enviado = 0. */
static int generar_lectura(sqlite3 *db, Estado *e)
{
    Muestra m;
    Telemetria t;

    sensor_leer(&m);
    if (datos_siguiente_seq(db, &t.seq) != 0)
        return -1;
    t.ts = m.ts;
    t.temp = m.temp;
    t.corriente = m.corriente;
    if (datos_guardar_telemetria(db, &t) != 0)
        return -1;

    e->ultimo_seq = t.seq;
    /* Si la cola estaba vacia, ya hay algo que enviar. Si estamos esperando por un
     * 503 o por la red, esa espera se respeta: la lectura queda guardada y ya. */
    if (e->t_envio == SIN_ENVIO)
        e->t_envio = ahora_ms();
    return 0;
}

/* ---------- Envio ---------- */

/* Intenta enviar la lectura pendiente mas vieja y deja programado el proximo intento.
 * Una sola por vuelta: asi el bucle sigue atendiendo lecturas y latidos aunque la cola sea larga. */
static void enviar_uno(Red *red, sqlite3 *db, Estado *e)
{
    Telemetria t;
    char json[160];
    Respuesta resp;

    int hay = datos_telemetria_pendiente(db, &t);
    if (hay < 0) {
        e->fatal = 1;
        return;
    }
    if (hay == 0) {
        e->t_envio = SIN_ENVIO;
        return;
    }

    int n = snprintf(json, sizeof json, "{\"seq\":%lld,\"ts\":%lld,\"temp\":%.2f,\"corriente\":%.2f}",
                     t.seq, t.ts, t.temp, t.corriente);
    if (n < 0 || (size_t)n >= sizeof json) {
        fprintf(stderr, "Error: el JSON de la lectura %lld no cabe en el buffer\n", t.seq);
        e->fatal = 1;
        return;
    }

    if (datos_sumar_intento(db, t.seq) != 0) {
        e->fatal = 1;
        return;
    }

    long long ahora;
    if (red_post_json(red, "/api/v1/telemetria", json, &resp) != 0) {
        /* Sin conexion: espera creciente 1, 2, 4... hasta 30 s. La lectura sigue en la cola. */
        e->fallas_red++;
        long espera = 1L << (e->fallas_red > 5 ? 5 : e->fallas_red - 1);
        if (espera > ESPERA_RED_MAX_S)
            espera = ESPERA_RED_MAX_S;
        ahora = ahora_ms();
        e->t_envio = ahora + espera * 1000;
        fprintf(stderr, "Sin conexion con el servidor: reintento en %ld s (lectura %lld sigue en la cola)\n",
                espera, t.seq);
        return;
    }

    e->fallas_red = 0;
    ahora = ahora_ms();

    switch (resp.codigo_http) {
    case 201:
    case 200:                           /* 201 = creada, 200 = ya la tenia (duplicada): ambas valen */
        if (datos_marcar_enviada(db, t.seq) != 0) {
            e->fatal = 1;
            break;
        }
        e->enviadas++;
        printf("Lectura %lld enviada (%s)\n", t.seq, resp.codigo_http == 201 ? "creada" : "duplicada");
        e->t_envio = ahora + PAUSA_ENTRE_ENVIOS_MS;
        break;
    case 503: {
        long espera = resp.retry_after > 0 ? resp.retry_after : ESPERA_503_DEFECTO_S;
        e->t_envio = ahora + espera * 1000 + MARGEN_RETRY_AFTER_MS;
        printf("Servidor en caida (503): espero %ld s sin hacer peticiones, la cola sigue guardando\n", espera);
        break;
    }
    case 429:
        fprintf(stderr, "HTTP 429 en telemetria: voy muy rapido, espero 1 s\n");
        e->t_envio = ahora + 1000;
        break;
    case 401:
        respuesta_ok(&resp, "telemetria");
        e->fatal = 1;                   /* con un token malo no tiene caso seguir */
        break;
    default:
        respuesta_ok(&resp, "telemetria");
        e->t_envio = ahora + 5000;      /* la lectura NO se pierde: se reintenta mas tarde */
    }
    respuesta_liberar(&resp);
}

/* ---------- Latido MQTT (Nivel 4) ---------- */

/* LATIDO;<seq>;<pendientes>;<ts>. Si el broker no responde el latido se pierde: no se encola. */
static void enviar_latido(Red *red, sqlite3 *db, const Estado *e)
{
    long long pendientes = 0;
    char topico[96];
    char texto[96];

    if (datos_contar_pendientes(db, &pendientes) != 0)
        return;

    int n = snprintf(topico, sizeof topico, "itla/estudiantes/%s/latido", red->matricula);
    if (n < 0 || (size_t)n >= sizeof topico)
        return;
    n = snprintf(texto, sizeof texto, "LATIDO;%lld;%lld;%lld\n",
                 e->ultimo_seq, pendientes, (long long)time(NULL));
    if (n < 0 || (size_t)n >= sizeof texto)
        return;

    if (red_mqtt_publicar(red, topico, texto) == 0)
        printf("Latido publicado (seq %lld, %lld pendientes)\n", e->ultimo_seq, pendientes);
}

/* ---------- Bucle principal ---------- */

/* Avanza un plazo periodico; si nos atrasamos mucho, no se hacen rafagas para ponerse al dia. */
static long long siguiente_plazo(long long plazo, long periodo_ms, long long ahora)
{
    plazo += periodo_ms;
    return plazo > ahora ? plazo : ahora + periodo_ms;
}

/* Duerme hasta el plazo mas cercano, en tramos cortos para atender Ctrl+C. */
static void esperar_proximo_plazo(const Estado *e)
{
    long long proximo = e->t_lectura;

    if (e->t_latido < proximo)
        proximo = e->t_latido;
    if (e->t_envio < proximo)
        proximo = e->t_envio;

    long long falta = proximo - ahora_ms();
    dormir_ms(falta > MAX_SUEO_MS ? MAX_SUEO_MS : falta);
}

int cmd_enviar(Red *red, sqlite3 *db)
{
    Estado e;
    long long siguiente = 1;

    memset(&e, 0, sizeof e);
    setvbuf(stdout, NULL, _IOLBF, 0);   /* con la salida a un archivo, cada linea se escribe al momento */

    /* Se refresca la config del servidor; si no hay red se usa la que ya estaba guardada. */
    if (cmd_config(red, db) != 0)
        fprintf(stderr, "Aviso: no pude leer la configuracion del servidor, uso la guardada\n");
    e.intervalo_ms = leer_config_entero(db, "intervalo_s", INTERVALO_DEFECTO_S, 1, 3600) * 1000;
    e.latido_ms = leer_config_entero(db, "latido_s", LATIDO_DEFECTO_S, 1, 3600) * 1000;

    if (datos_siguiente_seq(db, &siguiente) != 0)
        return 1;
    e.ultimo_seq = siguiente - 1;       /* 0 si es la primera vez */

    long long ahora = ahora_ms();
    e.t_lectura = ahora;
    e.t_latido = ahora;
    e.t_envio = ahora;                  /* primero se vacian las pendientes de una corrida anterior */

    senal_instalar();
    printf("Enviando una lectura cada %ld s y un latido cada %ld s. Ctrl+C para terminar.\n",
           e.intervalo_ms / 1000, e.latido_ms / 1000);

    while (!senal_pedida() && !e.fatal) {
        ahora = ahora_ms();
        if (ahora >= e.t_lectura) {
            if (generar_lectura(db, &e) != 0) {
                e.fatal = 1;
                break;
            }
            e.t_lectura = siguiente_plazo(e.t_lectura, e.intervalo_ms, ahora);
        }

        ahora = ahora_ms();
        if (ahora >= e.t_latido) {
            enviar_latido(red, db, &e);
            e.t_latido = siguiente_plazo(e.t_latido, e.latido_ms, ahora_ms());
        }

        if (ahora_ms() >= e.t_envio && !senal_pedida())
            enviar_uno(red, db, &e);

        esperar_proximo_plazo(&e);
    }

    long long pendientes = 0;
    (void)datos_contar_pendientes(db, &pendientes);
    printf("\nTerminado. Enviadas en esta corrida: %lld. Pendientes en la cola: %lld.\n",
           e.enviadas, pendientes);
    return e.fatal ? 1 : 0;
}
