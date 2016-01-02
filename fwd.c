#include "local.h"
#include "serial.h"
#include "udp.h"
#include "msg.h"
#include "packet.h"
#include "sernet.yucc"
#include "fwd.h"

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

#define MAX_QUEUE       16
#define MAX_ADDR_CACHE  16

typedef struct {
    pkt_t *pkt[MAX_QUEUE];
    size_t cnt;
    pthread_mutex_t lock;
    pthread_cond_t can_produce;
    pthread_cond_t can_consume;
    pthread_t thr_prod;
    pthread_t thr_cons;
} pkt_queue_t;

typedef struct sockaddr_in addr_cache_t;

static pkt_queue_t queue = {
        .cnt = 0,
        .lock = PTHREAD_MUTEX_INITIALIZER,
        .can_produce = PTHREAD_COND_INITIALIZER,
        .can_consume = PTHREAD_COND_INITIALIZER
};

static addr_cache_t addr_cache[MAX_ADDR_CACHE];

static void *queue_rx_thread(void *param);
static void *queue_tx_thread(void *param);


static struct sockaddr_in *get_addr(u32 addr, u16 port)
{
    for (u8 i = 0; i < MAX_ADDR_CACHE; i++) {
        if (port == addr_cache[i].sin_port && addr == addr_cache[i].sin_addr.s_addr)
            return &addr_cache[i];
    }

    for (u8 i = 0; i < MAX_ADDR_CACHE; i++) {
        if (!addr_cache[i].sin_port) {
            addr_cache[i].sin_family = AF_INET;
            addr_cache[i].sin_port = port;
            addr_cache[i].sin_addr.s_addr = addr;
            return &addr_cache[i];
        }
    }

    return NULL;
}

void fwd_start(fwd_param_t *p)
{
    pthread_create(&queue.thr_prod, NULL, queue_rx_thread, (void *)p);
    pthread_create(&queue.thr_cons, NULL, queue_tx_thread, (void *)p);
}

void fwd_stop(void)
{
    if (queue.thr_prod) {
        pthread_cancel(queue.thr_prod);
        pthread_join(queue.thr_prod, NULL);
    }

    if (queue.thr_cons) {
        pthread_cancel(queue.thr_cons);
        pthread_join(queue.thr_cons, NULL);
    }
}

static void *queue_rx_thread(void *param)
{
    const fwd_param_t *const p = (fwd_param_t *)param;
    const int fd = p->fd_rx;
    void *const buf = malloc(UART_MAX_LEN);
    pkt_t *const pkt = malloc(UART_MAX_LEN), *qpkt;
    ssize_t len, rlen;

    memset(pkt, 0, UART_MAX_LEN);
    len = 0;

    set_thread_prio(true);

    while (1) {
        while (true) {
            rlen = read(fd, &((u8 *)buf)[len], UART_MAX_LEN - (size_t)len);
            //printf("<%li>", rlen); fflush(stdout);
            //print_hex(stdout, &((u8 *)buf)[len], (u32)rlen);

            if (rlen <= 0) {
                if (errno == EINTR) continue;
                fprintf(stderr, "read(%s) = %li\t\t[l:%li]\t[e:%i] %s\n", p->path_rx, rlen, len, errno, strerror(errno));
                goto done;
            }

            len += rlen;
            if ((rlen = pkt_decode(buf, (pkt_len_t)len, pkt)) == PKT_ERROR) goto done;
            //printf("[%li, %li]", len, rlen); fflush(stdout);
            if (rlen && (len - rlen)) memcpy(buf, &((u8 *)buf)[rlen], (size_t)(len - rlen));
            len -= rlen;

            if (pkt->hdr.len) break;
        }

        //putchar('-'); fflush(stdout);
        //printf("-%i", pkt->hdr.len); fflush(stdout);

        if (p->verbose) print_hex(stderr, (u8 *)pkt, pkt->hdr.len);

        /*const u64 addr =
                (u64)((rx_pkt_t *)pkt)->src.addr[0] |
                ((u64)((rx_pkt_t *)pkt)->src.addr[1] << (8*1)) |
                ((u64)((rx_pkt_t *)pkt)->src.addr[2] << (8*2)) |
                ((u64)((rx_pkt_t *)pkt)->src.addr[3] << (8*3)) |
                ((u64)((rx_pkt_t *)pkt)->src.addr[4] << (8*4)) |
                ((u64)((rx_pkt_t *)pkt)->src.addr[5] << (8*5)) |
                ((u64)((rx_pkt_t *)pkt)->src.addr[6] << (8*6)) |
                ((u64)((rx_pkt_t *)pkt)->src.addr[7] << (8*7))
        ;
        printf("%08lx\n", addr);*/

        qpkt = malloc((size_t)pkt->hdr.len);
        memcpy(qpkt, pkt, (size_t)pkt->hdr.len);
        pthread_mutex_lock(&queue.lock);

        while (queue.cnt >= MAX_QUEUE)
            pthread_cond_wait(&queue.can_produce, &queue.lock);

        queue.pkt[queue.cnt] = qpkt;
        queue.cnt++;

        pthread_cond_signal(&queue.can_consume);
        pthread_mutex_unlock(&queue.lock);
    }

    done:
    return NULL;
}

static void *queue_tx_thread(void *param)
{
    const fwd_param_t *const p = (fwd_param_t *)param;
    const int fd = p->fd_tx;
    const struct sockaddr_in *const addr = &p->addr;
    msg_t *const msg = malloc(UART_MAX_LEN);
    pkt_msg_t *const pkt_msg = (pkt_msg_t *)msg;
    relay_msg_t *const relay_msg = (relay_msg_t *)msg;
    relay_t *const relay = &relay_msg->relay;
    rx_pkt_t *pkt;
    pkt_len_t plen;
    int len;

    set_thread_prio(false);

    while (1) {
        pthread_mutex_lock(&queue.lock);

        while (!queue.cnt)
            pthread_cond_wait(&queue.can_consume, &queue.lock);

        pkt = (rx_pkt_t *)queue.pkt[--queue.cnt];
        queue.pkt[queue.cnt + 1] = NULL;
        pthread_cond_signal(&queue.can_produce);
        pthread_mutex_unlock(&queue.lock);

        if (!pkt) {
            fprintf(stderr, "udp tx: pkt == NULL\n");
            break;
        }

        if (pkt->hdr.type != PKT_TYPE_RX_IND) goto next;

        plen = pkt->hdr.len - (pkt_len_t)sizeof(rx_pkt_t);

        msg->hdr.flag = 0;
        msg->hdr.type = MSG_TYPE_PKT;

        if (plen >= sizeof(relay_pkt_data_t)) {
            const relay_pkt_data_t *const relay_data = (relay_pkt_data_t *)pkt->data;

            if (relay_data->magic == PKT_MAGIC_RELAY) {
                *relay = relay_data->relay;
                plen -= sizeof(relay_pkt_data_t);
                relay_msg->hdr.type = MSG_TYPE_RELAY;
                relay_msg->hdr.len = (pkt_len_t)sizeof(relay_msg_t) + plen;
                relay_msg->src = pkt->src;
                if (plen) memcpy(relay_msg->data, relay_data->data, plen);
                //print_hex(stdout, &relay_msg->relay, sizeof(relay_t));
                goto send;
            }
        }

        pkt_msg->hdr.len = (pkt_len_t)sizeof(pkt_msg_t) + plen;
        pkt_msg->src = pkt->src;
        if (plen) memcpy(pkt_msg->data, pkt->data, plen);

        send:
        //print_hex(stdout, msg, msg->hdr.len);
        len = udp_send(fd, (struct sockaddr *)addr, sizeof(*addr), msg, msg->hdr.len);

        if (len < msg->hdr.len) {
            if (len < 0 && errno == EINTR) goto send;
            fprintf(stderr, "udp tx: len=%i exp=%u\n", len, msg->hdr.len);
            exit(-1);
        }

        //putchar('>'); fflush(stdout);
        //printf(">(%i)", msg->hdr.len); fflush(stdout);

        /*if (!pkt->port || !pkt->addr) {
            struct in_addr pkt_addr = { pkt->addr };
            fprintf(stderr, "uart rx: bad addr or port %s:%u\n", inet_ntoa(pkt_addr), ntohs(pkt->port));
            continue;
        }

        addr = get_addr(pkt->addr, pkt->port);

        if (!addr) {
            struct in_addr pkt_addr = { pkt->addr };
            fprintf(stderr, "udp tx: get_addr(%s, %u) == NULL\n", inet_ntoa(pkt_addr), ntohs(pkt->port));
            break;
        }

        retry:
        len = udp_send(fd, (struct sockaddr *)addr, sizeof(*addr), pkt, pkt->hdr.len);

        if (len < pkt->hdr.len) {
            if (len < 0 && errno == EINTR) goto retry;
            fprintf(stderr, "udp tx: len=%i exp=%u\n", len, pkt->hdr.len);
            //free(pkt);
            break;
        }*/

        next:
        free(pkt);
        //putchar('+'), fflush(stdout);
    }

    return NULL;
}
