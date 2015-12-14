#!/bin/bash -i
d=$(dirname $(readlink -f $0))

run_or_reset=0
only_if_running=0
no_attach=0
exists=0

pane1="$d/fwd.sh"
pane2="$d/osc.sh"
pane3="$d/build/sernet dst"

for arg; do
    if [[ "$arg" == "-r" ]]; then
        run_or_reset=1
    fi
    if [[ "$arg" == "-R" ]]; then
        only_if_running=1
    fi
    if [[ "$arg" == "-n" ]]; then
        no_attach=1
    fi
done

exists=$(tmux list-sessions 2>/dev/null | grep sernet)

if [ ! -z "$exists" ]; then
    exists=1
else
    exists=0
fi

echo "r=$run_or_reset R=$only_if_running n=$no_attach e=$exists"

if (( $exists < $only_if_running )); then
    exit
fi

tmux start-server
tmux new-session -d -s sernet 2>/dev/null
sleep 0.010

tmux selectp -t 1
sleep 0.005

if (( $run_or_reset )); then
    if (( $exists )); then
        tmux send-keys C-m
        sleep 0.5
        tmux send-keys C-m C-c C-l C-m
    fi

    #tmux send-keys C-l "echo fwd $exists" C-m C-l
    tmux send-keys C-l "$pane1" C-m
fi

if ! (( $exists )); then
    tmux splitw -v -p 50
else
    tmux selectp -t 3
    sleep 0.005

    if (( $run_or_reset )); then
        tmux send-keys C-m
        sleep 0.5
        tmux send-keys C-m C-c C-l C-m
    fi
fi

sleep 0.005

if (( $run_or_reset )); then
    #tmux send-keys C-l "echo dmp $exists" C-m C-l
    tmux send-keys C-l "$pane2" C-m
fi

tmux selectp -t 1
sleep 0.005

if ! (( $exists )); then
    tmux splitw -h -p 50
else
    tmux selectp -t 2
    sleep 0.005

    if (( $run_or_reset )); then
        tmux send-keys C-m
        sleep 0.5
        tmux send-keys C-m C-c C-l C-m
    fi
fi

if (( $run_or_reset )); then
    #tmux send-keys C-l "echo dst $exists" C-m C-l
    tmux send-keys C-l "$pane3" C-m
fi

if ! (( $no_attach )); then
    echo 1
    tmux attach-session -t sernet
fi
