#include "local.h"
#include "serial.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

speed_t btos(unsigned int baud)
{
    switch (baud) {
        case 0: return B0;
        case 50: return B50;
        case 75: return B75;
        case 110: return B110;
        case 134: return B134;
        case 150: return B150;
        case 200: return B200;
        case 300: return B300;
        case 600: return B600;
        case 1200: return B1200;
        case 1800: return B1800;
        case 2400: return B2400;
        case 4800: return B4800;
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 500000: return B500000;
        case 576000: return B576000;
        case 921600: return B921600;
        case 1000000: return B1000000;
        case 1152000: return B1152000;
        case 1500000: return B1500000;
        case 2000000: return B2000000;
        case 2500000: return B2500000;
        case 3000000: return B3000000;
        case 3500000: return B3500000;
        case 4000000: return B4000000;
        default: return 0;
    }
}

int serial_open(const char *path)
{
    const int fd = open(path, O_RDWR | O_NOCTTY);

    if (fd == -1) {
        fprintf(stderr, "open: %s\n", strerror(errno));
    }

    return fd;
}

void serial_close(int fd)
{
    close(fd);
}

/*
 * The values for speed are
 * B115200, B230400, B9600, B19200, B38400, B57600, B1200, B2400, B4800, etc
 *
 *  The values for parity are 0 (meaning no parity),
 * PARENB|PARODD (enable parity and use odd),
 * PARENB (enable parity and use even),
 * PARENB|PARODD|CMSPAR (mark parity),
 * and PARENB|CMSPAR (space parity).
 * */

/*
* if waitTime  < 0, it is blockmode
*  waitTime in unit of 100 millisec : 20 -> 2 seconds
*/

int serial_setup(int fd, speed_t speed, int parity, int wait_time)
{
    int blocking_mode;
    struct termios tty;

    blocking_mode = 0;
    if(wait_time < 0 || wait_time > 255)
        blocking_mode = 1;

    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(fd, &tty) != 0)
    {
        fprintf(stderr, "tcgetattr: %s\n", strerror(errno));
        return -1;
    }

    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN]  = (cc_t)(blocking_mode ? 1 : 0);
    tty.c_cc[VTIME] = (cc_t)(blocking_mode ? 0 : wait_time);

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag |= parity;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    cfmakeraw(&tty);
    tcflush(fd, TCIFLUSH | TCOFLUSH);

    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        fprintf(stderr, "tcsetattr: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}
