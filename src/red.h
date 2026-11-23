/**************************************************************************/
/*  Programación para mecatrónicos                                        */
/*  Nombre:    Bienderl Gonzalez                                          */
/*  Matricula: 2024-1302                                                  */
/*  Seccion:   Miercoles                                                  */
/*  Practica:  Estacion de Telemetria ITLA                                */
/*  Fecha:     23/11/2026                                                 */
/*  Link Practica: https://github.com/gonzalezbienderl01-arch/estacion    */
/**************************************************************************/

#ifndef RED_H
#define RED_H

#include <stddef.h>
#include <curl/curl.h>

#define RED_USER_AGENT   "estacion-itla/1.0"
#define RED_PUERTO_HTTP  4000
#define RED_MAX_CUERPO   (1024 * 1024)
#define RED_PUERTO_MQTT  1883

/* Conexion con el servidor: un solo easy handle que se reutiliza. */
typedef struct {
    CURL *curl;                     /* HTTP: un solo handle reutilizado */
    CURL *mqtt;                     /* MQTT (publicar): otro handle, tambien reutilizado */
    CURL *suscripcion;              /* MQTT (escuchar): propio, para no mezclarlo con nada */
    char  host[128];
    char  token[128];
    char  matricula[32];
    char  error[CURL_ERROR_SIZE];   /* texto del ultimo error que reporto curl */
} Red;

/* Resultado de una peticion HTTP. */
typedef struct {
    char *datos;        /* cuerpo, siempre terminado en '\0' si la funcion devuelve 0 */
    size_t largo;
    long  codigo_http;
    long  retry_after;  /* segundos que pidio esperar el servidor (503); 0 si no vino */
} Respuesta;

/* Lee ITLA_HOST, ITLA_MATRICULA e ITLA_TOKEN del entorno y prepara el handle.
 * Devuelve 0 si todo bien, -1 si algo falla (ya imprime el motivo). */
int  red_iniciar(Red *red);
void red_cerrar(Red *red);

/* GET autenticado. Devuelve 0 si hubo respuesta HTTP (revisa resp->codigo_http),
 * -1 si falla el nivel de red (CURLcode distinto de CURLE_OK). */
int  red_get(Red *red, const char *ruta, Respuesta *resp);

/* POST autenticado con cuerpo JSON. Mismo criterio de retorno que red_get. */
int  red_post_json(Red *red, const char *ruta, const char *json, Respuesta *resp);

void respuesta_liberar(Respuesta *resp);

/* Publica un mensaje MQTT (QoS 0) con libcurl: usuario = matricula, clave = token.
 * Devuelve 0 si se envio, -1 si el broker no respondio. El texto ya debe traer su '\n'. */
int  red_mqtt_publicar(Red *red, const char *topico, const char *texto);

/* Se suscribe a un topico MQTT con libcurl. curl_easy_perform no regresa mientras la
 * conexion siga viva: cada mensaje llega al callback "escribir" y "progreso" se llama
 * a cada rato (si devuelve 1, la suscripcion termina; sirve para Ctrl+C).
 * Devuelve 0 si la suscripcion termino sin error y -1 si se corto o no pudo conectar. */
int  red_mqtt_suscribir(Red *red, const char *topico,
                        curl_write_callback escribir, void *dato_escribir,
                        curl_xferinfo_callback progreso, void *dato_progreso);

/* Devuelve 1 si el codigo HTTP fue 200; si no, explica el motivo en stderr y devuelve 0.
 * "que" dice de que ruta se trata, para que el mensaje sea claro. */
int  respuesta_ok(const Respuesta *resp, const char *que);

#endif
