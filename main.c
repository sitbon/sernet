#include "local.h"
#include "serial.h"
#include "udp.h"
#include "msg.h"
#include "src.h"
#include "dst.h"
#include "sernet.yucc"
#include "packet.h"

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

static void main_dst(yuck_t *argp);
static void main_fwd(yuck_t *argp);
static void main_src(yuck_t *argp);

static param_t cfg;

static void print_src_info(yuck_t *argp, src_param_t *p, const char *suffix) {
    printf("<src> %s@%s %s:%u %sb/%sms%s",
            argp->src.output_arg, argp->rate_arg,
            inet_ntoa(p->addr_listen.sin_addr), ntohs(p->addr_listen.sin_port),
            argp->src.size_arg, argp->src.delay_arg, suffix
    );
}

void print_analyze_stats(const char *name, const timing_t *t)
{
    double ms = 0;

    if (t->cnt) {
        ms = t->sum.tv_sec * 1000ULL + t->sum.tv_nsec / (double)1000000;
        ms /= t->cnt;
    }

    if (t->cnt == t->cnt_exp) {
        printf("<%s> %06.3f ms\t\t%u\n", name, ms, t->cnt);
    } else {
        printf("<%s> %06.3f ms\t\t%u/%u\n", name, ms, t->cnt, t->cnt_exp);
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

    if (errno || *pterm || baud <= 0 || !(cfg.speed = btos((unsigned int)baud))) {
        if (errno) {
            perror("strtol");
            exit(1);
        } else {
            fputs("invalid baud rate\n", stderr);
            exit(1);
        }
    }

    switch (argp->cmd) {
        case SERNET_CMD_DST:
            main_dst(argp);
            break;
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

static void main_dst(yuck_t *argp)
{
    int fd_sock_in, fd_sock_out;
    dst_param_t param = { 0 };

    if (!argp->dst.sock_arg || !strlen(argp->dst.sock_arg)) {
        argp->dst.sock_arg = "sernet";
    }

    if (!argp->dst.port_arg || !strlen(argp->dst.port_arg)) {
        argp->dst.port_arg = "4434";
    }

    struct addrinfo *addr_info;
    struct sockaddr_in *addr, baddr;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    if (udp_addr(&addr_info, "0.0.0.0", argp->dst.port_arg, true)) exit(-1);
    if (!(addr = addr_inet_sockaddr(addr_info))) exit(-1);
    fd_sock_in = udp_open(addr_info->ai_family);
    if (fd_sock_in == -1) exit(-1);
    if (udp_bind(fd_sock_in, (struct sockaddr *)addr, addr_len)) exit(-1);
    baddr = *addr;
    if (getsockname(fd_sock_in, (struct sockaddr *)&baddr, &addr_len)) {
        perror("getsockname");
        exit(-1);
    }

    param.addr_un.sun_family = AF_UNIX;
    param.addr_un.sun_path[0] = 0;
    strcpy(&param.addr_un.sun_path[1], argp->dst.sock_arg);
    fd_sock_out = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd_sock_out == -1) { perror("socket"); exit(-1); }

    printf("<dst> %s:%u -> unix://%s\n",
            inet_ntoa(baddr.sin_addr), ntohs(baddr.sin_port),
            argp->dst.sock_arg
    );

    param.fd_rx = fd_sock_in;
    param.fd_tx_in = fd_sock_in;
    param.fd_tx_un = fd_sock_out;

    dst_start(&param);
    while (getchar() != '\n');
    dst_stop();
    close(fd_sock_in);
    close(fd_sock_out);
}

static void main_fwd(yuck_t *argp)
{
    int fd_uart_in, fd_sock_out;

    if (!argp->fwd.input_arg || !strlen(argp->fwd.input_arg)) {
        fputs("input is required\n", stderr);
        exit(1);
    }

    if (argp->fwd.host_arg && !strlen(argp->fwd.host_arg)) {
        fputs("host is required\n", stderr);
        exit(1);
    }

    if (!argp->fwd.port_arg || !strlen(argp->fwd.port_arg)) {
        argp->fwd.port_arg = "4434";
    }

    struct addrinfo *addr_info;
    struct sockaddr_in *addr, baddr;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    if (udp_addr(&addr_info, argp->fwd.host_arg, argp->fwd.port_arg, false)) exit(-1);
    if (!(addr = addr_inet_sockaddr(addr_info))) exit(-1);
    fd_sock_out = udp_open(addr_info->ai_family);
    if (fd_sock_out == -1) exit(-1);
    //if (udp_bind(fd_sock_out, (struct sockaddr *)addr, addr_len)) exit(-1);
    baddr = *addr;
    /*if (getsockname(fd_sock_out, (struct sockaddr *)&baddr, &addr_len)) {
        perror("getsockname");
        exit(-1);
    }*/

    fd_uart_in = serial_open(argp->fwd.input_arg);
    if (fd_uart_in == -1) exit(-1);
    if (serial_setup(fd_uart_in, cfg.speed, 0, -1)) exit(-1);

    printf("<fwd> %s@%s %s:%u\n",
            argp->fwd.input_arg, argp->rate_arg,
            inet_ntoa(baddr.sin_addr), ntohs(baddr.sin_port)
    );

    fwd_param_t param = {
            .fd_rx = fd_uart_in,
            .fd_tx = fd_sock_out,
            .addr = baddr
    };

    fwd_start(&param);
    while (getchar() != '\n');
    fwd_stop();
    close(fd_uart_in);
    close(fd_sock_out);
}

static void main_src(yuck_t *argp)
{
    int fd_uart_out, fd_sock_in;
    src_param_t param = { 0 };

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
        snprintf(argp->src.size_arg, 9, "%lu", sizeof(relay_pkt_data_t));
    }

    char *pterm;
    long pkt_len = strtol(argp->src.size_arg, &pterm, 10);

    if (errno || *pterm || pkt_len < sizeof(relay_pkt_data_t) || pkt_len >= UART_MAX_LEN) {
        if (errno) {
            perror("strtol");
            exit(1);
        } else {
            fprintf(stderr, "invalid output packet size: min=%lu max=%u\n",
                    sizeof(relay_pkt_data_t), UART_MAX_LEN-1);
            exit(1);
        }
    } else {
        param.pkt_len = (size_t)pkt_len;
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
        param.delay_ns = delay_ns;
    }

    struct ifaddrs *addr;

    if (getifaddrs(&addr)) {
        perror("getifaddrs()");
        exit(-1);
    }

    while (addr) {
        if (!addr->ifa_addr || addr->ifa_addr->sa_family != AF_INET) goto next;
        if (argp->src.listen_arg && strcmp(argp->src.listen_arg, addr->ifa_name)) goto next;

        const in_addr_t in_addr = ntohl(((struct sockaddr_in *)addr->ifa_addr)->sin_addr.s_addr);
        if (in_addr == INADDR_LOOPBACK || in_addr == INADDR_NONE || in_addr == INADDR_ANY) goto next;

        param.addr_listen = *(struct sockaddr_in *)addr->ifa_addr;
        break;

        next:
        addr = addr->ifa_next;
    }

    if (!addr) {
        fputs("no suitable interfaces found", stderr);
        exit(-1);
    }

    fd_sock_in = udp_open(param.addr_listen.sin_family);
    if (fd_sock_in == -1) exit(-1);

    if (udp_bind(fd_sock_in, (struct sockaddr *)&param.addr_listen, sizeof(param.addr_listen))) exit(-1);

    socklen_t addr_len = sizeof(param.addr_listen);

    if (getsockname(fd_sock_in, (struct sockaddr *)&param.addr_listen, &addr_len)) {
        perror("getsockname");
        exit(-1);
    }

    fd_uart_out = serial_open(argp->src.output_arg);
    if (fd_uart_out == -1) exit(-1);
    if (serial_setup(fd_uart_out, cfg.speed, 0, -1)) exit(-1);

    print_src_info(argp, &param, "\n");

    param.fd_tx = fd_uart_out;
    param.fd_rx = fd_sock_in;

    src_start(&param);
    while (getchar() != '\n');
    src_stop();
    close(fd_uart_out);
    close(fd_sock_in);

    print_src_info(argp, &param, "  ");
    print_analyze_stats("rtt", &param.t_rtt);
}
