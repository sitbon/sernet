#!/bin/bash

arr=(a b c)

for ((i=0; i < ${#arr[@]}; i++)); do
    echo $i "${arr[i]}"
done
