#!/bin/bash -e
d=$(dirname $(readlink -f $0))
id=$(hostname)

$d/build/sernet osc -h 127.0.0.1 -p 8005
