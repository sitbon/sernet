#ifndef __UDP_H__
#define __UDP_H__

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

int udp_open(struct addrinfo *addr);
int udp_bind(int fd, struct addrinfo *addr);
void udp_close(int fd);
int udp_addr(struct addrinfo **addr, const char *host, const char *service, bool passive);
int udp_send(int fd, struct addrinfo *addr, const void *buf, size_t len);
int udp_recv(int fd, struct sockaddr_in *addr, void *buf, size_t len);
in_port_t addr_inet_port(struct addrinfo *addr);

#endif
