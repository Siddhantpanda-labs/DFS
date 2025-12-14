#!/bin/bash

# Stop script for Linux DFS
# Kills all DFS processes

echo "Stopping Distributed File System..."

# Try to read PIDs from file
if [ -f ".dfs_pids" ]; then
    PIDS=$(cat .dfs_pids)
    echo "Killing processes: $PIDS"
    kill $PIDS 2>/dev/null
    rm -f .dfs_pids
fi

# Also kill any remaining processes by name
pkill -f "./coordinator" 2>/dev/null
pkill -f "./node" 2>/dev/null

echo "DFS system stopped."
echo "You can check for remaining processes with: ps aux | grep -E 'coordinator|node'"

