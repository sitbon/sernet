#include "local.h"
#include "udp.h"
#include "msg.h"
#include "packet.h"
#include "dst.h"

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
#define MAX_DEDUP       16

typedef struct {
    msg_t *msg[MAX_QUEUE];
    size_t cnt;
    pthread_mutex_t lock;
    pthread_cond_t can_produce;
    pthread_cond_t can_consume;
    pthread_t thr_prod;
    pthread_t thr_cons;
} msg_queue_t;

typedef struct {
    pkt_addr_t          src;
    struct timespec     stm[UINT8_MAX+1];
} dedup_t;

typedef struct sockaddr_in addr_cache_t;

static msg_queue_t queue = {
        .cnt = 0,
        .lock = PTHREAD_MUTEX_INITIALIZER,
        .can_produce = PTHREAD_COND_INITIALIZER,
        .can_consume = PTHREAD_COND_INITIALIZER
};

const static pkt_addr_t PKT_ADDR_NONE;

static addr_cache_t addr_cache[MAX_ADDR_CACHE];
static dedup_t dedup[MAX_DEDUP];
static u8 dedup_len;
static u8 dedup_rc;

static void *queue_rx_thread(void *param);
static void *queue_tx_thread(void *param);
static void handle_relay(int fd, relay_msg_t *msg);
static void handle_packet(int fd, const struct sockaddr_un *addr, pkt_msg_t *msg);


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

void dst_start(dst_param_t *p)
{
    pthread_create(&queue.thr_prod, NULL, queue_rx_thread, (void *)p);
    pthread_create(&queue.thr_cons, NULL, queue_tx_thread, (void *)p);
}

void dst_stop(void)
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
    const dst_param_t *const p = (dst_param_t *)param;
    const int fd = p->fd_rx;
    struct sockaddr_in sin = { 0 };
    socklen_t sin_len;
    msg_t *const msg = malloc(UART_MAX_LEN), *qmsg;
    struct timespec ts = { 0 }, tsd;
    pkt_addr_t src;
    seq_t seq;
    u8 i;
    int len;

    set_thread_prio(true);

    while (1) {
        recv:
        sin_len = sizeof(sin);
        len = udp_recv(fd, (struct sockaddr *)&sin, &sin_len, msg, UART_MAX_LEN);

        if (len < sizeof(msg_t) || len != msg->hdr.len) {
            if (len < 0 && errno == EINTR) continue;
            if (!len) break;

            if (len >= sizeof(msg_hdr_t)) {
                fprintf(stderr, "udp rx: len=%i plen=%i\n", len, msg->hdr.len);
            } else {
                fprintf(stderr, "udp rx: len=%i min=%li\n", len, sizeof(msg_t));
            }

            continue;
        }

        switch (msg->hdr.type) {
            case MSG_TYPE_PKT:
                if (msg->hdr.len < sizeof(pkt_msg_t)) {
                    fprintf(stderr, "udp rx: len=%i min=%li\n", len, sizeof(pkt_msg_t));
                    continue;
                }

                src = ((pkt_msg_t *)msg)->src;
                seq = ((pkt_msg_t *)msg)->data[2]; // TODO: Identify air packet and correct seq index
                break;
            case MSG_TYPE_RELAY:
                if (msg->hdr.len < sizeof(relay_msg_t)) {
                    fprintf(stderr, "udp rx: len=%i min=%li\n", len, sizeof(relay_msg_t));
                    continue;
                }

                //print_hex(stdout, &((relay_msg_t *)msg)->relay, sizeof(relay_t));
                src = ((relay_msg_t *)msg)->src;
                seq = ((relay_msg_t *)msg)->relay.seq;
                break;
            default:
                continue;
        }

        clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
        //fprintf(stderr, "seq=%i t = %lu/%lu\n", seq, ts.tv_sec, ts.tv_nsec);

        for (i = 0; i < dedup_len; i++) {
            if (!memcmp(&src, &dedup[i].src, sizeof(src))) {
                if (!dedup[i].stm[seq].tv_sec) {
                    dedup[i].stm[seq] = ts;
                    break;
                }

                ts_diff(&tsd, &dedup[i].stm[seq], &ts);
                dedup[i].stm[seq] = ts;

                if (!tsd.tv_sec && tsd.tv_nsec <= 80000000) {
                    //TODO: in some cases with just one sender this is happening a lot even
                    //      though no duplicates are sent and everything makes it back to the
                    //      relay target.
                    //fprintf(stderr, "diff %lu/%lu\n", tsd.tv_sec, tsd.tv_nsec);
                    goto recv;
                }
                break;
            }
        }

        if (i == dedup_len) {
            if (++dedup_len >= MAX_DEDUP) {
                dedup_len--;
                i = dedup_rc++;
                memset(&dedup[i].stm, 0, sizeof(dedup[i].stm));
            }
            dedup[i].src = src;
            dedup[i].stm[seq] = ts;
        }

        //putchar('-'); fflush(stdout);
        //printf("-%i", msg->hdr.len);

        qmsg = malloc((size_t)msg->hdr.len);
        memcpy(qmsg, msg, (size_t)msg->hdr.len);
        pthread_mutex_lock(&queue.lock);

        while (queue.cnt >= MAX_QUEUE)
            pthread_cond_wait(&queue.can_produce, &queue.lock);

        queue.msg[queue.cnt] = qmsg;
        queue.cnt++;

        pthread_cond_signal(&queue.can_consume);
        pthread_mutex_unlock(&queue.lock);
    }



    done:
    return NULL;
}

static void *queue_tx_thread(void *param)
{
    const dst_param_t *const dst = (dst_param_t *)param;
    const int fd_in = dst->fd_tx_in;
    const int fd_un = dst->fd_tx_un;
    msg_t *qmsg;

    const struct sockaddr_un *const addr_un = &dst->addr_un;

    set_thread_prio(false);

    while (1) {
        pthread_mutex_lock(&queue.lock);

        while (!queue.cnt)
            pthread_cond_wait(&queue.can_consume, &queue.lock);

        qmsg = queue.msg[--queue.cnt];
        queue.msg[queue.cnt + 1] = NULL;
        pthread_cond_signal(&queue.can_produce);
        pthread_mutex_unlock(&queue.lock);

        if (!qmsg) {
            fprintf(stderr, "udp tx: msg == NULL\n");
            break;
        }

        switch (qmsg->hdr.type) {
            case MSG_TYPE_RELAY:
                handle_relay(fd_in, (relay_msg_t *)qmsg);
                break;
            case MSG_TYPE_PKT:
                handle_packet(fd_un, addr_un, (pkt_msg_t *)qmsg);
                break;
        }

        free(qmsg);
    }

    return NULL;
}

static void handle_relay(int fd, relay_msg_t *msg)
{
    struct sockaddr_in *addr;
    ssize_t len;

    if (!msg->relay.port || !msg->relay.src) {
        struct in_addr msg_addr = { msg->relay.src };
        fprintf(stderr, "udp rx: bad addr or port %s:%u\n", inet_ntoa(msg_addr), ntohs(msg->relay.port));
        return;
    }

    addr = get_addr(msg->relay.src, msg->relay.port);

    if (!addr) {
        struct in_addr msg_addr = { msg->relay.src };
        fprintf(stderr, "udp rx: get_addr(%s, %u) == NULL\n", inet_ntoa(msg_addr), ntohs(msg->relay.port));
        return;
    }

    retry:
    len = udp_send(fd, (struct sockaddr *)addr, sizeof(*addr), msg, msg->hdr.len);

    if (len < msg->hdr.len) {
        if (len < 0 && errno == EINTR) goto retry;
        fprintf(stderr, "udp tx: len=%li exp=%u\n", len, msg->hdr.len);
        return;
    }

    //putchar('^'); fflush(stdout);
}

static void handle_packet(int fd, const struct sockaddr_un *addr, pkt_msg_t *msg)
{
    ssize_t len;

    retry:
    len = sendto(fd, msg, msg->hdr.len, MSG_NOSIGNAL | MSG_WAITALL, (struct sockaddr *)addr, sizeof(*addr));//write(fd, msg, msg->hdr.len);

    if (len < msg->hdr.len) {
        if (len < 0) {
            if (errno == EINTR) goto retry;
            perror("sendto");
        } else
            fprintf(stderr, "unix tx: len=%li exp=%u\n", len, msg->hdr.len);
        return;
    }

    //putchar('>'); fflush(stdout);
}
