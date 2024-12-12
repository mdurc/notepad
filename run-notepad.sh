#!/bin/bash
current_dir=$(pwd)

cd ~/personal/vimpad
make clean && make
cd "$current_dir"
if [[ $# -eq 1 ]]; then
    ~/personal/vimpad/notes.out "$1"
else
    ~/personal/vimpad/notes.out
fi
