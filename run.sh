#!/bin/bash
d=$(dirname $(readlink -f $0))
sernet="$d/build/sernet"
params="analyze -r 230400 -l 0.0.0.0 -h 127.0.0.1 $@"
declare -a pids
declare -a fifos
fifoi=0

cmds=(
    "-i /dev/ttyUSB0 -o /dev/ttyUSB1 -p 44010"
    "-i /dev/ttyUSB2 -o /dev/ttyUSB3 -p 44011"
#    "-i /dev/ttyUSB4 -o /dev/ttyUSB5 -p 44012"
#    "-i /dev/ttyUSB6 -o /dev/ttyUSB7 -p 44013"
)

function write_fifos_wait() {
    for ((i = 0; i < ${#fifos[@]}; i++)); do
        fifof="${fifos[i]}"
        cmd="${cmds[i]}"
        pid="${pids[i]}"
        echo
        echo ">> $cmd"
        echo "$@" > "$fifof"
        echo
        wait $pid 2>/dev/null
    done
}

function cleanup_fifos() {
    for fifof in "${fifos[@]}"; do
        rm "$fifof"
    done
}

function wait_ch() {
    for pid in ${pids[@]}; do
        wait $pid 2>/dev/null
    done
    cleanup_fifos
}

function kill_ch() {
    for pid in ${pids[@]}; do
        kill $pid 2>/dev/null
    done
    cleanup_fifos
}

trap kill_ch SIGINT
trap wait_ch EXIT

echo "$params"

for cmd in "${cmds[@]}"; do
    ex="$sernet $params $cmd"
    echo ": $cmd"
    fifof=".fifo${fifoi}"
    fifoi=$((fifoi+1))
    mkfifo $fifof
    fifos=( ${fifos[@]} "$fifof" )
    cat "$fifof" | $ex &
    pids=( ${pids[@]} $! )
done

read -r var
write_fifos_wait "$var"
