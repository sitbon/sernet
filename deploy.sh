#!/bin/bash -e
d=$(dirname $(readlink -f "$0"))
b=$(basename "$d")

echo 'Updating files...' >&2

parallel-rsync --outdir out --errdir out -h"$d/hosts" -x"--recursive --perms --times --copy-links --cvs-exclude --exclude=build/ --exclude=out/ $@" \
    "$d/" "/home/\$USER/$b/"

echo 'Compiling...' >&2

parallel-ssh --outdir out --errdir out -h"$d/hosts" "cd $b/ && mkdir -p build && cd build && cmake .. && make"
