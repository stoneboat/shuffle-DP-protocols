# About

This branch tests the functionality of sending randomized encodings from a client to a blockchain miner via Tor. For this project, we focus solely on the Tor relay functionality of the miner, which we refer to as the "Tor relay."

---

## Installation

To set up the environment, run the following commands:

```bash
sudo apt update && sudo apt upgrade -y
pip install --upgrade -r requirements.txt
```

---

## Usage

### Client

To send a test message from the client, open a new shell and run:

```bash
python3 src/client/send_message.py
```

### Tor Relay

The Tor relay acts as an intermediary, receiving messages sent by the client and routing them locally. To start the relay:

1. Open a new shell.
2. Run the following shell script:

```bash
./src/tor_relay/start_relay_services.sh
```

This script performs two main actions:
- **Start a Tor process**: It uses the configuration file located at `./src/tor_relay/torrc`. This file sets up a hidden service on port 9001 and routes incoming messages to the local port 9001.
- **Start the Tor relay program**: The relay program listens on the configured local port (9001 in the example), receives messages, and processes them according to its functionality.

### Notes

- Ensure the Tor process uses unique listening and receiving ports as specified in the configuration file (`./src/tor_relay/torrc`) to avoid conflicts with other Tor instances running on the same machine.

---
