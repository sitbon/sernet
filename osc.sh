#!/bin/bash -e
d=$(dirname $(readlink -f $0))
id=$(hostname)
id="${id: -1}"
sudo ifconfig eth0:1 "10.10.10.20${id}/24"

#$d/build/sernet osc -h 10.10.10.161 -p 8001
#$d/build/sernet osc -h 10.10.10.13 -p 10085
