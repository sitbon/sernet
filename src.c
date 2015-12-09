#include "src.h"
#include "packet.h"
#include "msg.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <pthread.h>
#include <time.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/wait.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/ioctl.h>

typedef struct {
    pthread_t thr_tx;
    pthread_t thr_rx;
} src_t;

static void *src_tx_thread(void *param);
static void *src_rx_thread(void *param);

static src_t src;

void src_start(src_param_t *p)
{
    pthread_create(&src.thr_tx, NULL, src_tx_thread, (void *)p);
    pthread_create(&src.thr_rx, NULL, src_rx_thread, (void *)p);
}

void src_stop(void)
{
    if (src.thr_tx) {
        pthread_cancel(src.thr_tx);
        pthread_join(src.thr_tx, NULL);
    }

    if (src.thr_rx) {
        pthread_cancel(src.thr_rx);
        pthread_join(src.thr_rx, NULL);
    }
}

static void *src_tx_thread(void *param)
{
    src_param_t *const p = (src_param_t *)param;
    const int fd = p->fd_tx;
    pkt_len_t pkt_len = (pkt_len_t)sizeof(tx_pkt_t) + (pkt_len_t)p->pkt_len;
    size_t len;
    ssize_t wlen;
    tx_pkt_t *const pkt = malloc(pkt_len);
    relay_pkt_data_t *const relay_data = (relay_pkt_data_t *)pkt->data;
    relay_t *const relay = &relay_data->relay;
    pkt_data_t *const buf = malloc(UART_MAX_LEN*2);

    const struct timespec ts_delay = {
            .tv_sec = 0,
            .tv_nsec = p->delay_ns
    };

    memset(pkt, 0, pkt_len);
    pkt->hdr.len = (pkt_len_t)pkt_len;
    pkt->hdr.type = PKT_TYPE_TX_REQ;
    pkt->fid = 1;
    pkt->rsv[0] = 0xff;
    pkt->rsv[1] = 0xfe;
    pkt->dst.addr[4] = 0xff;
    pkt->dst.addr[5] = 0xff;
    pkt->dst.addr[6] = 0xff;
    pkt->dst.addr[7] = 0xff;
    relay_data->magic = PKT_MAGIC_RELAY;
    relay->src = p->addr_listen.sin_addr.s_addr;
    relay->port = p->addr_listen.sin_port;

    set_thread_prio(true);

    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &relay->ts);
        if (!++relay->seq) relay->seq++;

#ifdef SRC_XBEE_API_MODE
        pkt_len = pkt_encode((pkt_t *)pkt, buf);
        print_hex(stdout, buf, pkt_len);
        len = 0;

        do {
            wlen = write(fd, &((unsigned char *)pkt)[len], pkt_len - len);

            if (wlen <= 0) {
                if (errno == EINTR) continue;
                fprintf(stderr, "uart tx: wlen=%li len=%li exp=%i\n", wlen, len, pkt_len);
                exit(-1);
            }

            len += wlen;
        } while (len < pkt_len);
        //putchar('-'); fflush(stdout);
        //read(fd, buf, UART_MAX_LEN*2);
#else
        pkt_len = sizeof(relay_pkt_data_t);
        len = 0;

        do {
            wlen = write(fd, &((unsigned char *)relay_data)[len], pkt_len - len);

            if (wlen <= 0) {
                if (errno == EINTR) continue;
                fprintf(stderr, "uart tx: wlen=%li len=%li exp=%i\n", wlen, len, pkt_len);
                exit(-1);
            }

            len += wlen;
        } while (len < pkt_len);
        //putchar('-'); fflush(stdout);
        //printf("-%li", len); fflush(stdout);
#endif
        p->t_rtt.cnt_exp++;
        if (ts_delay.tv_nsec) nanosleep(&ts_delay, NULL);
    }

    return NULL;
}

static void *src_rx_thread(void *param)
{
    src_param_t *const p = (src_param_t *)param;
    const int fd = p->fd_rx;
    struct sockaddr_in sin = { 0 };
    socklen_t sin_len;
    relay_msg_t *const msg = malloc(UART_MAX_LEN);
    relay_t *const relay = &msg->relay;
    struct timespec ts = { 0 };
    int len;

    set_thread_prio(false);

    while (1) {
        sin_len = sizeof(sin);
        len = udp_recv(fd, (struct sockaddr *)&sin, &sin_len, msg, UART_MAX_LEN);

        if (len < sizeof(relay_msg_t) || len != msg->hdr.len) {
            if (len < 0 && errno == EINTR) continue;
            if (!len) break;

            if (len >= sizeof(msg_hdr_t)) {
                fprintf(stderr, "udp rx: len=%i plen=%i\n", len, msg->hdr.len);
            } else {
                fprintf(stderr, "udp rx: len=%i min=%li\n", len, sizeof(msg_t));
            }

            if (len < 0) exit(-1);
            continue;
        }

        //putchar('.'); fflush(stdout);

        if (msg->hdr.type != MSG_TYPE_RELAY) {
            fprintf(stderr, "udp rx: type=%i expected=%i\n", msg->hdr.type, MSG_TYPE_RELAY);
            continue;
        }

        if (relay->port != p->addr_listen.sin_port || relay->src != p->addr_listen.sin_addr.s_addr) {
            struct in_addr msg_addr = { relay->src };
            fprintf(stderr, "udp rx: dst=%s:%u exp=%s:%u\n",
                    inet_ntoa(msg_addr), ntohs(relay->port),
                    inet_ntoa(p->addr_listen.sin_addr), ntohs(p->addr_listen.sin_port));
            continue;
        }

        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts_diff(&ts, &relay->ts, &ts);
        ts_add(&p->t_rtt.sum, &p->t_rtt.sum, &ts);
        p->t_rtt.cnt++;
    }

    return NULL;
}
