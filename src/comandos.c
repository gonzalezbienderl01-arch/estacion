/**************************************************************************/
/*  Programación para mecatrónicos                                        */
/*  Nombre:    Bienderl Gonzalez                                          */
/*  Matricula: 2024-1302                                                  */
/*  Seccion:   Miercoles                                                  */
/*  Practica:  Estacion de Telemetria ITLA                                */
/*  Fecha:     23/11/2026                                                 */
/*  Link Practica: https://github.com/gonzalezbienderl01-arch/estacion    */
/**************************************************************************/

#include "comandos.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Recorre el texto "clave=valor" linea por linea y guarda cada par.
 * Modifica el texto (pone '\0'), que es nuestro buffer.
 * Devuelve la cantidad de pares guardados, o -1 si hay error. */
static int guardar_pares(sqlite3 *db, char *texto)
{
    int guardados = 0;
    char *linea = texto;

    while (linea != NULL && *linea != '\0') {
        char *fin = strchr(linea, '\n');
        if (fin != NULL)
            *fin = '\0';

        size_t largo = strlen(linea);
        if (largo > 0 && linea[largo - 1] == '\r')
            linea[largo - 1] = '\0';

        char *igual = strchr(linea, '=');
        if (igual != NULL && igual != linea) {
            *igual = '\0';
            if (datos_guardar_config(db, linea, igual + 1) != 0)
                return -1;
            guardados++;
        }
        linea = (fin != NULL) ? fin + 1 : NULL;
    }
    return guardados;
}

static void imprimir_clave(sqlite3 *db, const char *clave)
{
    char valor[128];
    int rc = datos_leer_config(db, clave, valor, sizeof valor);
    if (rc == 0)
        printf("  %-12s = %s\n", clave, valor);
    else if (rc == 1)
        printf("  %-12s = (no vino en la respuesta)\n", clave);
}

int cmd_config(Red *red, sqlite3 *db)
{
    Respuesta resp;

    if (red_get(red, "/api/v1/config", &resp) != 0)
        return 1;

    if (resp.codigo_http == 401) {
        fprintf(stderr, "HTTP 401: el servidor rechazo tu token. "
                        "Revisa ITLA_TOKEN e ITLA_MATRICULA.\n");
        respuesta_liberar(&resp);
        return 1;
    }
    if (resp.codigo_http != 200) {
        fprintf(stderr, "HTTP %ld inesperado en /config\n", resp.codigo_http);
        respuesta_liberar(&resp);
        return 1;
    }

    /* Guardar todos los pares dentro de una transaccion. */
    if (datos_comenzar(db) != 0) {
        respuesta_liberar(&resp);
        return 1;
    }
    int guardados = guardar_pares(db, resp.datos);
    respuesta_liberar(&resp);

    if (guardados < 0 || datos_confirmar(db) != 0) {
        datos_revertir(db);
        return 1;
    }

    printf("Configuracion recibida (%d parametros guardados):\n", guardados);
    imprimir_clave(db, "umbral_temp");
    imprimir_clave(db, "intervalo_s");

    char hora[64];
    if (datos_leer_config(db, "hora", hora, sizeof hora) == 0) {
        char *fin = NULL;
        long long epoch = strtoll(hora, &fin, 10);
        if (fin != hora) {
            time_t t = (time_t)epoch;
            char texto[64] = "";
            struct tm *tm_local = localtime(&t);
            if (tm_local != NULL)
                strftime(texto, sizeof texto, "%Y-%m-%d %H:%M:%S", tm_local);
            printf("  %-12s = %lld (%s)\n", "hora", epoch, texto);
        }
    }
    return 0;
}
