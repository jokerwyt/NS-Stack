#!/usr/bin/env bash
rm -rf build/* && cmake -DCMAKE_BUILD_TYPE=Debug -B build && make -Cbuild -j


cd checkpoints
make clean
make