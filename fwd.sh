#!/bin/bash
d=$(dirname $(readlink -f $0))
sernet="$d/build/sernet"
params="fwd -h 10.1.0.139 -p 8000 $@"
declare -a pids
declare -a fifos
fifoi=0
stdio_labels=0

name=$(hostname)

#if [ "$name" == "nu5" ]; then
#    cmds=(
#        "-i /dev/ttyUSB0"
#        "-i /dev/ttyUSB1"
#        "-i /dev/ttyUSB2"
#        "-i /dev/ttyUSB3"
#        "-i /dev/ttyUSB4"
#        "-i /dev/ttyUSB5"
#    )
#else
    cmds=(
        "-i /dev/ttyUSB0"
        "-i /dev/ttyUSB1"
        "-i /dev/ttyUSB2"
        "-i /dev/ttyUSB3"
    )
#fi

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
