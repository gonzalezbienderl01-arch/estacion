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

/* Opciones que valen para todas las peticiones (se ponen una sola vez). */
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
    OPCION(h, CURLOPT_ERRORBUFFER, red->error);
    return 0;
}

/* Opciones del handle MQTT. La URL y el mensaje se ponen en cada publicacion. */
static int configurar_mqtt(Red *red)
{
    CURL *h = red->mqtt;

    OPCION(h, CURLOPT_USERNAME, red->matricula);
    OPCION(h, CURLOPT_PASSWORD, red->token);
    OPCION(h, CURLOPT_CONNECTTIMEOUT, 2L);
    OPCION(h, CURLOPT_TIMEOUT, 3L);     /* un latido perdido no debe frenar el bucle */
    OPCION(h, CURLOPT_NOSIGNAL, 1L);
    OPCION(h, CURLOPT_ERRORBUFFER, red->error);
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

    red->mqtt = curl_easy_init();
    if (red->mqtt == NULL) {
        fprintf(stderr, "Error: no se pudo crear el handle MQTT\n");
        red_cerrar(red);
        return -1;
    }

    if (configurar_handle(red) != 0 || configurar_mqtt(red) != 0) {
        red_cerrar(red);
        return -1;
    }
    return 0;
}

void red_cerrar(Red *red)
{
    if (red->suscripcion != NULL) {
        curl_easy_cleanup(red->suscripcion);
        red->suscripcion = NULL;
    }
    if (red->mqtt != NULL) {
        curl_easy_cleanup(red->mqtt);
        red->mqtt = NULL;
    }
    if (red->curl != NULL) {
        curl_easy_cleanup(red->curl);
        red->curl = NULL;
        curl_global_cleanup();
    }
}

void respuesta_liberar(Respuesta *resp)
{
    free(resp->datos);
    resp->datos = NULL;
    resp->largo = 0;
}

/* Deja la respuesta vacia y arma la URL completa. */
static int preparar(const Red *red, const char *ruta, char *url, size_t tam,
                    Respuesta *resp)
{
    resp->datos = NULL;
    resp->largo = 0;
    resp->codigo_http = 0;
    resp->retry_after = 0;

    int n = snprintf(url, tam, "http://%s:%d%s", red->host, RED_PUERTO_HTTP, ruta);
    if (n < 0 || (size_t)n >= tam) {
        fprintf(stderr, "Error: la URL es demasiado larga\n");
        return -1;
    }
    return 0;
}

/* Ejecuta la peticion ya configurada y revisa los dos niveles de error:
 * primero el CURLcode (hubo conexion?) y luego se lee el codigo HTTP. */
static int ejecutar(Red *red, const char *metodo, const char *ruta, Respuesta *resp)
{
    red->error[0] = '\0';

    CURLcode rc = curl_easy_perform(red->curl);
    if (rc != CURLE_OK) {
        fprintf(stderr, "Error de red en %s %s: %s\n", metodo, ruta,
                red->error[0] ? red->error : curl_easy_strerror(rc));
        respuesta_liberar(resp);
        return -1;
    }

    rc = curl_easy_getinfo(red->curl, CURLINFO_RESPONSE_CODE, &resp->codigo_http);
    if (rc != CURLE_OK) {
        fprintf(stderr, "Error leyendo el codigo HTTP: %s\n", curl_easy_strerror(rc));
        respuesta_liberar(resp);
        return -1;
    }

    /* Retry-After (503): libcurl ya lo interpreta; si no vino, queda en 0. */
    curl_off_t espera = 0;
    if (curl_easy_getinfo(red->curl, CURLINFO_RETRY_AFTER, &espera) == CURLE_OK && espera > 0)
        resp->retry_after = (long)espera;

    if (resp->datos == NULL) {          /* respuesta sin cuerpo */
        resp->datos = calloc(1, 1);
        if (resp->datos == NULL) {
            fprintf(stderr, "Error: sin memoria\n");
            return -1;
        }
    }
    return 0;
}

int red_get(Red *red, const char *ruta, Respuesta *resp)
{
    char url[256];

    if (preparar(red, ruta, url, sizeof url, resp) != 0)
        return -1;

    OPCION(red->curl, CURLOPT_URL, url);
    OPCION(red->curl, CURLOPT_HTTPGET, 1L);     /* vuelve a GET si antes fue POST */
    OPCION(red->curl, CURLOPT_WRITEDATA, resp);
    return ejecutar(red, "GET", ruta, resp);
}

/* Opciones propias del POST. Si algo falla, quien llama libera las cabeceras. */
static int configurar_post(Red *red, const char *url, const char *json,
                           struct curl_slist *cabeceras, Respuesta *resp)
{
    OPCION(red->curl, CURLOPT_URL, url);
    OPCION(red->curl, CURLOPT_HTTPHEADER, cabeceras);
    OPCION(red->curl, CURLOPT_POSTFIELDSIZE, (long)strlen(json));
    OPCION(red->curl, CURLOPT_COPYPOSTFIELDS, json);    /* curl guarda su propia copia */
    OPCION(red->curl, CURLOPT_WRITEDATA, resp);
    return 0;
}

int red_post_json(Red *red, const char *ruta, const char *json, Respuesta *resp)
{
    char url[256];

    if (preparar(red, ruta, url, sizeof url, resp) != 0)
        return -1;

    struct curl_slist *cabeceras = curl_slist_append(NULL, "Content-Type: application/json");
    if (cabeceras == NULL) {
        fprintf(stderr, "Error: sin memoria para las cabeceras\n");
        return -1;
    }

    int resultado = configurar_post(red, url, json, cabeceras, resp);
    if (resultado == 0)
        resultado = ejecutar(red, "POST", ruta, resp);

    /* El handle se reutiliza: se sueltan las cabeceras antes de liberarlas. */
    (void)curl_easy_setopt(red->curl, CURLOPT_HTTPHEADER, NULL);
    curl_slist_free_all(cabeceras);
    return resultado;
}

int red_mqtt_publicar(Red *red, const char *topico, const char *texto)
{
    char url[256];
    int n = snprintf(url, sizeof url, "mqtt://%s:%d/%s", red->host, RED_PUERTO_MQTT, topico);

    if (n < 0 || (size_t)n >= sizeof url) {
        fprintf(stderr, "Error: la URL MQTT es demasiado larga\n");
        return -1;
    }

    OPCION(red->mqtt, CURLOPT_URL, url);
    OPCION(red->mqtt, CURLOPT_POSTFIELDSIZE, (long)strlen(texto));
    OPCION(red->mqtt, CURLOPT_COPYPOSTFIELDS, texto);

    red->error[0] = '\0';
    CURLcode rc = curl_easy_perform(red->mqtt);
    if (rc != CURLE_OK) {
        fprintf(stderr, "Error MQTT publicando en %s: %s\n", topico,
                red->error[0] ? red->error : curl_easy_strerror(rc));
        return -1;
    }
    return 0;
}

/* El handle de la suscripcion se crea la primera vez que se usa. */
static int configurar_suscripcion(Red *red)
{
    if (red->suscripcion != NULL)
        return 0;

    red->suscripcion = curl_easy_init();
    if (red->suscripcion == NULL) {
        fprintf(stderr, "Error: no se pudo crear el handle de la suscripcion\n");
        return -1;
    }

    CURL *h = red->suscripcion;
    OPCION(h, CURLOPT_USERNAME, red->matricula);
    OPCION(h, CURLOPT_PASSWORD, red->token);
    OPCION(h, CURLOPT_CONNECTTIMEOUT, 3L);
    /* Sin CURLOPT_TIMEOUT: la suscripcion debe quedarse abierta todo el tiempo que pueda. */
    OPCION(h, CURLOPT_NOSIGNAL, 1L);
    OPCION(h, CURLOPT_NOPROGRESS, 0L);                  /* activa el callback de progreso */
    OPCION(h, CURLOPT_TCP_KEEPALIVE, 1L);               /* detecta una conexion muerta */
    OPCION(h, CURLOPT_TCP_KEEPIDLE, 30L);
    OPCION(h, CURLOPT_TCP_KEEPINTVL, 10L);
    OPCION(h, CURLOPT_ERRORBUFFER, red->error);
    return 0;
}

int red_mqtt_suscribir(Red *red, const char *topico,
                       curl_write_callback escribir, void *dato_escribir,
                       curl_xferinfo_callback progreso, void *dato_progreso)
{
    char url[256];
    int n = snprintf(url, sizeof url, "mqtt://%s:%d/%s", red->host, RED_PUERTO_MQTT, topico);

    if (n < 0 || (size_t)n >= sizeof url) {
        fprintf(stderr, "Error: la URL MQTT es demasiado larga\n");
        return -1;
    }
    if (configurar_suscripcion(red) != 0)
        return -1;

    CURL *h = red->suscripcion;
    OPCION(h, CURLOPT_URL, url);                        /* sin POSTFIELDS: eso es suscribirse */
    OPCION(h, CURLOPT_WRITEFUNCTION, escribir);
    OPCION(h, CURLOPT_WRITEDATA, dato_escribir);
    OPCION(h, CURLOPT_XFERINFOFUNCTION, progreso);
    OPCION(h, CURLOPT_XFERINFODATA, dato_progreso);

    red->error[0] = '\0';
    CURLcode rc = curl_easy_perform(h);
    if (rc == CURLE_ABORTED_BY_CALLBACK)
        return 0;                                       /* nos pidieron terminar: no es un error */
    if (rc != CURLE_OK) {
        fprintf(stderr, "Suscripcion MQTT cortada: %s\n",
                red->error[0] ? red->error : curl_easy_strerror(rc));
        return -1;
    }
    return 0;
}

int respuesta_ok(const Respuesta *resp, const char *que)
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
    case 403:
        fprintf(stderr, "HTTP 403 en %s: acceso denegado (en el analisis significa que ya no quedan intentos)\n", que);
        break;
    case 429:
        fprintf(stderr, "HTTP 429 en %s: demasiadas peticiones por segundo\n", que);
        break;
    default:
        fprintf(stderr, "HTTP %ld inesperado en %s\n", resp->codigo_http, que);
    }
    return 0;
}
