#include <stdio.h>
#include <string.h>

#include "analisis.h"
#include "comandos.h"
#include "datos.h"
#include "descarga.h"
#include "envio.h"
#include "escuchar.h"
#include "red.h"

typedef int (*Comando)(Red *red, sqlite3 *db);

typedef struct {
    const char *nombre;
    const char *ayuda;
    Comando     ejecutar;
} Subcomando;

/* Cada nivel nuevo agrega una linea aqui. */
static const Subcomando SUBCOMANDOS[] = {
    { "config",    "Nivel 0: leer la configuracion del servidor", cmd_config },
    { "descargar", "Nivel 1: bajar las lecturas de planta",       cmd_descargar },
    { "analizar",  "Nivel 2: estadisticas por maquina con SQL",    cmd_analizar },
    { "enviar",    "Niveles 3 y 4: telemetria resiliente y latido", cmd_enviar },
    { "escuchar",  "Nivel 5: alarmas por MQTT y confirmacion HTTP", cmd_escuchar },
};

#define CANTIDAD(a) (sizeof(a) / sizeof((a)[0]))

static void uso(const char *prog)
{
    fprintf(stderr, "Uso: %s <subcomando>\n", prog);
    for (size_t i = 0; i < CANTIDAD(SUBCOMANDOS); i++)
        fprintf(stderr, "  %-10s %s\n", SUBCOMANDOS[i].nombre, SUBCOMANDOS[i].ayuda);
    fprintf(stderr, "\nVariables de entorno: ITLA_HOST, ITLA_MATRICULA, ITLA_TOKEN\n");
}

static const Subcomando *buscar(const char *nombre)
{
    for (size_t i = 0; i < CANTIDAD(SUBCOMANDOS); i++)
        if (strcmp(nombre, SUBCOMANDOS[i].nombre) == 0)
            return &SUBCOMANDOS[i];
    return NULL;
}

/* Abre red y base, ejecuta el subcomando y cierra todo en orden inverso. */
static int correr(const Subcomando *sub)
{
    Red red;
    sqlite3 *db = NULL;
    int resultado = 1;

    if (red_iniciar(&red) != 0)
        return 1;

    if (datos_abrir(DATOS_ARCHIVO, &db) == 0) {
        resultado = sub->ejecutar(&red, db);
        datos_cerrar(db);
    }

    red_cerrar(&red);
    return resultado;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        uso(argv[0]);
        return 2;
    }

    const Subcomando *sub = buscar(argv[1]);
    if (sub != NULL)
        return correr(sub);

    uso(argv[0]);
    return 2;
}
