#!/usr/bin/env python
"""Relay keyboard commands over a serial connection.

Guide for keyboard -> spider commands (keys -> values in the map arrays)
        and their associated buttons on the phone app:

        1       STOP MOVEMENT               WALK

        2       DANCE                       DANS

        3       FIST PUMP                   TILT

        O       LIGHTS (O)FF                OFF

        R       LIGHTS (R)ED                R

        G       LIGHTS (G)REEN              G

        B       LIGHTS (B)LUE               B

        M       LIGHTS (M)ULTI              MULTI

        C       LIGHTS (C)HRISTMAS          XMAS

        +       STAND UP (+ ON)             +

        -       SIT DOWN (- OFF)            -

        [       COLOR ADVANCE "LEFT"

        ]       COLOR ADVANCE "RIGHT"

        K       ENABLE (K)ILL MODE

        L       DISABLE (L)OCKOUT
"""
import sys, os, threading

CMD_MAP_0 = {
    '1': '+',
    '2': 'C',
    '3': 'M',
    '4': 'B',
    '5': 'G',
    '6': 'B',
    '7': '2',
    '8': '1',
    '9': '3',
    '0': '-',
    'K': 'K', 'k': 'K',
    'L': 'L', 'l': 'L',
}

CMD_MAP_KEEP = {
    '!': '+',
    '@': 'C',
    '#': 'M',
    '$': 'B',
    '%': 'G',
    '^': 'B',
    '&': '2',
    '*': '1',
    '(': '3',
    ')': '-',
    '1': '1',
    '2': '2',
    '3': '3',
    'C': 'C', 'c': 'C',
    'M': 'M', 'm': 'M',
    'G': 'G', 'g': 'G',
    'B': 'B', 'b': 'B',
    'R': 'R', 'r': 'R',
    'O': 'O', 'o': 'O',
    'K': 'K', 'k': 'K',
    'L': 'L', 'l': 'L',
    '+': '+',
    '-': '-',
    '[': None,
    ']': None,
}

COLOR_CURRENT = "O"
COLOR_ORDER = "ORGBMC"

def inchr_map(inchr):
    global COLOR_CURRENT

    if inchr in "[]":
        COLOR_CURRENT = COLOR_ORDER[(COLOR_ORDER.index(COLOR_CURRENT) - 1 + 2*"[]".index(inchr)) % len(COLOR_ORDER)]
        inchr = COLOR_CURRENT

    return CMD_MAP_KEEP.get(inchr, '')


def main(args):
    tty = args[0]
    baud = 9600
    key = True

    try:
        if len(args) > 1:
            baud = int(args[1])

        serial = get_serial(tty, baud)
        sread = serial.read
        swrite = serial.write
        sflush = serial.flush

        write = sys.stdout.write
        flush = sys.stdout.flush

        chars = []

        def swritek(inchr):
            if inchr is not None:
                value = ord(inchr)

                if inchr == '.':
                    raise SystemExit

                if value == 0xA or value == 0xD:
                    swrite('\n')
                    sflush()
                    write('\n')
                    flush()
                elif 0x20 <= value < 0x7F:
                    inchr = inchr_map(inchr)
                    if inchr:
                        swrite(inchr + '\n')
                        sflush()
                        write(inchr)
                        flush()
                else:
                    write(inchr)
                    flush()

        def swrites(instr):
            if instr:
                for instrl in instr.splitlines():
                    swrite(instrl + '\n')

        relay_stdin_start(swritek if key else swrites, key)

        while True:
            c = sread()

            if c == '\n' or c == '\r':
                if len(chars):
                    write(''.join(chars))
                    chars[:] = []
                    flush()
            else:
                chars.append(c)

    except Exception, e:
        raise
    except KeyboardInterrupt:
        print


def relay_stdin_thread(swrite, key):
    read = reader(key)
    try:
        while True:
            swrite(read())
    except Exception:
        raise
    except KeyboardInterrupt:
        print


def relay_stdin_start(swrite, key):
    relay_thread = threading.Thread(target=relay_stdin_thread, args=(swrite, key))
    relay_thread.setDaemon(True)
    relay_thread.start()
    return relay_thread


def reader(key):
    if not key:
        return sys.stdin.readline
    else:
        try:
            import msvcrt
            return msvcrt.getch
        except ImportError:
            import tty, termios, atexit

            #def getch():
            #    fd = sys.stdin.fileno()
            #    attr_old = termios.tcgetattr(fd)
            #    try:
            #        #tty.setraw(fd, termios.TCSADRAIN)
            #        tty.setraw(fd)
            #        return sys.stdin.read(1)
            #    finally:
            #        termios.tcsetattr(fd, termios.TCSADRAIN, attr_old)
            #return getch

            fd = sys.stdin.fileno()
            attr_old = termios.tcgetattr(fd)
            attr_new = attr_old[:]
            attr_new[3] &= ~(termios.ICANON | termios.ECHO | termios.ECHOCTL)
            termios.tcsetattr(fd, termios.TCSANOW, attr_new)
            atexit.register(termios.tcsetattr, fd, termios.TCSANOW, attr_old)
            return lambda: sys.stdin.read(1)


def get_serial(tty, baud):
    import serial
    ser = serial.Serial()
    ser.port = tty 
    ser.baudrate = baud
    ser.open()
    return ser 


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print "usage:", os.path.basename(sys.argv[0]), "<tty>", "[<baud>]"
        raise SystemExit
    
    main(sys.argv[1:])
