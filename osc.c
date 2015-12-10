#include "local.h"
#include "udp.h"
#include "msg.h"
#include "packet.h"
#include "osc.h"
#include "osc/tinyosc.h"

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


#define OSC_MAX_QUEUE       16


typedef struct {
    msg_t *msg[OSC_MAX_QUEUE];
    size_t cnt;
    pthread_mutex_t lock;
    pthread_cond_t can_produce;
    pthread_cond_t can_consume;
    pthread_t thr_prod;
    pthread_t thr_cons;
} osc_queue_t;


static void *osc_rx_thread(void *param);
static void *osc_tx_thread(void *param);


static osc_queue_t osc_queue = {
        .cnt = 0,
        .lock = PTHREAD_MUTEX_INITIALIZER,
        .can_produce = PTHREAD_COND_INITIALIZER,
        .can_consume = PTHREAD_COND_INITIALIZER
};


void osc_start(osc_param_t *p)
{
    pthread_create(&osc_queue.thr_prod, NULL, osc_rx_thread, (void *)p);
    pthread_create(&osc_queue.thr_cons, NULL, osc_tx_thread, (void *)p);
}

void osc_stop(void)
{
    if (osc_queue.thr_prod) {
        pthread_cancel(osc_queue.thr_prod);
        pthread_join(osc_queue.thr_prod, NULL);
    }

    if (osc_queue.thr_cons) {
        pthread_cancel(osc_queue.thr_cons);
        pthread_join(osc_queue.thr_cons, NULL);
    }
}

static void *osc_rx_thread(void *param)
{
    msg_t *msg = malloc(UART_MAX_LEN), *qmsg;
    const int fd = ((osc_param_t *)param)->fd_rx;
    ssize_t len;

    set_thread_prio(true);

    while (1) {
        len = recv(fd, msg, UART_MAX_LEN, MSG_NOSIGNAL | MSG_WAITALL);

        if (len <= 0) {
            if (errno == ENOTCONN) {
                sleep(1);
                continue;
            }

            if (errno) perror("recv");
            else fprintf(stderr, "unix rx: len=%li\n", len);
            return NULL;
        }

        qmsg = malloc((size_t)msg->hdr.len);
        memcpy(qmsg, msg, (size_t)msg->hdr.len);
        pthread_mutex_lock(&osc_queue.lock);

        while (osc_queue.cnt >= OSC_MAX_QUEUE)
            pthread_cond_wait(&osc_queue.can_produce, &osc_queue.lock);

        osc_queue.msg[osc_queue.cnt] = qmsg;
        osc_queue.cnt++;

        pthread_cond_signal(&osc_queue.can_consume);
        pthread_mutex_unlock(&osc_queue.lock);
    }

    return NULL;
}

static void *osc_tx_thread(void *param)
{
    const osc_param_t *const p = (osc_param_t *)param;
    const int fd = p->fd_tx;
    const struct sockaddr_in *const addr = &p->addr_osc;
    buf_t buf[UART_MAX_LEN*4];
    pkt_msg_t *qmsg;
    int len_buf, i;
    ssize_t len_tx;
    u64 src;
    char osc_addr[128];
    char osc_data[UART_MAX_LEN*8];
    char osc_tmp[4];

    set_thread_prio(false);

    while (1) {
        pthread_mutex_lock(&osc_queue.lock);

        while (!osc_queue.cnt)
            pthread_cond_wait(&osc_queue.can_consume, &osc_queue.lock);

        qmsg = (pkt_msg_t *)osc_queue.msg[--osc_queue.cnt];
        osc_queue.msg[osc_queue.cnt + 1] = NULL;
        pthread_cond_signal(&osc_queue.can_produce);
        pthread_mutex_unlock(&osc_queue.lock);

        if (!qmsg) {
            fprintf(stderr, "osc tx: msg == NULL\n");
            break;
        }

        if (qmsg->hdr.type != MSG_TYPE_PKT) {
            free(qmsg);
            continue;
        }

        src = (u64)qmsg->src.addr[7] |
                ((u64)qmsg->src.addr[6] << (8*1)) |
                ((u64)qmsg->src.addr[5] << (8*2)) |
                ((u64)qmsg->src.addr[4] << (8*3)) |
                ((u64)qmsg->src.addr[3] << (8*4)) |
                ((u64)qmsg->src.addr[2] << (8*5)) |
                ((u64)qmsg->src.addr[1] << (8*6)) |
                ((u64)qmsg->src.addr[0] << (8*7));

        sprintf(osc_addr, "/gesture/xbee/%016lX", src);
        len_buf = qmsg->hdr.len - (int)sizeof(*qmsg);
        osc_data[0] = '\0';
        for (i = 0; i < len_buf; i++) {
            sprintf(osc_tmp, "%02X", qmsg->data[i]);
            strcat(osc_data, osc_tmp);
        }

        // TODO: send
        len_buf = tosc_writeMessage((char *)buf, UART_MAX_LEN*4, osc_addr, "s", osc_data);
        if (len_buf <= 0) { fprintf(stderr, "udp tx: osc len=%i", len_buf); free(qmsg); continue; }

        // 10.10.10.161/24:8001
        printf("%s: %s [%li]\n", osc_addr, osc_data, strlen(osc_data)/2);

        send:
        //print_hex(stdout, msg, msg->hdr.len);
        len_tx = udp_send(fd, (struct sockaddr *)addr, sizeof(*addr), buf, (size_t)len_buf);

        if (len_tx < len_buf) {
            if (len_tx < 0 && errno == EINTR) goto send;
            fprintf(stderr, "udp tx: len=%li exp=%u\n", len_tx, len_buf);
            exit(-1);
        }

        free(qmsg);
    }

    return NULL;
}
