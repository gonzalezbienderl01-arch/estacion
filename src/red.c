#include "red.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOKEN_LARGO_ESPERADO 32

/* Asigna una opcion de curl y sale con -1 si falla. */
#define OPCION(h, op, val)                                              \
    do {                                                                \
        CURLcode rc_ = curl_easy_setopt((h), (op), (val));              \
        if (rc_ != CURLE_OK) {                                          \
            fprintf(stderr, "Error configurando curl (%s): %s\n",       \
                    #op, curl_easy_strerror(rc_));                      \
            return -1;                                                  \
        }                                                               \
    } while (0)

/* Copia una variable de entorno a un buffer con control de tamano. */
static int leer_entorno(const char *nombre, char *destino, size_t tam)
{
    const char *valor = getenv(nombre);
    if (valor == NULL || valor[0] == '\0') {
        fprintf(stderr, "Error: falta la variable de entorno %s\n", nombre);
        return -1;
    }
    int n = snprintf(destino, tam, "%s", valor);
    if (n < 0 || (size_t)n >= tam) {
        fprintf(stderr, "Error: %s es demasiado larga\n", nombre);
        return -1;
    }
    return 0;
}

/* Callback de libcurl: acumula el cuerpo en memoria dinamica. */
static size_t al_recibir(char *ptr, size_t size, size_t nmemb, void *ud)
{
    Respuesta *r = ud;
    size_t n = size * nmemb;

    if (r->largo + n > RED_MAX_CUERPO)
        return 0;                       /* respuesta enorme: abortar */

    char *nuevo = realloc(r->datos, r->largo + n + 1);
    if (nuevo == NULL)
        return 0;

    r->datos = nuevo;
    memcpy(r->datos + r->largo, ptr, n);
    r->largo += n;
    r->datos[r->largo] = '\0';
    return n;                           /* siempre size * nmemb */
}

static int configurar_handle(Red *red)
{
    CURL *h = red->curl;

    OPCION(h, CURLOPT_USERAGENT, RED_USER_AGENT);
    OPCION(h, CURLOPT_TIMEOUT, 5L);
    OPCION(h, CURLOPT_CONNECTTIMEOUT, 3L);
    OPCION(h, CURLOPT_NOSIGNAL, 1L);
    OPCION(h, CURLOPT_HTTPAUTH, (long)CURLAUTH_BEARER);
    OPCION(h, CURLOPT_XOAUTH2_BEARER, red->token);
    OPCION(h, CURLOPT_WRITEFUNCTION, al_recibir);
    return 0;
}

int red_iniciar(Red *red)
{
    memset(red, 0, sizeof *red);

    if (leer_entorno("ITLA_HOST", red->host, sizeof red->host) != 0 ||
        leer_entorno("ITLA_MATRICULA", red->matricula, sizeof red->matricula) != 0 ||
        leer_entorno("ITLA_TOKEN", red->token, sizeof red->token) != 0)
        return -1;

    /* Pista del reto: un token con espacios o salto de linea da HTTP 401. */
    size_t largo = strlen(red->token);
    if (largo != TOKEN_LARGO_ESPERADO)
        fprintf(stderr, "Aviso: ITLA_TOKEN mide %zu caracteres (deben ser %d)\n",
                largo, TOKEN_LARGO_ESPERADO);

    CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (rc != CURLE_OK) {
        fprintf(stderr, "Error iniciando libcurl: %s\n", curl_easy_strerror(rc));
        return -1;
    }

    red->curl = curl_easy_init();
    if (red->curl == NULL) {
        fprintf(stderr, "Error: no se pudo crear el handle de curl\n");
        curl_global_cleanup();
        return -1;
    }

    if (configurar_handle(red) != 0) {
        red_cerrar(red);
        return -1;
    }
    return 0;
}

void red_cerrar(Red *red)
{
    if (red->curl != NULL) {
        curl_easy_cleanup(red->curl);
        red->curl = NULL;
        curl_global_cleanup();
    }
}

int red_get(Red *red, const char *ruta, Respuesta *resp)
{
    char url[256];
    char err[CURL_ERROR_SIZE] = "";

    resp->datos = NULL;
    resp->largo = 0;
    resp->codigo_http = 0;

    int n = snprintf(url, sizeof url, "http://%s:%d%s",
                     red->host, RED_PUERTO_HTTP, ruta);
    if (n < 0 || (size_t)n >= sizeof url) {
        fprintf(stderr, "Error: la URL es demasiado larga\n");
        return -1;
    }

    OPCION(red->curl, CURLOPT_URL, url);
    OPCION(red->curl, CURLOPT_HTTPGET, 1L);
    OPCION(red->curl, CURLOPT_WRITEDATA, resp);
    OPCION(red->curl, CURLOPT_ERRORBUFFER, err);

    /* Nivel 1 de error: la conexion en si (CURLcode). */
    CURLcode rc = curl_easy_perform(red->curl);
    if (rc != CURLE_OK) {
        fprintf(stderr, "Error de red en GET %s: %s\n", ruta,
                err[0] ? err : curl_easy_strerror(rc));
        respuesta_liberar(resp);
        return -1;
    }

    /* Nivel 2 de error: el codigo HTTP lo interpreta quien llama. */
    rc = curl_easy_getinfo(red->curl, CURLINFO_RESPONSE_CODE, &resp->codigo_http);
    if (rc != CURLE_OK) {
        fprintf(stderr, "Error leyendo el codigo HTTP: %s\n", curl_easy_strerror(rc));
        respuesta_liberar(resp);
        return -1;
    }

    if (resp->datos == NULL) {          /* respuesta sin cuerpo */
        resp->datos = calloc(1, 1);
        if (resp->datos == NULL)
            return -1;
    }
    return 0;
}

void respuesta_liberar(Respuesta *resp)
{
    free(resp->datos);
    resp->datos = NULL;
    resp->largo = 0;
}
