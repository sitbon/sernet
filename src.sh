#!/bin/bash
d=$(dirname $(readlink -f $0))
sernet="$d/build/sernet"
params="src -d 100 -s 32 -l eth0 $@"
declare -a pids
declare -a fifos
fifoi=0
stdio_labels=0

cmds=(
    "-p 59524 -o /dev/ttyUSB0"
    "-p 59525 -o /dev/ttyUSB1"
#    "-o /dev/ttyUSB2"
#    "-o /dev/ttyUSB3"
#    "-o /dev/ttyUSB4"
#    "-o /dev/ttyUSB5"
)

function write_fifos_wait() {
    for ((i = 0; i < ${#fifos[@]}; i++)); do
        fifof="${fifos[i]}"
        cmd="${cmds[i]}"
        pid="${pids[i]}"
        if (( $stdio_labels )); then
            echo
            echo ">> $cmd"
        fi
        echo "$@" > "$fifof"
        if (( $stdio_labels )); then
            echo
        fi
        wait $pid 2>/dev/null
    done
}

function cleanup_fifos() {
    for fifof in "${fifos[@]}"; do
        rm -f "$fifof"
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
    if (( $stdio_labels )); then
        echo ": $cmd"
    fi
    fifof=".fifo${fifoi}"
    fifoi=$((fifoi+1))
    rm -f $fifof
    mkfifo $fifof
    fifos=( ${fifos[@]} "$fifof" )
    cat "$fifof" | $ex &
    pids=( ${pids[@]} $! )
    sleep 0.1
done

read -r var
write_fifos_wait "$var"
