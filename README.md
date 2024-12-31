# About

This branch tests the functionality of sending randomized encodings from a client to a blockchain miner via Tor. For this project, we focus solely on the Tor relay functionality of the miner, which we refer to as the "Tor relay."

---

## Installation

### Tor Installation

To install Tor on your system, use the following commands:

#### Ubuntu/Debian-based Systems:
```bash
sudo apt update
sudo apt install -y tor
```

#### Verify Tor Installation:
```bash
tor --version
```

Ensure that the Tor service is running correctly:
```bash
sudo systemctl start tor
sudo systemctl enable tor
sudo systemctl status tor
```


### Firewall Policies

Ensure that the ports used by Tor are allowed for external access. In this example, the Tor relay uses ports 9051 and 9001 for listening and receiving, respectively. Similarly, the client uses ports 9052 and 9002. Configure firewall rules accordingly if you are using Google Cloud Virtual Machine Service:

#### Open Ports in Google Cloud:
```bash
gcloud compute firewall-rules create allow-tor-ports \
  --direction=INGRESS \
  --priority=1000 \
  --network=default \
  --action=ALLOW \
  --rules=tcp:9001,tcp:9051,tcp:9002,tcp:9052 \
  --source-ranges=0.0.0.0/0
```

Ensure the specified ports are not blocked by any other firewall rules or policies.

### Software Installation

To set up the environment, run the following commands:

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install python3.10-venv
git clone https://github.com/stoneboat/shuffle-DP-protocols.git
python3 -m venv shuffleDP
source shuffleDP/bin/activate
sudo apt install python3-pip
cd shuffle-DP-protocols
pip3 install --upgrade -r requirements.txt
```


### Editor Configuration
To edit the code, we recommend using JupyterLab. To configure it, use the following commands:

#### Install JupyterLab:
```bash
pip install jupyterlab
```

#### Start JupyterLab:
```bash
jupyter lab
```

#### Display JupyterLab URLs:
```bash
jupyter lab list
```

This will show the URL for accessing the JupyterLab web service.

---

## Usage

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

### Client

To send a test message from the client, open a new shell and run:

```bash
./src/client/start_client.sh
```

### Notes

- The client and Tor relay will set up separate Tor processes, each with different listening and receiving ports. Ensure that all such ports are free and clear for use. If you are using a cloud service like a virtual machine, make sure the firewall policies are configured to allow external access to these ports.
- The Tor configuration file for the client is located at `src/client/torrc`, while the configuration file for the Tor relay is at `src/tor_relay/torrc`.
- If you are using a deploy key to update the remote project, follow these steps to configure the proper credentials:

```bash
# Start the SSH agent
eval "$(ssh-agent -s)"

# Add the deploy key (replace `your_deploy_key` with your actual deploy key file name)
ssh-add ~/.ssh/your_deploy_key

# Set the correct Git remote URL (replace `USERNAME` and `REPOSITORY` with your GitHub account name and repository name, respectively)
git remote set-url origin git@github.com:USERNAME/REPOSITORY.git
```

Ensure that you replace `your_deploy_key`, `USERNAME`, and `REPOSITORY` with your specific credentials and repository details.

---
