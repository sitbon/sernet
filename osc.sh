#!/bin/bash -e
id=$(hostname)
id="${id: -1}"
sudo ifconfig eth0:1 "10.10.10.20${id}/24"

./build/sernet osc -h 10.10.10.161 -p 8001
