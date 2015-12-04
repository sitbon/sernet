#ifndef __UDP_H__
#define __UDP_H__

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

int udp_open(int ai_family);
int udp_bind(int fd, struct sockaddr *addr, socklen_t addr_len);
void udp_close(int fd);
int udp_addr(struct addrinfo **addr, const char *host, const char *service, bool passive);
int udp_send(int fd, struct sockaddr *addr, socklen_t addr_len, const void *buf, size_t len);
int udp_recv(int fd, struct sockaddr *addr, socklen_t *addr_len, void *buf, size_t len);
struct sockaddr_in *addr_inet_sockaddr(struct addrinfo *addr);
in_port_t addr_inet_port(struct addrinfo *addr);

#endif
