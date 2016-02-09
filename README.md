Sernet
=============

The purpose of this utility is to facilitate communication between
serial ports and networks, primarily for XBee packets.

Requirements
=============

This project uses CMake to build and bash/tmux to run the different
components. Tmux is optional but helpful because typical operation
requires up to three processes to run at the same time and it puts
their output all in the same terminal (like screen, but with panes).

This has been lightly tested on Mac, heavily used on Linux.

Building
=============

In the project top-level folder:

    mkdir build
    cd build
    cmake ..
    make
    cd ..

At this point you can run ./build/sernet or any of the utility scripts
(fwd.sh, osc.sh, tmux.sh).

How it works
=============

There are three primary important components to sernet, which allow for
flexibility when dealing with multiple sources but can also complicate
more simple use cases. Hopefully this explanation will make it usable
in any appropriate scenario.

The three main components are as follows:

fwd: serial forwarding agent
-------------
When run directly, this listens for XBee packets on a specified serial port
and sends them as UDP messages to a specified IP address. Example:

    ./build/sernet fwd -i /dev/ttyUSB0 -r 115200 -h 127.0.0.1 -p 4434 

The port option defaults to 4434 and the rate defaults to 230400 (the max speed
of the XBee radios). It is recommended that you customize `fwd.sh` with your
desired parameters as it also handles starting multiple instances in the case
where you are reading from more than one serial port.

You can get usage for the command (and any other) with the `--help` option,
e.g. `./build/sernet fwd --help`.

dst: packet sink and processing
-------------
This may not be helpful in many use cases, but it's been left as-is to enable
distributed processing. By default, it takes in packets on port 4434 (on any
interface) and (depending on a compile-time constant, see `dst.c`) deduplicates
the messages before forwarding them over a UNIX socket at `/tmp/sernet`.

osc: conversion from UDP packets to OSC-UDP messages
-------------
This reads from the UNIX socket and forwards out to a specified IP address
and port. See (and customize) `osc.sh` or the program usage for parameter
details. If multicast output is desired, one must currently use another tool
called `mtou` in order to translate unicast to multicast (contact Phillip
for more info).

Running with tmux
=============

After installing tmux, copy `tmux.conf` to `~/.tmux.conf` -- this provides
a configuration that is compatible with the script that organizes the panes.

`tmux.sh` runs `fwd`, `dst`, and `osc` all at once and is capable of (re)starting
all of those processes using the `-r` parameter.

When you invoke `tmux.sh -r`, you should see three panes with sernet running.
If you want to restart one of them, navigate to the appropriate pane
(ctrl+a, left/right/up/down) and simply press enter to exit the program, and
then use the up arrow to recall the same command. If you want to exit the session,
one of many methods is to stop each script and ctrl+d until all the panes are
gone. You can also call `tmux kill-session -t sernet` in order to terminate
everything at once.
