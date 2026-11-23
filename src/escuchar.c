/**************************************************************************/
/*  Programación para mecatrónicos                                        */
/*  Nombre:    Bienderl Gonzalez                                          */
/*  Matricula: 2024-1302                                                  */
/*  Seccion:   Miercoles                                                  */
/*  Practica:  Estacion de Telemetria ITLA                                */
/*  Fecha:     23/11/2026                                                 */
/*  Link Practica: https://github.com/gonzalezbienderl01-arch/estacion    */
/**************************************************************************/

#define _POSIX_C_SOURCE 200809L         /* clock_gettime y nanosleep con -std=c11 */

#include "escuchar.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "senal.h"

#define MAX_BUFFER_MQTT         4096
#define REINTENTO_ACK_S         2       /* cada cuanto se reintenta una confirmacion que fallo */
#define MAX_REINTENTOS_POR_VUELTA 8
#define ESPERA_RECONEXION_MAX_S 30

/* Todo lo que comparten los callbacks de la suscripcion. */
typedef struct {
    Red      *red;
    sqlite3  *db;
    char      buf[MAX_BUFFER_MQTT]; /* bytes recibidos que todavia no forman un mensaje completo */
    size_t    len;
    long long ultimo_reintento;     /* reloj monotonico, en segundos */
} Contexto;

static long long segundos_monotonico(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec;
}

/* ---------- Confirmar una alarma por HTTP ---------- */

/* Un codigo valido son solo letras y numeros: asi se puede armar el JSON con seguridad. */
static int codigo_valido(const char *codigo)
{
    size_t n = strlen(codigo);

    if (n == 0 || n >= ALARMA_CODIGO_MAX)
        return 0;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)codigo[i]))
            return 0;
    return 1;
}

/* POST /alarmas/ack. Devuelve 1 si la alarma quedo confirmada, 0 si hay que reintentar.
 * Se usa el handle HTTP normal: nunca el de la suscripcion. */
static int confirmar(Contexto *c, const char *codigo)
{
    char json[64];
    Respuesta resp;

    int n = snprintf(json, sizeof json, "{\"codigo\":\"%s\"}", codigo);
    if (n < 0 || (size_t)n >= sizeof json)
        return 0;
    if (red_post_json(c->red, "/api/v1/alarmas/ack", json, &resp) != 0)
        return 0;                       /* sin red: queda pendiente y se reintenta */

    int confirmada = 0;
    if (resp.codigo_http == 200) {
        printf("Alarma %s confirmada (%.*s)\n", codigo, (int)strcspn(resp.datos, "\r\n"), resp.datos);
        confirmada = 1;
    } else if (resp.codigo_http == 409) {
        printf("Alarma %s: el servidor ya la tenia confirmada\n", codigo);
        confirmada = 1;
    } else if (resp.codigo_http == 404) {
        fprintf(stderr, "Alarma %s: el servidor dice que no existe (404)\n", codigo);
    } else {
        respuesta_ok(&resp, "alarmas/ack");
    }
    respuesta_liberar(&resp);

    if (confirmada && datos_confirmar_alarma(c->db, codigo, (long long)time(NULL)) != 0)
        return 0;
    return confirmada;
}

/* Reintenta las alarmas guardadas que todavia no se pudieron confirmar. */
static void reintentar_pendientes(Contexto *c)
{
    char codigos[MAX_REINTENTOS_POR_VUELTA][ALARMA_CODIGO_MAX];
    int n = datos_alarmas_sin_confirmar(c->db, codigos, MAX_REINTENTOS_POR_VUELTA);

    for (int i = 0; i < n; i++)
        (void)confirmar(c, codigos[i]);
}

/* ---------- Interpretar los mensajes ---------- */

/* Parte "ALARMA;codigo;maquina;ts" en sus tres campos (cambia los ';' por '\0').
 * Se usa strchr y no strtok, por el mismo motivo que en el CSV. Devuelve 0 si sirvio. */
static int partir_alarma(char *texto, char **codigo, char **maquina, long long *ts)
{
    char *p = strchr(texto, ';');
    if (p == NULL)
        return -1;
    *codigo = p + 1;

    p = strchr(*codigo, ';');
    if (p == NULL)
        return -1;
    *p = '\0';
    *maquina = p + 1;

    p = strchr(*maquina, ';');
    if (p == NULL)
        return -1;
    *p = '\0';

    char *fin = NULL;
    errno = 0;
    *ts = strtoll(p + 1, &fin, 10);
    if (errno != 0 || fin == p + 1 || *fin != '\0')
        return -1;
    return 0;
}

/* Una alarma nueva se guarda y se confirma; una ya confirmada se ignora (el broker la
 * retiene y la vuelve a entregar si la conexion se cae); una guardada pero sin confirmar
 * se vuelve a intentar. */
static void procesar_alarma(Contexto *c, char *texto)
{
    char *codigo, *maquina;
    long long ts_servidor;
    int existe = 0, confirmada = 0;

    if (partir_alarma(texto, &codigo, &maquina, &ts_servidor) != 0 || !codigo_valido(codigo)) {
        fprintf(stderr, "Aviso: mensaje de alarma con formato raro, lo ignoro\n");
        return;
    }
    if (datos_alarma_estado(c->db, codigo, &existe, &confirmada) != 0)
        return;
    if (confirmada)
        return;                         /* ya la confirmamos antes: se ignora */

    if (!existe) {
        printf("Alarma nueva %s en la maquina %s\n", codigo, maquina);
        if (datos_guardar_alarma(c->db, codigo, maquina, ts_servidor, (long long)time(NULL)) != 0)
            return;
    }
    (void)confirmar(c, codigo);
}

static void procesar_mensaje(Contexto *c, char *texto)
{
    if (strncmp(texto, "ALARMA;", 7) == 0)
        procesar_alarma(c, texto);
    /* "NINGUNA;<ts>" significa que no hay alarma pendiente: no hay nada que hacer. */
}

/* libcurl no entrega texto sino bytes con formato fijo: 2 bytes con el largo del topico
 * (el primero es el byte alto), el topico y el contenido. Varios mensajes pueden llegar
 * pegados en una sola llamada y uno puede llegar partido en dos, por eso se acumula en
 * c->buf y solo se procesa lo que ya esta completo (cada contenido termina en '\n'). */
static size_t al_recibir_mqtt(char *ptr, size_t size, size_t nmemb, void *ud)
{
    Contexto *c = ud;
    size_t n = size * nmemb;

    if (n > sizeof c->buf - c->len) {   /* no cabe: se descarta lo acumulado */
        c->len = 0;
        if (n > sizeof c->buf)
            return n;
    }
    memcpy(c->buf + c->len, ptr, n);
    c->len += n;

    for (;;) {
        if (c->len < 2)
            break;
        size_t largo_topico = ((size_t)(unsigned char)c->buf[0] << 8) | (unsigned char)c->buf[1];
        if (c->len < 2 + largo_topico)
            break;
        char *contenido = c->buf + 2 + largo_topico;
        char *fin = memchr(contenido, '\n', c->len - 2 - largo_topico);
        if (fin == NULL)
            break;                      /* el mensaje todavia no llego entero */
        *fin = '\0';
        procesar_mensaje(c, contenido);

        size_t usado = (size_t)(fin - c->buf) + 1;
        memmove(c->buf, c->buf + usado, c->len - usado);
        c->len -= usado;
    }
    return n;                           /* siempre size * nmemb */
}

/* libcurl lo llama a cada rato mientras la suscripcion esta viva. Devolver 1 la termina. */
static int al_progresar(void *ud, curl_off_t dt, curl_off_t dn, curl_off_t ut, curl_off_t un)
{
    Contexto *c = ud;
    (void)dt; (void)dn; (void)ut; (void)un;

    if (senal_pedida())
        return 1;                       /* Ctrl+C: que perform regrese */

    /* Si una confirmacion fallo (por ejemplo el servidor se cayo), el broker no vuelve a
     * mandar la alarma mientras la conexion siga viva: hay que reintentarla desde aqui. */
    long long ahora = segundos_monotonico();
    if (ahora - c->ultimo_reintento >= REINTENTO_ACK_S) {
        c->ultimo_reintento = ahora;
        reintentar_pendientes(c);
    }
    return 0;
}

/* ---------- Bucle principal ---------- */

/* Espera en tramos de un segundo para poder atender Ctrl+C. */
static void esperar_s(int segundos)
{
    for (int i = 0; i < segundos && !senal_pedida(); i++) {
        struct timespec t = { 1, 0 };
        nanosleep(&t, NULL);
    }
}

int cmd_escuchar(Red *red, sqlite3 *db)
{
    static Contexto c;                  /* estatico: el buffer es grande */
    char topico[96];

    memset(&c, 0, sizeof c);
    c.red = red;
    c.db = db;

    int n = snprintf(topico, sizeof topico, "itla/planta/%s/alarma", red->matricula);
    if (n < 0 || (size_t)n >= sizeof topico) {
        fprintf(stderr, "Error: la matricula es demasiado larga para el topico\n");
        return 1;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    senal_instalar();
    printf("Escuchando alarmas en %s. Ctrl+C para terminar.\n", topico);

    int espera = 1;
    while (!senal_pedida()) {
        c.len = 0;
        long long inicio = segundos_monotonico();
        c.ultimo_reintento = inicio;
        reintentar_pendientes(&c);      /* por si quedaron alarmas sin confirmar de antes */

        red_mqtt_suscribir(red, topico, al_recibir_mqtt, &c, al_progresar, &c);
        if (senal_pedida())
            break;

        /* La conexion se corto: se reconecta con espera creciente (1, 2, 4... hasta 30 s).
         * La alarma pendiente esta retenida en el broker, asi que no se pierde. */
        if (segundos_monotonico() - inicio >= 10)
            espera = 1;                 /* estuvo bien un rato: se empieza de nuevo */
        printf("Reconecto en %d s...\n", espera);
        esperar_s(espera);
        espera = espera * 2 > ESPERA_RECONEXION_MAX_S ? ESPERA_RECONEXION_MAX_S : espera * 2;
    }

    printf("\nTerminado.\n");
    return 0;
}
