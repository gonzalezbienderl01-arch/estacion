/**************************************************************************/
/*  Programación para mecatrónicos                                        */
/*  Nombre:    Bienderl Gonzalez                                          */
/*  Matricula: 2024-1302                                                  */
/*  Seccion:   Miercoles                                                  */
/*  Practica:  Estacion de Telemetria ITLA                                */
/*  Fecha:     23/11/2026                                                 */
/*  Link Practica: https://github.com/gonzalezbienderl01-arch/estacion    */
/**************************************************************************/

#include "analisis.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Lee umbral_temp de la tabla config (la guarda el comando config). */
static int leer_umbral(sqlite3 *db, double *umbral)
{
    char texto[64];
    int rc = datos_leer_config(db, "umbral_temp", texto, sizeof texto);

    if (rc == 1) {
        fprintf(stderr, "Falta umbral_temp en la base. Corre primero: ./estacion config\n");
        return -1;
    }
    if (rc != 0)
        return -1;

    char *fin = NULL;
    errno = 0;
    double v = strtod(texto, &fin);
    if (errno != 0 || fin == texto || *fin != '\0') {
        fprintf(stderr, "umbral_temp tiene un valor invalido: '%s'\n", texto);
        return -1;
    }
    *umbral = v;
    return 0;
}

static void mostrar(const Analisis *a, int n, double umbral)
{
    printf("Umbral de temperatura: %.2f\n", umbral);
    printf("%-8s %7s %11s %9s %9s %10s %13s\n",
           "maquina", "validas", "descartadas", "temp_min", "temp_max", "temp_prom", "sobre_umbral");
    for (int i = 0; i < n; i++)
        printf("%-8s %7lld %11lld %9.2f %9.2f %10.2f %13lld\n",
               a[i].maquina, a[i].validas, a[i].descartadas,
               a[i].temp_min, a[i].temp_max, a[i].temp_prom, a[i].sobre_umbral);
}

/* Arma {"maquinas":[{...},{...}]} con snprintf. Devuelve 0 si cupo en el buffer.
 * Cada vuelta comprueba el espacio que queda, para no escribir fuera del arreglo. */
static int armar_json(const Analisis *a, int n, char *json, size_t tam)
{
    size_t usado = 0;
    int w = snprintf(json, tam, "{\"maquinas\":[");

    if (w < 0 || (size_t)w >= tam)
        return -1;
    usado = (size_t)w;

    for (int i = 0; i < n; i++) {
        w = snprintf(json + usado, tam - usado,
                     "%s{\"maquina\":\"%s\",\"validas\":%lld,\"descartadas\":%lld,"
                     "\"temp_min\":%.2f,\"temp_max\":%.2f,\"temp_prom\":%.2f,"
                     "\"sobre_umbral\":%lld}",
                     i > 0 ? "," : "", a[i].maquina, a[i].validas, a[i].descartadas,
                     a[i].temp_min, a[i].temp_max, a[i].temp_prom, a[i].sobre_umbral);
        if (w < 0 || (size_t)w >= tam - usado)
            return -1;
        usado += (size_t)w;
    }

    w = snprintf(json + usado, tam - usado, "]}");
    return (w < 0 || (size_t)w >= tam - usado) ? -1 : 0;
}

/* La respuesta trae una linea por campo (M1.temp_prom=ok o =mal) y
 * intentos_restantes=N. Se muestra tal cual y se cuentan los "=mal". */
static int contar_malos(const char *cuerpo)
{
    int malos = 0;

    for (const char *p = cuerpo; (p = strstr(p, "=mal")) != NULL; p += 4)
        malos++;
    return malos;
}

static int enviar_analisis(Red *red, const Analisis *a, int n)
{
    char json[2048];
    Respuesta resp;

    if (armar_json(a, n, json, sizeof json) != 0) {
        fprintf(stderr, "Error: el JSON del analisis no cabe en el buffer\n");
        return -1;
    }
    if (red_post_json(red, "/api/v1/respuestas/analisis", json, &resp) != 0)
        return -1;

    int ok = respuesta_ok(&resp, "respuestas/analisis");
    if (resp.datos != NULL && resp.datos[0] != '\0')
        printf("Respuesta del servidor:\n%s%s", resp.datos,
               resp.datos[strlen(resp.datos) - 1] == '\n' ? "" : "\n");

    int malos = ok ? contar_malos(resp.datos) : -1;
    respuesta_liberar(&resp);

    if (!ok)
        return -1;
    if (malos > 0) {
        fprintf(stderr, "El servidor marco %d campo(s) como mal. Cada envio gasta un intento.\n", malos);
        return -1;
    }
    printf("El servidor acepto todos los campos del analisis\n");
    return 0;
}

int cmd_analizar(Red *red, sqlite3 *db)
{
    Analisis res[ANALISIS_MAX_MAQUINAS];
    double umbral = 0.0;

    if (leer_umbral(db, &umbral) != 0)
        return 1;

    int n = datos_analizar(db, umbral, res, ANALISIS_MAX_MAQUINAS);
    if (n < 0)
        return 1;
    if (n == 0) {
        fprintf(stderr, "No hay lecturas. Corre primero: ./estacion descargar\n");
        return 1;
    }

    mostrar(res, n, umbral);
    return enviar_analisis(red, res, n) == 0 ? 0 : 1;
}
