#include "local.h"
#include "udp.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

int udp_open(struct addrinfo *addr)
{
    const int fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);

    if (fd == -1) {
        perror("socket");
    }

    return fd;
}

int udp_bind(int fd, struct addrinfo *addr)
{
    if (bind(fd, addr->ai_addr, addr->ai_addrlen) == -1) {
        perror("bind");
        return -1;
    }

    return 0;
}

void udp_close(int fd)
{
    close(fd);
}

int udp_addr(struct addrinfo **addr, const char *host, const char *service, bool passive)
{
    struct addrinfo hints;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;//AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;// 0;
    hints.ai_flags = AI_ADDRCONFIG;

    if (passive) hints.ai_flags |= AI_PASSIVE;

    const int err = getaddrinfo(host, service, &hints, addr);

    if (err) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
    }

    return err;
}

int udp_send(int fd, struct addrinfo *addr, const void *buf, size_t len)
{
    const ssize_t res = sendto(fd, buf, len, MSG_NOSIGNAL, addr->ai_addr, addr->ai_addrlen);

    if (res == -1) {
        fprintf(stderr, "sendto: %s\n", strerror(errno));
    }

    return (int)res;
}

int udp_recv(int fd, struct sockaddr_in *addr, void *buf, size_t len)
{
    socklen_t addr_len = sizeof(struct sockaddr_in);
    const ssize_t res = recvfrom(fd, buf, len, MSG_NOSIGNAL | MSG_WAITALL, (struct sockaddr *)addr, &addr_len);

    if (res == -1) {
        fprintf(stderr, "recvfrom: %s\n", strerror(errno));
    }

    return (int)res;
}

in_port_t addr_inet_port(struct addrinfo *addr)
{
    switch (addr->ai_family) {
        case AF_INET:
            return ((struct sockaddr_in *)addr->ai_addr)->sin_port;
        case AF_INET6:
            return ((struct sockaddr_in6 *)addr->ai_addr)->sin6_port;
        default:
            return 0;
    }
}
