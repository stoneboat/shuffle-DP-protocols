#!/bin/bash

# Create the folders if they do not exist
[ -d logs ] || mkdir -p logs
[ -d hidden_client ] || mkdir -p hidden_client
chmod -R 700 hidden_client

# Start Tor for client with custom torrc
tor -f ./src/client/torrc & TOR_PID=$!

echo "Started Tor with PID $TOR_PID"

# Wait for Tor to initialize
sleep 10

# Start tor relay (Flask server)
python3 src/client/send_message.py & CLIENT_PID=$!

echo "Started test client with PID $CLIENT_PID"

# Function to handle script termination
cleanup() {
    echo "Terminating Tor and Client..."
    kill $TOR_PID
    kill $RELAY_PID
    exit 0
}

# Trap Ctrl+C (SIGINT) to run cleanup
trap cleanup SIGINT

# Keep the script running
wait