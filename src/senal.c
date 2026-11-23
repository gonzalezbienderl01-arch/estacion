#define _POSIX_C_SOURCE 200809L         /* sigaction con -std=c11 */

#include "senal.h"

#include <signal.h>
#include <string.h>

static volatile sig_atomic_t pedida = 0;

static void al_recibir_senal(int senal)
{
    (void)senal;
    pedida = 1;
}

void senal_instalar(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = al_recibir_senal;   /* sin SA_RESTART: asi las esperas se interrumpen */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

int senal_pedida(void)
{
    return pedida;
}
