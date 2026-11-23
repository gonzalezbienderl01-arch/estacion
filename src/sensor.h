/**************************************************************************/
/*  Programación para mecatrónicos                                        */
/*  Nombre:    Bienderl Gonzalez                                          */
/*  Matricula: 2024-1302                                                  */
/*  Seccion:   Miercoles                                                  */
/*  Practica:  Estacion de Telemetria ITLA                                */
/*  Fecha:     23/11/2026                                                 */
/*  Link Practica: https://github.com/gonzalezbienderl01-arch/estacion    */
/**************************************************************************/

#ifndef SENSOR_H
#define SENSOR_H

/* Una lectura del sensor. */
typedef struct {
    long long ts;           /* hora Unix (segundos) */
    double    temp;         /* grados C: entre 25 y 45 */
    double    corriente;    /* amperios: entre 2 y 5 */
} Muestra;

/* Sensor simulado: una onda lenta mas un poco de ruido, siempre dentro del rango
 * que pide el reto. Si algun dia hay un DHT22 real, solo cambia este archivo. */
void sensor_leer(Muestra *m);

#endif
