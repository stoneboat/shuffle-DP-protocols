#!/bin/bash

# Create the folders if they do not exist
[ -d logs ] || mkdir -p logs
[ -d hidden_relay_service ] || mkdir -p hidden_relay_service
chmod -R 700 hidden_relay_service

# Start Tor with custom torrc
tor -f ./src/tor_relay/torrc &
TOR_PID=$!

echo "Started Tor with PID $TOR_PID"

# Wait for Tor to initialize
sleep 10