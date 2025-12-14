#!/bin/bash

# Build script for Linux DFS
# Usage: ./build.sh

echo "Building Distributed File System for Linux..."
echo ""

# Check if g++ is installed
if ! command -v g++ &> /dev/null; then
    echo "ERROR: g++ compiler not found. Please install build-essential:"
    echo "  sudo apt install build-essential"
    exit 1
fi

# Build coordinator
echo "Building coordinator..."
g++ -std=c++17 coordinator/coordinator.cpp -o coordinator
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to build coordinator"
    exit 1
fi

# Build node
echo "Building node..."
g++ -std=c++17 node/node.cpp -o node
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to build node"
    exit 1
fi

# Build client
echo "Building client..."
g++ -std=c++17 client/client.cpp -o client
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to build client"
    exit 1
fi

# Make executables executable
chmod +x coordinator node client

echo ""
echo "Build successful! Executables created:"
echo "  - coordinator"
echo "  - node"
echo "  - client"
echo ""
echo "To run the system:"
echo "  1. Start coordinator: ./coordinator"
echo "  2. Start nodes: ./node 1  (in separate terminals)"
echo "                   ./node 2"
echo "  3. Use client: ./client upload test.txt /docs/test.txt"
echo ""

