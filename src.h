#ifndef __SRC_H__
#define __SRC_H__

#include "local.h"
#include "udp.h"

typedef struct {
    unsigned int cnt_exp;
    unsigned int cnt;
    struct timespec sum;
} timing_t;

typedef struct {
    int fd_rx;
    int fd_tx;
    size_t pkt_len;
    long delay_ns;
    struct sockaddr_in addr_listen;
    timing_t t_rtt;
    bool simulate_rx;
} src_param_t;

void src_start(src_param_t *p);
void src_stop(void);

#endif
