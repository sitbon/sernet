#ifndef __SERIAL_H__
#define __SERIAL_H__

#include <fcntl.h>
#include <termios.h>

speed_t btos(unsigned int baud);
int serial_open(const char *path);
void serial_close(int fd);
int serial_setup(int fd, speed_t speed, int parity, int wait_time);

#endif
