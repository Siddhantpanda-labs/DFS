#!/bin/bash

# Start script for Linux DFS
# Starts coordinator and nodes in background

echo "Starting Distributed File System..."
echo ""

# Check if executables exist
if [ ! -f "./coordinator" ]; then
    echo "ERROR: coordinator not found. Please build first using 'make' or './build.sh'"
    exit 1
fi

if [ ! -f "./node" ]; then
    echo "ERROR: node not found. Please build first using 'make' or './build.sh'"
    exit 1
fi

if [ ! -f "./client" ]; then
    echo "ERROR: client not found. Please build first using 'make' or './build.sh'"
    exit 1
fi

# Start coordinator in background
echo "Starting coordinator in background..."
./coordinator > coordinator.log 2>&1 &
COORDINATOR_PID=$!
echo "Coordinator started (PID: $COORDINATOR_PID)"

# Wait for coordinator to start
sleep 2

# Start node 1 in background
echo "Starting node 1 in background..."
./node 1 > node1.log 2>&1 &
NODE1_PID=$!
echo "Node 1 started (PID: $NODE1_PID)"

sleep 1

# Start node 2 in background
echo "Starting node 2 in background..."
./node 2 > node2.log 2>&1 &
NODE2_PID=$!
echo "Node 2 started (PID: $NODE2_PID)"

sleep 1

echo ""
echo "========================================"
echo "DFS System Started!"
echo "========================================"
echo "Coordinator: Running on port 9000 (PID: $COORDINATOR_PID)"
echo "Node 1: Running on port 9001 (PID: $NODE1_PID)"
echo "Node 2: Running on port 9002 (PID: $NODE2_PID)"
echo ""
echo "You can now use the client:"
echo "  ./client upload test.txt /docs/test.txt"
echo "  ./client download /docs/test.txt output.txt"
echo "  ./client list"
echo ""
echo "Logs are being written to:"
echo "  - coordinator.log"
echo "  - node1.log"
echo "  - node2.log"
echo ""
echo "To stop the system, run:"
echo "  kill $COORDINATOR_PID $NODE1_PID $NODE2_PID"
echo "Or use: ./stop.sh"
echo ""

# Save PIDs to file for stop script
echo "$COORDINATOR_PID $NODE1_PID $NODE2_PID" > .dfs_pids

