#!/usr/bin/env python
import sys, os

def main(args):
    tty = args[0]
    baud = 9600
    try:
        if len(args) > 1:
            baud = int(args[1])

        serial = get_serial(tty, baud)
        read = serial.read
        write = sys.stdout.write
        flush = sys.stdout.flush
        
        while True:
            write(read())
            flush()
    except Exception, e:
        print e
    except KeyboardInterrupt:
        print

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
