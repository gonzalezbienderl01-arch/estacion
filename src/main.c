#include <stdio.h>
#include <string.h>

#include "comandos.h"
#include "datos.h"
#include "red.h"

static void uso(const char *prog)
{
    fprintf(stderr,
        "Uso: %s <subcomando>\n"
        "  config      Nivel 0: leer la configuracion del servidor\n"
        "  descargar   Nivel 1: bajar las lecturas de planta (pendiente)\n"
        "  analizar    Nivel 2: estadisticas con SQL (pendiente)\n"
        "  enviar      Niveles 3 y 4: telemetria y latido (pendiente)\n"
        "  escuchar    Nivel 5: alarmas MQTT (pendiente)\n"
        "\nVariables de entorno: ITLA_HOST, ITLA_MATRICULA, ITLA_TOKEN\n",
        prog);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        uso(argv[0]);
        return 2;
    }

    const char *sub = argv[1];
    int es_config = strcmp(sub, "config") == 0;

    if (!es_config) {
        if (strcmp(sub, "descargar") == 0 || strcmp(sub, "analizar") == 0 ||
            strcmp(sub, "enviar") == 0 || strcmp(sub, "escuchar") == 0)
            fprintf(stderr, "El subcomando '%s' todavia no esta implementado.\n", sub);
        else
            uso(argv[0]);
        return 2;
    }

    Red red;
    sqlite3 *db = NULL;
    int resultado = 1;

    if (red_iniciar(&red) != 0)
        return 1;

    if (datos_abrir(DATOS_ARCHIVO, &db) == 0) {
        resultado = cmd_config(&red, db);
        datos_cerrar(db);
    }

    red_cerrar(&red);
    return resultado;
}
