#!/bin/bash -e
d=$(dirname $(readlink -f $0))
id=$(hostname)
id=$(( ${id: 2} + 160 ))
sudo ifconfig eth0:1 "10.10.10.${id}/24"

$d/mtou/mtou -I eth0:1 -i 239.255.0.170 -p 10170 -o 127.0.0.1 -P 8005 --reverse &
mtou_pid=$!

function kill_mtou() {
    kill $mtou_pid
}

trap kill_mtou SIGINT EXIT

$d/build/sernet osc -h 127.0.0.1 -p 8005

#$d/build/sernet osc -h 10.10.10.200 -p 8001
#$d/build/sernet osc -h 10.10.10.100 -p 8005
#$d/build/sernet osc -h 10.10.10.16 -p 8005

