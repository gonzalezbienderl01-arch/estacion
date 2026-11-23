#include "sensor.h"

#include <math.h>
#include <stdlib.h>
#include <time.h>

#define PI 3.14159265358979323846

/* Ruido uniforme en [-amplitud, +amplitud]. */
static double ruido(double amplitud)
{
    return ((double)rand() / RAND_MAX * 2.0 - 1.0) * amplitud;
}

/* Deja el valor dentro de [minimo, maximo]. */
static double limitar(double v, double minimo, double maximo)
{
    if (v < minimo)
        return minimo;
    if (v > maximo)
        return maximo;
    return v;
}

void sensor_leer(Muestra *m)
{
    static int inicializado = 0;
    time_t ahora = time(NULL);

    if (!inicializado) {
        srand((unsigned)ahora);
        inicializado = 1;
    }

    m->ts = (long long)ahora;
    /* Temperatura: onda de 10 minutos alrededor de 35 C, +-8 C, mas ruido de +-2 C. */
    m->temp = limitar(35.0 + 8.0 * sin(2.0 * PI * (double)ahora / 600.0) + ruido(2.0), 25.0, 45.0);
    /* Corriente: onda de 5 minutos alrededor de 3.5 A, +-1.2 A, mas ruido de +-0.3 A. */
    m->corriente = limitar(3.5 + 1.2 * sin(2.0 * PI * (double)ahora / 300.0) + ruido(0.3), 2.0, 5.0);
}
