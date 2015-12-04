#include "local.h"
#include "serial.h"
#include "udp.h"
#include "packet.h"
#include "sernet.yucc"

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
    relay_pkt_t *pkt[MAX_QUEUE];
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

void relay_start(int fd_uart_in, int fd_sock_out)
{
    if (fd_uart_in > -1)
        pthread_create(&queue.thr_prod, NULL, queue_rx_thread, (void *)(intptr_t)fd_uart_in);

    if (fd_sock_out > -1)
        pthread_create(&queue.thr_cons, NULL, queue_tx_thread, (void *)(intptr_t)fd_sock_out);
}

void relay_stop(void)
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
    relay_pkt_t *const pkt = malloc(UART_MAX_LEN);
    relay_pkt_t *qpkt;
    const int fd = (int)(intptr_t)param;
    ssize_t len, rlen;

    memset(pkt, 0, UART_MAX_LEN);

    while (1) {
        len = 0;

        do {
            rlen = read(fd, &((unsigned char *)pkt)[len], UART_MAX_LEN - (size_t)len);

            if (rlen <= 0) {
                if (errno == EINTR) continue;
                fprintf(stderr, "uart rx: rlen=%li len=%li\n", rlen, len);
                goto done;
            }

            len += rlen;
        } while (len < sizeof(relay_pkt_t) || len < pkt->hdr.len);

        if (pkt->hdr.type != PKT_TYPE_RELAY) {
            fprintf(stderr, "uart rx: type=%i expected=%i\n", pkt->hdr.type, PKT_TYPE_RELAY);
            break;
        }

        if (!pkt->port || !pkt->addr) {
            struct in_addr pkt_addr = { pkt->addr };
            fprintf(stderr, "uart rx: bad addr or port %s:%u\n", inet_ntoa(pkt_addr), ntohs(pkt->port));
            continue;
        }

        qpkt = malloc((size_t)len);
        memcpy(qpkt, pkt, (size_t)len);
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
    relay_pkt_t *pkt;
    const int fd = (int)(intptr_t)param;
    struct sockaddr_in *addr;
    int len;

    while (1) {
        pthread_mutex_lock(&queue.lock);

        while (!queue.cnt)
            pthread_cond_wait(&queue.can_consume, &queue.lock);

        pkt = queue.pkt[--queue.cnt];
        queue.pkt[queue.cnt + 1] = NULL;
        pthread_cond_signal(&queue.can_produce);
        pthread_mutex_unlock(&queue.lock);

        if (!pkt) {
            fprintf(stderr, "udp tx: pkt == NULL\n");
            break;
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
        }

        free(pkt);
        //putchar('+'), fflush(stdout);
    }

    return NULL;
}
