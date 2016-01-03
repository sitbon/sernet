#ifndef __DST_H__
#define __DST_H__

#include "local.h"
#include <sys/un.h>

typedef struct {
    int                 fd_rx;
    int                 fd_tx_un;
    int                 fd_tx_in;
    struct sockaddr_un  addr_un;
    bool                verbose;
} dst_param_t;

void dst_start(dst_param_t *p);
void dst_stop(void);

#endif
