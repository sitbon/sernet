#ifndef __FWD_H__
#define __FWD_H__

#include "local.h"
#include "udp.h"

typedef struct {
    int                 fd_rx;
    int                 fd_tx;
    struct sockaddr_in  addr;
    bool                verbose;
} fwd_param_t;

void fwd_start(fwd_param_t *p);
void fwd_stop(void);

#endif
