#!/bin/bash
set -e
OUT=cleaner
SRCS=(daemon.cpp)

echo "Building ${OUT} ..."
# Clean previous builds
rm -f ${OUT} *.o

g++ -std=c++17 -Wall -Werror -O2 "${SRCS[@]}" -o ${OUT}

echo "Build finished: ./${OUT}"