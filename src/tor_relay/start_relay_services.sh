#!/bin/bash

# Create the folders if they do not exist
[ -d logs ] || mkdir -p logs
[ -d hidden_relay_service ] || mkdir -p hidden_relay_service
chmod -R 700 hidden_relay_service

# Start Tor with custom torrc
tor -f ./src/tor_relay/torrc & TOR_PID=$!

echo "Started Tor with PID $TOR_PID"

# Wait for Tor to initialize
sleep 10

# Start tor relay (Flask server)
python3 src/tor_relay/receive_message.py & RELAY_PID=$!

echo "Started Tor relay with PID $RELAY_PID"

# Function to handle script termination
cleanup() {
    echo "Terminating Tor and Program B..."
    kill $TOR_PID
    kill $RELAY_PID
    exit 0
}

# Trap Ctrl+C (SIGINT) to run cleanup
trap cleanup SIGINT

# Keep the script running
wait