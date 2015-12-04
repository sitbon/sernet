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
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/ioctl.h>

typedef struct {
    speed_t speed;
} param_t;

typedef struct {
    unsigned int cnt_exp;
    unsigned int cnt;
    struct timespec sum;
} timing_t;

typedef struct {
    size_t pkt_len;
    long delay_ns;
    struct sockaddr_in addr_listen;
    timing_t t_rtt;
    pthread_t thr_tx;
    pthread_t thr_rx;
} source_t;

extern void relay_start(int fd_uart_in, int fd_sock_out);
extern void relay_stop(void);
static void *source_tx_thread(void *param);
static void *source_rx_thread(void *param);
static void main_fwd(yuck_t *argp);
static void main_src(yuck_t *argp);

static param_t  param;
static source_t source;

static void source_start(int fd_uart_out, int fd_sock_in)
{
    if (fd_uart_out > -1)
        pthread_create(&source.thr_tx, NULL, source_tx_thread, (void *)(intptr_t)fd_uart_out);

    if (fd_sock_in > -1)
        pthread_create(&source.thr_rx, NULL, source_rx_thread, (void *)(intptr_t)fd_sock_in);
}

static void source_stop(void)
{
    if (source.thr_tx) {
        pthread_cancel(source.thr_tx);
        pthread_join(source.thr_tx, NULL);
    }

    if (source.thr_rx) {
        pthread_cancel(source.thr_rx);
        pthread_join(source.thr_rx, NULL);
    }
}

static void *source_tx_thread(void *param)
{
    const int fd = (int)(intptr_t)param;
    const size_t pkt_len = source.pkt_len;
    relay_pkt_t *const pkt = malloc(pkt_len);
    const struct timespec ts_delay = {
            .tv_sec = 0,
            .tv_nsec = source.delay_ns
    };
    ssize_t len, wlen;
    memset(pkt, 0, pkt_len);
    pkt->hdr.len = (pkt_len_t)pkt_len;
    pkt->hdr.type = PKT_TYPE_RELAY;
    pkt->addr = source.addr_listen.sin_addr.s_addr;
    pkt->port = source.addr_listen.sin_port;

    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &pkt->ts);
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

        source.t_rtt.cnt_exp++;
        if (ts_delay.tv_nsec) nanosleep(&ts_delay, NULL);
    }

    done:
    //free(pkt);
    return NULL;
}

static void *source_rx_thread(void *param)
{
    const int fd = (int)(intptr_t)param;
    struct sockaddr_in sin = { 0 };
    socklen_t sin_len;
    relay_pkt_t *const pkt = malloc(UART_MAX_LEN);
    struct timespec ts = { 0 };
    int len;

    while (1) {
        sin_len = sizeof(sin);
        len = udp_recv(fd, (struct sockaddr *)&sin, &sin_len, pkt, UART_MAX_LEN);

        if (len < sizeof(relay_pkt_t) || len != source.pkt_len) {
            if (len < 0 && errno == EINTR) continue;
            if (!len) break;

            if (len >= sizeof(pkt_hdr_t)) {
                fprintf(stderr, "udp rx: len=%i plen=%i exp=%li\n", len, pkt->hdr.len, source.pkt_len);
            } else {
                fprintf(stderr, "udp rx: len=%i exp=%li\n", len, source.pkt_len);
            }

            continue;
        }

        if (pkt->port != source.addr_listen.sin_port || pkt->addr != source.addr_listen.sin_addr.s_addr) {
            struct in_addr pkt_addr = { pkt->addr };
            fprintf(stderr, "udp rx: dst=%s:%u exp=%s:%u\n",
                    inet_ntoa(pkt_addr), ntohs(pkt->port),
                    inet_ntoa(source.addr_listen.sin_addr), ntohs(source.addr_listen.sin_port));
            continue;
        }

        if (pkt->hdr.type != PKT_TYPE_RELAY) {
            fprintf(stderr, "uart rx: type=%i expected=%i\n", pkt->hdr.type, PKT_TYPE_RELAY);
            break;
        }

        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts_diff(&ts, &pkt->ts, &ts);
        ts_add(&source.t_rtt.sum, &source.t_rtt.sum, &ts);
        source.t_rtt.cnt++;
    }

    //free(pkt);
    return NULL;
}

static void print_src_info(yuck_t *argp, const char *suffix) {
    printf("<src> %s@%s %s:%u %sb/%sms%s",
            argp->src.output_arg, argp->rate_arg,
            inet_ntoa(source.addr_listen.sin_addr), ntohs(source.addr_listen.sin_port),
            argp->src.size_arg, argp->src.delay_arg, suffix
    );
}

static void print_analyze_stats(const char *name, const timing_t *t)
{
    if (t->cnt) {
        double ms;
        ms = t->sum.tv_sec * 1000ULL + t->sum.tv_nsec / (double)1000000;
        ms /= t->cnt;
        if (t->cnt == t->cnt_exp) {
            printf("<%s> %05.2f ms\t\t%u\n", name, ms, t->cnt);
        } else {
            printf("<%s> %05.2f ms\t\t%u/%u\n", name, ms, t->cnt, t->cnt_exp);
        }
    }
}


int main(int argc, char *argv[])
{
    yuck_t argp[1];

    yuck_parse(argp, argc, argv);

    if (!argp->rate_arg || !strlen(argp->rate_arg)) {
        argp->rate_arg = "230400";
    }

    char *pterm;
    const long baud = strtol(argp->rate_arg, &pterm, 10);

    if (errno || *pterm || baud <= 0 || !(param.speed = btos((unsigned int)baud))) {
        if (errno) {
            perror("strtol");
            exit(1);
        } else {
            fputs("invalid baud rate\n", stderr);
            exit(1);
        }
    }

    switch (argp->cmd) {
        case SERNET_CMD_FWD:
            main_fwd(argp);
            break;
        case SERNET_CMD_SRC:
            main_src(argp);
            break;
        default:
            yuck_auto_usage(argp);
            exit(1);
    }

    yuck_free(argp);
    return 0;
}

static void main_fwd(yuck_t *argp)
{
    int fd_uart_in, fd_sock_out;

    if (!argp->fwd.input_arg || !strlen(argp->fwd.input_arg)) {
        fputs("input is required\n", stderr);
        exit(1);
    }

    struct addrinfo *addr_info;
    struct sockaddr_in *addr, baddr;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    if (udp_addr(&addr_info, "0.0.0.0", "0", true)) exit(-1);
    if (!(addr = addr_inet_sockaddr(addr_info))) exit(-1);
    fd_sock_out = udp_open(addr_info->ai_family);
    if (fd_sock_out == -1) exit(-1);
    if (udp_bind(fd_sock_out, (struct sockaddr *)addr, addr_len)) exit(-1);
    if (getsockname(fd_sock_out, (struct sockaddr *)&baddr, &addr_len)) {
        perror("getsockname");
        exit(-1);
    }

    fd_uart_in = serial_open(argp->fwd.input_arg);
    if (fd_uart_in == -1) exit(-1);
    if (serial_setup(fd_uart_in, param.speed, 0, -1)) exit(-1);

    printf("<fwd> %s@%s %s:%u\n",
            argp->fwd.input_arg, argp->rate_arg,
            inet_ntoa(baddr.sin_addr), ntohs(baddr.sin_port)
    );

    relay_start(fd_uart_in, fd_sock_out);
    while (getchar() != '\n');
    relay_stop();
    if (fd_uart_in > -1) close(fd_uart_in);
    if (fd_sock_out > -1) close(fd_sock_out);
}

static void main_src(yuck_t *argp)
{
    int fd_uart_out, fd_sock_in;

    if (!argp->src.output_arg || !strlen(argp->src.output_arg)) {
        fputs("output is required\n", stderr);
        exit(1);
    }

    if (argp->src.listen_arg && !strlen(argp->src.listen_arg)) {
        argp->src.listen_arg = NULL;
    }

    if (!argp->src.port_arg || !strlen(argp->src.port_arg)) {
        argp->src.port_arg = "0";
    }

    if (!argp->src.size_arg || !strlen(argp->src.size_arg)) {
        argp->src.size_arg = malloc(10); // LEAK
        snprintf(argp->src.size_arg, 9, "%lu", sizeof(relay_pkt_t));
    }

    char *pterm;
    long pkt_len = strtol(argp->src.size_arg, &pterm, 10);

    if (errno || *pterm || pkt_len < sizeof(relay_pkt_t) || pkt_len > UART_MAX_LEN) {
        if (errno) {
            perror("strtol");
            exit(1);
        } else {
            fprintf(stderr, "invalid output packet size: min=%lu max=%u\n",
                    sizeof(relay_pkt_t), UART_MAX_LEN);
            exit(1);
        }
    } else {
        source.pkt_len = (size_t)pkt_len;
    }

    if (!argp->src.delay_arg || !strlen(argp->src.delay_arg)) {
        argp->src.delay_arg = "250";
    }

    long delay_ns = 1000000 * strtol(argp->src.delay_arg, &pterm, 10);

    if (errno || *pterm || delay_ns < 0 || delay_ns > 999999999) {
        if (errno) {
            perror("strtol");
            exit(1);
        } else {
            fprintf(stderr, "invalid output delay %s\n", argp->src.delay_arg);
            exit(1);
        }
    } else {
        source.delay_ns = delay_ns;
    }

    struct ifaddrs *addr;

    if (getifaddrs(&addr)) {
        perror("getifaddrs()");
        exit(-1);
    }

    while (addr) {
        if (addr->ifa_addr->sa_family != AF_INET) goto next;
        if (argp->src.listen_arg && strcmp(argp->src.listen_arg, addr->ifa_name)) goto next;

        const in_addr_t in_addr = ntohl(((struct sockaddr_in *)addr->ifa_addr)->sin_addr.s_addr);
        if (in_addr == INADDR_LOOPBACK || in_addr == INADDR_NONE || in_addr == INADDR_ANY) goto next;

        source.addr_listen = *(struct sockaddr_in *)addr->ifa_addr;
        break;

        next:
        addr = addr->ifa_next;
    }

    if (!addr) {
        fputs("no suitable interfaces found", stderr);
        exit(-1);
    }

    fd_sock_in = udp_open(source.addr_listen.sin_family);
    if (fd_sock_in == -1) exit(-1);

    if (udp_bind(fd_sock_in, (struct sockaddr *)&source.addr_listen, sizeof(source.addr_listen))) exit(-1);

    socklen_t addr_len = sizeof(source.addr_listen);

    if (getsockname(fd_sock_in, (struct sockaddr *)&source.addr_listen, &addr_len)) {
        perror("getsockname");
        exit(-1);
    }

    fd_uart_out = serial_open(argp->src.output_arg);
    if (fd_uart_out == -1) exit(-1);
    if (serial_setup(fd_uart_out, param.speed, 0, -1)) exit(-1);

    print_src_info(argp, "\n");

    source_start(fd_uart_out, fd_sock_in);
    while (getchar() != '\n');
    source_stop();
    if (fd_uart_out > -1) close(fd_uart_out);
    if (fd_sock_in > -1) close(fd_sock_in);

    print_src_info(argp, "  ");
    print_analyze_stats("rtt", &source.t_rtt);
}
