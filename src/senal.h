/**************************************************************************/
/*  Programación para mecatrónicos                                        */
/*  Nombre:    Bienderl Gonzalez                                          */
/*  Matricula: 2024-1302                                                  */
/*  Seccion:   Miercoles                                                  */
/*  Practica:  Estacion de Telemetria ITLA                                */
/*  Fecha:     23/11/2026                                                 */
/*  Link Practica: https://github.com/gonzalezbienderl01-arch/estacion    */
/**************************************************************************/

#ifndef SENAL_H
#define SENAL_H

/* Ctrl+C (SIGINT) y SIGTERM: el manejador solo levanta una bandera y el programa
 * la revisa con calma. Asi se cierran SQLite y curl en orden. */
void senal_instalar(void);
int  senal_pedida(void);        /* 1 si el usuario pidio terminar */

#endif
