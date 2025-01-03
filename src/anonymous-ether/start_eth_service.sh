#!/bin/bash

# Create the folders if they do not exist
[ -d logs ] || mkdir -p logs
[ -d hidden_relay_service ] || mkdir -p hidden_relay_service
chmod -R 700 hidden_relay_service

# Start Tor for tor relay with custom torrc
tor -f ./src/anonymous-ether/torrc & TOR_PID=$!

echo "Started Tor with PID $TOR_PID"

# Wait for Tor to initialize
sleep 6

# Navigate to the child folder and run the dApp
cd src/anonymous-ether/ || exit

# Use find to locate directories named .ipynb_checkpoints and remove them
find . -type d -name ".ipynb_checkpoints" -exec rm -rf {} +
# Clear compiled file
[ -d build ] || mkdir -p build
rm -r build/ & mkdir build

brownie compile

brownie run scripts/tor_are_int_test.py --network ganache-local 

# echo "Started ARE simulation with PID $ARE_PID"

# Function to handle script termination
cleanup() {
    echo "Terminating Tor and ARE simulation..."
    kill $TOR_PID
    # kill $ARE_PID
    exit 0
}

# Trap Ctrl+C (SIGINT) to run cleanup
trap cleanup SIGINT

# Keep the script running
wait