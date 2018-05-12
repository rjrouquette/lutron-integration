#!/bin/bash

cd "$(dirname "${BASH_SOURCE[0]}")"

mkdir -p dist/
mkdir -p build/
rm dist/*
cd build

CORES=$(nproc)
echo "Running build with $CORES threads."

rm -rf *
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ ..
#make VERBOSE=1
make -j$CORES
mv lutron-integration ../dist

rm -rf *
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ ..
#make VERBOSE=1
make -j$CORES
mv lutron-integration ../dist/lutron-integration-dbg

cd ..
rm -rf build

if [ ! -f dist/lutron-integration ]; then
    echo "Failed to build lutron-integration"
    exit 1
fi

if [ ! -f dist/lutron-integration-dbg ]; then
    echo "Failed to build lutron-integration-dbg"
    exit 1
fi

echo "Successfully built: lutron-integration lutron-integration-dbg"
exit 0
