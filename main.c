#include "local.h"
#include "serial.h"
#include "udp.h"
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

#define UART_MAX_LEN 2048
#define MAX_QUEUE 16

typedef struct __attribute__((packed)) {
    unsigned short len;
    unsigned short port;
    unsigned char data[];
} pkt_t;

typedef struct __attribute__((packed)) {
    unsigned short len;
    unsigned short port;
    struct timespec ts[2];
    unsigned char data[];
} analysis_pkt_t;

typedef struct {
    pkt_t *pkt[MAX_QUEUE];
    size_t cnt;
    pthread_mutex_t lock;
    pthread_cond_t can_produce;
    pthread_cond_t can_consume;
    pthread_t thr_prod;
    pthread_t thr_cons;
} pkt_queue_t;

typedef struct {
    unsigned int cnt;
    struct timespec sum;
} analysis_timing_t;

typedef struct {
    bool enabled;
    bool fork;
    bool listen;
    size_t pkt_len;
    long delay_ns;
    struct addrinfo *addr_listen;
    in_port_t addr_listen_port;
    analysis_timing_t t_air, t_sys, t_net, t_all;
    pthread_t thr_tx;
    pthread_t thr_rx;
} analysis_t;

static analysis_t *analyze;

static struct addrinfo *addr;
static in_port_t addr_port;

static pkt_queue_t queue = {
        .cnt = 0,
        .lock = PTHREAD_MUTEX_INITIALIZER,
        .can_produce = PTHREAD_COND_INITIALIZER,
        .can_consume = PTHREAD_COND_INITIALIZER
};

static void *queue_rx_thread(void *param);
static void *queue_tx_thread(void *param);
static void *analyze_tx_thread(void *param);
static void *analyze_rx_thread(void *param);

static inline void ts_diff(struct timespec *dst, struct timespec *a, struct timespec *b)
{
    if ((b->tv_nsec - a->tv_nsec) < 0) {
        dst->tv_sec = b->tv_sec - a->tv_sec - 1;
        dst->tv_nsec = 1000000000 + b->tv_nsec - a->tv_nsec;
    } else {
        dst->tv_sec = b->tv_sec - a->tv_sec;
        dst->tv_nsec = b->tv_nsec - a->tv_nsec;
    }
}

static inline void ts_add(struct timespec *dst, struct timespec *a, struct timespec *b)
{
    dst->tv_sec = a->tv_sec + b->tv_sec;
    dst->tv_nsec = a->tv_nsec + b->tv_nsec;

    while (dst->tv_nsec >= 1000000000) {
        dst->tv_sec++;
        dst->tv_nsec -= 1000000000;
    }
}

static void queue_start(int fd_uart_in, int fd_sock_out)
{
    if (fd_uart_in > -1)
        pthread_create(&queue.thr_prod, NULL, queue_rx_thread, (void *)fd_uart_in);

    if (fd_sock_out > -1)
        pthread_create(&queue.thr_cons, NULL, queue_tx_thread, (void *)fd_sock_out);
}

static void queue_join(void)
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
    pkt_t *const pkt = malloc(UART_MAX_LEN);
    pkt_t *qpkt;
    const int fd = (int)param;
    const bool analyzing = analyze->enabled;
    struct timespec ts = { 0 };
    ssize_t len, rlen;

    memset(pkt, 0, UART_MAX_LEN);

    while (1) {
        len = 0;

        do {
            rlen = read(fd, &((unsigned char *)pkt)[len], UART_MAX_LEN - (size_t)len);

            if (rlen < 0) {
                if (errno == EINTR) continue;
                fprintf(stderr, "uart rx: rlen=%li len=%li\n", rlen, len);
                goto done;
            }

            len += rlen;
        } while (len < sizeof(pkt_t) || len < (sizeof(pkt_t) + pkt->len));

        if (pkt->len != (len - sizeof(pkt_t))) {
            fprintf(stderr, "uart rx: len=%li plen=%i\n", len, pkt->len);
            break;
        }

        if (pkt->port != addr_port) continue;

        if (analyzing) {
            analysis_pkt_t *const a_pkt = (analysis_pkt_t *)pkt;

            if (len < sizeof(analysis_pkt_t)) {
                fprintf(stderr, "uart rx: len=%li min=%lu\n", len, sizeof(analysis_pkt_t));
                break;
            }

            clock_gettime(CLOCK_REALTIME, &a_pkt->ts[1]);
            //printf("uart rx: l=%li pl=%u %li %li\n", len, pkt->len, ((analysis_pkt_t *)pkt)->ts.tv_sec, ((analysis_pkt_t *)pkt)->ts.tv_nsec);
            ts_diff(&ts, &a_pkt->ts[0], &a_pkt->ts[1]);
            ts_add(&analyze->t_air.sum, &analyze->t_air.sum, &ts);
            analyze->t_air.cnt++;
            clock_gettime(CLOCK_REALTIME, &a_pkt->ts[1]);
        }

        //putchar('-'), fflush(stdout);

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
    pkt_t *pkt;
    const int fd = (int)param;
    const bool analyzing = analyze->enabled;
    struct timespec ts = { 0 };
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

        if (analyzing) {
            analysis_pkt_t *const a_pkt = (analysis_pkt_t *)pkt;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts_diff(&ts, &a_pkt->ts[1], &ts);
            ts_add(&analyze->t_sys.sum, &analyze->t_sys.sum, &ts);
            analyze->t_sys.cnt++;
            clock_gettime(CLOCK_REALTIME, &a_pkt->ts[1]);
        }

        retry:
        len = udp_send(fd, addr, pkt, sizeof(pkt_t) + pkt->len);

        if (len != (sizeof(pkt_t) + pkt->len)) {
            if (len < 0 && errno == EINTR) goto retry;
            fprintf(stderr, "udp tx: len=%i exp=%li\n", len, (sizeof(pkt_t) + pkt->len));
            //free(pkt);
            break;
        }

        free(pkt);
        //putchar('+'), fflush(stdout);
    }

    return NULL;
}

static void analyze_start(int fd_uart_out, int fd_sock_in)
{
    if (fd_uart_out > -1)
        pthread_create(&analyze->thr_tx, NULL, analyze_tx_thread, (void *)fd_uart_out);

    if (fd_sock_in > -1)
        pthread_create(&analyze->thr_rx, NULL, analyze_rx_thread, (void *)fd_sock_in);
}

static void analyze_join(void)
{
    if (analyze->thr_tx) {
        pthread_cancel(analyze->thr_tx);
        pthread_join(analyze->thr_tx, NULL);
    }

    if (analyze->thr_rx) {
        pthread_cancel(analyze->thr_rx);
        pthread_join(analyze->thr_rx, NULL);
    }
}

static void *analyze_tx_thread(void *param)
{
    const int fd = (int)param;
    const size_t pkt_len = analyze->pkt_len;
    analysis_pkt_t *const pkt = malloc(pkt_len);
    const struct timespec ts_delay = {
            .tv_sec = 0,
            .tv_nsec = analyze->delay_ns
    };
    ssize_t len, wlen;
    memset(pkt, 0, pkt_len);
    pkt->len = (unsigned short)pkt_len - (unsigned short)sizeof(pkt_t);
    pkt->port = analyze->addr_listen_port;

    while (1) {
        clock_gettime(CLOCK_REALTIME, &pkt->ts[0]);
        len = 0;

        do {
            wlen = write(fd, &((unsigned char *)pkt)[len], pkt_len - len);

            if (wlen <= 0) {
                if (errno == EINTR) continue;
                fprintf(stderr, "uart tx: wlen=%li len=%li exp=%li\n", wlen, len, pkt_len);
                goto done;
            }

            len += wlen;
        } while (len < pkt_len);

        if (ts_delay.tv_nsec) nanosleep(&ts_delay, NULL);
    }

    done:
    //free(pkt);
    return NULL;
}

static void *analyze_rx_thread(void *param)
{
    const int fd = (int)param;
    struct sockaddr_in sin = { 0 };
    const size_t pkt_len = UART_MAX_LEN;
    analysis_pkt_t *const pkt = malloc(pkt_len);
    struct timespec ts[2] = { { 0 }, { 0 } };
    int len;

    while (1) {
        len = udp_recv(fd, &sin, pkt, pkt_len);

        if (len < sizeof(analysis_pkt_t) || pkt->len != (len - sizeof(pkt_t))) {
            if (len < 0 && errno == EINTR) continue;
            if (len >= sizeof(pkt_t)) {
                fprintf(stderr, "udp rx: len=%i plen=%i exp=%li\n", len, pkt->len, analyze->pkt_len);
            } else {
                fprintf(stderr, "udp rx: len=%i exp=%li\n", len, analyze->pkt_len);
            }

            break;
        }

        if (pkt->port != analyze->addr_listen_port) continue;

        clock_gettime(CLOCK_REALTIME, &ts[0]);

        if (pkt->ts[1].tv_sec || pkt->ts[1].tv_nsec) {
            ts_diff(&ts[1], &pkt->ts[1], &ts[0]);
            ts_add(&analyze->t_net.sum, &analyze->t_net.sum, &ts[1]);
            analyze->t_net.cnt++;
        }

        ts_diff(&ts[0], &pkt->ts[0], &ts[0]);
        ts_add(&analyze->t_all.sum, &analyze->t_all.sum, &ts[0]);
        analyze->t_all.cnt++;
    }

    //free(pkt);
    return NULL;
}

static void print_analyze_stats(const char *name, const analysis_timing_t *t)
{
    if (t->cnt) {
        double ms;
        ms = t->sum.tv_sec * 1000ULL + t->sum.tv_nsec / (double)1000000;
        ms /= t->cnt;
        printf("%s: %05.2f ms\t\t%u\n", name, ms, t->cnt);
    }
}

/*
static void key_setup(void)
{
    FILE* urandom = fopen("/dev/urandom", "r");
    fread(&key, sizeof(key), 1, urandom);
    fclose(urandom);
}
*/

int main(int argc, char *argv[])
{
    yuck_t argp[1];
    speed_t speed;
    int fd_uart_in = -1, fd_uart_out = -1, fd_sock_in = -1, fd_sock_out = -1;
    int cpid = 0;

    analyze = mmap(NULL, sizeof(*analyze), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    memset(analyze, 0, sizeof(*analyze));

    yuck_parse(argp, argc, argv);

    if (!argp->host_arg || !strlen(argp->host_arg)) {
        if (argp->cmd != SERNET_CMD_ANALYZE) {
            fputs("host is required\n", stderr);
            exit(1);
        } else {
            argp->host_arg = NULL;
        }
    }

    if (!argp->port_arg || !strlen(argp->port_arg)) {
        argp->port_arg = "42100";
    }

    if (!argp->input_arg || !strlen(argp->input_arg)) {
        if (argp->cmd != SERNET_CMD_ANALYZE) {
            fputs("input is required\n", stderr);
            exit(2);
        } else {
            argp->input_arg = NULL;
        }
    }

    if (!argp->rate_arg || !strlen(argp->rate_arg)) {
        argp->rate_arg = "115200";
    }

    const long baud = strtol(argp->rate_arg, &argp->rate_arg, 10);

    if (errno || *argp->rate_arg || baud <= 0 || !(speed = btos((unsigned int)baud))) {
        if (errno) {
            perror("strtol");
            exit(3);
        } else {
            fputs("invalid baud rate\n", stderr);
            exit(4);
        }
    }

    if (argp->cmd == SERNET_CMD_ANALYZE) {
        analyze->enabled = true;

        if (!argp->analyze.output_arg || !strlen(argp->analyze.output_arg)) {
            fputs("output is required\n", stderr);
            exit(5);
        }

        if (!argp->analyze.size_arg || !strlen(argp->analyze.size_arg)) {
            argp->analyze.size_arg = malloc(10); // LEAK
            snprintf(argp->analyze.size_arg, 9, "%lu", sizeof(analysis_pkt_t));
        }

        long pkt_len = strtol(argp->analyze.size_arg, &argp->analyze.size_arg, 10);

        if (errno || *argp->analyze.size_arg || pkt_len < sizeof(analysis_pkt_t) || pkt_len > UART_MAX_LEN) {
            if (errno) {
                perror("strtol");
                exit(6);
            } else {
                fprintf(stderr, "invalid output packet size: min=%lu max=%u\n",
                        sizeof(analysis_pkt_t), UART_MAX_LEN);
                exit(7);
            }
        } else {
            analyze->pkt_len = (size_t)pkt_len;
        }

        if (!argp->analyze.delay_arg || !strlen(argp->analyze.delay_arg)) {
            argp->analyze.delay_arg = "250";
        }

        long delay_ns = 1000000 * strtol(argp->analyze.delay_arg, &argp->analyze.delay_arg, 10);

        if (errno || *argp->analyze.delay_arg || delay_ns < 0 || delay_ns > 999999999) {
            if (errno) {
                perror("strtol");
                exit(8);
            } else {
                fputs("invalid output delay\n", stderr);
                exit(9);
            }
        } else {
            analyze->delay_ns = delay_ns;
        }

        if (argp->analyze.listen_arg) {
            analyze->listen = true;

            if (!*argp->analyze.listen_arg) {
                argp->analyze.listen_arg = "localhost";
            }
        }

        if (argp->analyze.fork_flag) {
            analyze->fork = true;
        }
    }

    if (analyze->enabled) {
        fd_uart_out = serial_open(argp->analyze.output_arg);
        if (fd_uart_out == -1) exit(-10);
        if (serial_setup(fd_uart_out, speed, 0, -1)) exit(-20);

        if (analyze->listen) {
            if (udp_addr(&analyze->addr_listen, argp->analyze.listen_arg, argp->port_arg, true)) exit(-30);
            analyze->addr_listen_port = addr_inet_port(analyze->addr_listen);
            fd_sock_in = udp_open(analyze->addr_listen);
            if (fd_sock_in == -1) exit(-40);
            if (udp_bind(fd_sock_in, analyze->addr_listen)) exit(-50);
        }
    }

    if (argp->input_arg) {
        fd_uart_in = serial_open(argp->input_arg);
        if (fd_uart_in == -1) exit(-11);
        if (serial_setup(fd_uart_in, speed, 0, -1)) exit(-21);
    }

    if (argp->host_arg) {
        if (udp_addr(&addr, argp->host_arg, argp->port_arg, false)) exit(-31);
        addr_port = addr_inet_port(addr);
        fd_sock_out = udp_open(addr);
        if (fd_sock_out == -1) exit(-41);

        if (argp->broadcast_flag) {
            int bcast = 1;
            if (setsockopt(fd_sock_out, SOL_SOCKET, SO_BROADCAST, (void *)&bcast, sizeof(bcast)) < 0) {
                perror("setsockopt(SO_BROADCAST)");
                exit(-51);
            }
        }
    }

    yuck_free(argp);
    queue_start(fd_uart_in, fd_sock_out);

    if (analyze->enabled) {
        if (!analyze->fork) {
            analyze_start(fd_uart_out, fd_sock_in);
            while (getchar() != '\n');
            analyze_join();
            if (fd_uart_out > -1) close(fd_uart_out);
            if (fd_sock_in > -1) close(fd_sock_in);
        } else if (!(cpid = fork())) {
            prctl(PR_SET_PDEATHSIG, SIGKILL);
            if (fd_uart_in > -1) close(fd_uart_in);
            if (fd_sock_out > -1) close(fd_sock_out);
            analyze_start(fd_uart_out, fd_sock_in);
            while (getchar() != '\n');
            analyze_join();
            if (fd_uart_out > -1) close(fd_uart_out);
            if (fd_sock_in > -1) close(fd_sock_in);
            exit(0);
        } else {
            if (cpid <= 0) {
                perror("fork");
                exit(-41);
            }

            if (fd_uart_out > -1) close(fd_uart_out);
            if (fd_sock_in > -1) close(fd_sock_in);
            waitpid(cpid, NULL, 0);
        }
    } else while (getchar() != '\n');

    queue_join();
    if (fd_uart_in > -1) close(fd_uart_in);
    if (fd_sock_out > -1) close(fd_sock_out);

    if (analyze->enabled) {
        print_analyze_stats("air", &analyze->t_air);
        print_analyze_stats("sys", &analyze->t_sys);
        print_analyze_stats("net", &analyze->t_net);
        print_analyze_stats("all", &analyze->t_all);
    }

    return 0;
}
