#ifndef __OSC_H__
#define __OSC_H__

#include "local.h"
#include <sys/un.h>
#include <netinet/in.h>

typedef struct {
    int                 fd_rx;
    int                 fd_tx;
    struct sockaddr_un  addr_un;
    struct sockaddr_in  addr_osc;
} osc_param_t;

void osc_start(osc_param_t *p);
void osc_stop(void);

#endif
