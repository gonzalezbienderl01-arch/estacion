#ifndef RED_H
#define RED_H

#include <stddef.h>
#include <curl/curl.h>

#define RED_USER_AGENT   "estacion-itla/1.0"
#define RED_PUERTO_HTTP  4000
#define RED_MAX_CUERPO   (1024 * 1024)

/* Conexion con el servidor: un solo easy handle que se reutiliza. */
typedef struct {
    CURL *curl;
    char  host[128];
    char  token[128];
    char  matricula[32];
} Red;

/* Resultado de una peticion HTTP. */
typedef struct {
    char *datos;        /* cuerpo, siempre terminado en '\0' si red_get devuelve 0 */
    size_t largo;
    long  codigo_http;
} Respuesta;

/* Lee ITLA_HOST, ITLA_MATRICULA e ITLA_TOKEN del entorno y prepara el handle.
 * Devuelve 0 si todo bien, -1 si algo falla (ya imprime el motivo). */
int  red_iniciar(Red *red);
void red_cerrar(Red *red);

/* GET autenticado. Devuelve 0 si hubo respuesta HTTP (revisa resp->codigo_http),
 * -1 si falla el nivel de red (CURLcode distinto de CURLE_OK). */
int  red_get(Red *red, const char *ruta, Respuesta *resp);

void respuesta_liberar(Respuesta *resp);

#endif
