#define _POSIX_C_SOURCE 200809L         /* para nanosleep con -std=c11 */

#include "descarga.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LIMITE_PAGINA           200     /* filas que pedimos por pagina */
#define MAX_PAGINAS             100     /* tope de seguridad contra bucles infinitos */
#define MAX_REINTENTOS_429      3
#define PAUSA_ENTRE_PAGINAS_MS  250     /* el servidor permite 5 peticiones por segundo */
#define CAMPOS_POR_FILA         7       /* id,maquina,ts,temp,corriente,vibracion,estado */

/* Lo que paso con las filas de una pagina. */
typedef struct {
    int       filas;        /* filas de datos que trajo la pagina */
    int       nuevas;       /* filas que no estaban en la base */
    int       repetidas;    /* filas que el PRIMARY KEY ignoro */
    int       rotas;        /* filas con formato invalido que se saltaron */
    long long id_max;       /* id mas alto que llego en la pagina */
} ResumenPagina;

static void pausa_ms(long ms)
{
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

/* ---------- Lectura del CSV ---------- */

/* Parte una linea en CAMPOS_POR_FILA campos cambiando cada coma por '\0'.
 * No se usa strtok porque se salta los campos vacios (",,") y correria las columnas.
 * Devuelve 0 si la linea tiene exactamente 7 campos. */
static int partir_linea(char *linea, char *campos[CAMPOS_POR_FILA])
{
    int n = 0;
    char *inicio = linea;

    for (;;) {
        if (n >= CAMPOS_POR_FILA)
            return -1;                  /* sobran campos */
        campos[n++] = inicio;

        char *coma = strchr(inicio, ',');
        if (coma == NULL)
            break;
        *coma = '\0';
        inicio = coma + 1;
    }
    return n == CAMPOS_POR_FILA ? 0 : -1;
}

/* Un campo vacio o con basura no es un numero valido: devuelve 0 y no toca *valor. */
static int leer_entero(const char *texto, long long *valor)
{
    char *fin = NULL;

    if (texto[0] == '\0')
        return 0;
    errno = 0;
    long long v = strtoll(texto, &fin, 10);
    if (errno != 0 || *fin != '\0')
        return 0;
    *valor = v;
    return 1;
}

static int leer_real(const char *texto, double *valor)
{
    char *fin = NULL;

    if (texto[0] == '\0')
        return 0;
    errno = 0;
    double v = strtod(texto, &fin);
    if (errno != 0 || *fin != '\0')
        return 0;
    *valor = v;
    return 1;
}

/* Convierte una linea CSV en una Lectura. El id es obligatorio; los demas
 * campos pueden faltar (quedan como NULL en la base). Devuelve 0 si sirvio. */
static int armar_lectura(char *linea, Lectura *l)
{
    char *c[CAMPOS_POR_FILA];

    memset(l, 0, sizeof *l);
    if (partir_linea(linea, c) != 0 || !leer_entero(c[0], &l->id))
        return -1;

    l->maquina = c[1][0] ? c[1] : NULL;
    l->tiene_ts = leer_entero(c[2], &l->ts);
    l->tiene_temp = leer_real(c[3], &l->temp);
    l->tiene_corriente = leer_real(c[4], &l->corriente);
    l->tiene_vibracion = leer_real(c[5], &l->vibracion);
    l->estado = c[6][0] ? c[6] : NULL;
    return 0;
}

static void quitar_retorno(char *linea)
{
    size_t largo = strlen(linea);
    if (largo > 0 && linea[largo - 1] == '\r')
        linea[largo - 1] = '\0';
}

/* ---------- Guardado ---------- */

static int guardar_fila(sqlite3 *db, sqlite3_stmt *st, char *linea, ResumenPagina *r)
{
    Lectura l;

    r->filas++;
    if (armar_lectura(linea, &l) != 0) {
        r->rotas++;
        fprintf(stderr, "Aviso: una fila con formato roto se salto\n");
        return 0;                       /* una fila mala no detiene la descarga */
    }

    int rc = datos_insertar_lectura(db, st, &l);
    if (rc < 0)
        return -1;
    if (rc == 1)
        r->nuevas++;
    else
        r->repetidas++;
    if (l.id > r->id_max)
        r->id_max = l.id;
    return 0;
}

/* Recorre el CSV linea por linea (salta la cabecera "id,...") y guarda cada fila. */
static int guardar_filas(sqlite3 *db, sqlite3_stmt *st, char *csv, ResumenPagina *r)
{
    char *linea = csv;

    memset(r, 0, sizeof *r);
    while (linea != NULL && *linea != '\0') {
        char *fin = strchr(linea, '\n');
        if (fin != NULL)
            *fin = '\0';
        quitar_retorno(linea);

        if (linea[0] != '\0' && strncmp(linea, "id,", 3) != 0 &&
            guardar_fila(db, st, linea, r) != 0)
            return -1;

        linea = (fin != NULL) ? fin + 1 : NULL;
    }
    return 0;
}

/* Una transaccion por pagina: la red se usa fuera de ella, asi la base queda
 * bloqueada solo el instante de guardar (enviar y escuchar tambien la usan). */
static int procesar_pagina(sqlite3 *db, sqlite3_stmt *st, char *csv, ResumenPagina *r)
{
    if (datos_comenzar(db) != 0)
        return -1;
    if (guardar_filas(db, st, csv, r) != 0 || datos_confirmar(db) != 0) {
        datos_revertir(db);
        return -1;
    }
    return 0;
}

/* ---------- Peticiones ---------- */

/* Devuelve 1 si el codigo fue 200; si no, explica el motivo y devuelve 0. */
static int codigo_ok(const Respuesta *resp, const char *que)
{
    switch (resp->codigo_http) {
    case 200:
        return 1;
    case 400:
        fprintf(stderr, "HTTP 400 en %s: el servidor no entendio la peticion\n", que);
        break;
    case 401:
        fprintf(stderr, "HTTP 401 en %s: token rechazado. Revisa ITLA_TOKEN e ITLA_MATRICULA\n", que);
        break;
    case 429:
        fprintf(stderr, "HTTP 429 en %s: demasiadas peticiones por segundo\n", que);
        break;
    default:
        fprintf(stderr, "HTTP %ld inesperado en %s\n", resp->codigo_http, que);
    }
    return 0;
}

/* Pide una pagina. Si el servidor responde 429 espera 1 s y reintenta. */
static int pedir_pagina(Red *red, long long desde_id, Respuesta *resp)
{
    char ruta[96];
    int n = snprintf(ruta, sizeof ruta, "/api/v1/lecturas?desde_id=%lld&limite=%d",
                     desde_id, LIMITE_PAGINA);
    if (n < 0 || (size_t)n >= sizeof ruta)
        return -1;

    for (int intento = 0; ; intento++) {
        if (red_get(red, ruta, resp) != 0)
            return -1;
        if (resp->codigo_http != 429 || intento >= MAX_REINTENTOS_429)
            break;
        fprintf(stderr, "HTTP 429: voy muy rapido, espero 1 s y reintento\n");
        respuesta_liberar(resp);
        pausa_ms(1000);
    }

    if (!codigo_ok(resp, "lecturas")) {
        respuesta_liberar(resp);
        return -1;
    }
    return 0;
}

/* Baja paginas hasta que una traiga menos de LIMITE_PAGINA filas. */
static int descargar_paginas(Red *red, sqlite3 *db, sqlite3_stmt *st)
{
    long long desde_id = 0;

    for (int pagina = 1; pagina <= MAX_PAGINAS; pagina++) {
        Respuesta resp;
        ResumenPagina r;

        if (pedir_pagina(red, desde_id, &resp) != 0)
            return -1;
        int rc = procesar_pagina(db, st, resp.datos, &r);
        respuesta_liberar(&resp);
        if (rc != 0)
            return -1;

        printf("Pagina %2d: %3d filas (%d nuevas, %d repetidas)\n",
               pagina, r.filas, r.nuevas, r.repetidas);

        if (r.filas < LIMITE_PAGINA)
            return 0;                   /* ultima pagina */
        if (r.id_max <= desde_id) {
            fprintf(stderr, "Error: la pagina %d no avanzo (id %lld), me detengo\n",
                    pagina, desde_id);
            return -1;
        }
        desde_id = r.id_max;
        pausa_ms(PAUSA_ENTRE_PAGINAS_MS);
    }

    fprintf(stderr, "Error: pasaron %d paginas sin terminar, me detengo\n", MAX_PAGINAS);
    return -1;
}

/* Cuenta con SQL y envia {"filas_unicas":N,"suma_ids":S} al servidor. */
static int enviar_resultado(Red *red, sqlite3 *db)
{
    long long filas = 0, suma = 0;
    char json[96];

    if (datos_resumen_lecturas(db, &filas, &suma) != 0)
        return -1;
    printf("En la base: %lld filas unicas, suma de ids %lld\n", filas, suma);

    int n = snprintf(json, sizeof json, "{\"filas_unicas\":%lld,\"suma_ids\":%lld}", filas, suma);
    if (n < 0 || (size_t)n >= sizeof json)
        return -1;

    Respuesta resp;
    if (red_post_json(red, "/api/v1/respuestas/descarga", json, &resp) != 0)
        return -1;

    int correcto = codigo_ok(&resp, "respuestas/descarga") &&
                   strstr(resp.datos, "correcto=1") != NULL;
    respuesta_liberar(&resp);

    if (correcto)
        printf("El servidor confirmo la descarga (correcto=1)\n");
    else
        fprintf(stderr, "El servidor no confirmo la descarga (correcto=0)\n");
    return correcto ? 0 : -1;
}

int cmd_descargar(Red *red, sqlite3 *db)
{
    sqlite3_stmt *st = NULL;

    if (datos_preparar_insertar_lectura(db, &st) != 0)
        return 1;

    int rc = descargar_paginas(red, db, st);
    datos_finalizar(st);
    if (rc != 0) {
        fprintf(stderr, "La descarga no termino. Puedes repetir el comando: no duplica filas.\n");
        return 1;
    }

    return enviar_resultado(red, db) == 0 ? 0 : 1;
}
