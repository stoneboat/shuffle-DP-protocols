# About

This branch focuses on testing the process of sending randomized encodings from a client to an Ethereum blockchain through Tor. The implementation leverages Brownie, a Python-based Ethereum development framework, and uses a local Ganache network to simulate both the client and the in-memory blockchain environment.

---

## Installation

The experiment is tested on a Google Virtual Machine instance with an Ubuntu 22.04.5 LTS system.

### Update and Upgrade Your System
First, ensure your system is up-to-date:

```bash
sudo apt update
sudo apt upgrade -y
```

### Dependencies Installation

#### Tor Installation
To install Tor on your system, use the following commands:
```bash
sudo apt install -y tor
```

To check the Tor installation:
```bash
tor --version
```
The tested Tor version is 0.4.6.10.

#### Node.js and npm Installation
Ganache requires Node.js. Install it using the following steps:
```bash
curl -fsSL https://deb.nodesource.com/setup_18.x | sudo -E bash -
sudo apt install -y nodejs
```

To check the installation:
```bash
node -v
npm -v
```
The tested Node.js version is v18.20.5, and npm is 10.8.2.

#### Ganache Installation
Install Ganache via npm:
```bash
sudo npm install -g ganache
```

To check the installation:
```bash
ganache --version
```
The tested Ganache version is v7.9.2.

#### Firewall Policies
Ensure that the ports used by Tor are allowed for external access. In this example:
- Tor relay uses ports 9051 and 9001 for listening and receiving.
- The client uses ports 9052 and 9002.
- Ganache RPC uses port 8545.

Configure firewall rules if you are using Google Cloud:

1. **Create a Firewall Rule**:
   - Name: `allow-shuffle-project`
   - Network: Select the appropriate VPC network (default is `default`).
   - Direction: `Ingress`
   - Action: `Allow`
   - Source: Specify IP ranges (e.g., `0.0.0.0/0` for all, or restrict for security).
   - Protocols and Ports: Specify `tcp:8540-8550,tcp:9000-9010,tcp:9050-9060`.

Ensure no other rules block these ports.

### Project Code Installation
To download the code and set up the environment, use the following commands:

#### Step 1: Update and Install Git
```bash
sudo apt update && sudo apt upgrade -y
sudo apt install git  # Install Git if not already installed

git clone https://github.com/stoneboat/shuffle-DP-protocols.git
```

#### Step 2: Set Up the Python Environment
1. **Install Python Virtual Environment Support**:
   ```bash
   sudo apt install python3.10-venv  # Ensure the correct version of Python
   sudo apt install python3-pip      # Install pip if not already installed
   ```

2. **Create and Activate a Virtual Environment**:
   ```bash
   python3 -m venv shuffleDP
   source shuffleDP/bin/activate
   ```

3. **Navigate to the Project Directory and Install Dependencies**:
   ```bash
   cd shuffle-DP-protocols
   pip install --upgrade -r requirements.txt
   ```

### Editor Configuration
To edit the code, we recommend using JupyterLab. Use the following commands to configure:

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
- **Start a Tor process**: Uses the configuration file located at `./src/tor_relay/torrc`. This file sets up a hidden service on port 9001 and routes incoming messages to the local port 9001.
- **Start the Tor relay program**: The relay program listens on the configured local port (9001 in the example), receives messages, and processes them.

### Client
To send a test message from the client, open a new shell and run:
```bash
./src/client/start_client.sh
```

### Notes
- The client and Tor relay set up separate Tor processes, each with different listening and receiving ports. Ensure all such ports are free and properly configured.
- Configuration files:
  - Tor client: `src/client/torrc`
  - Tor relay: `src/tor_relay/torrc`
- If you are using a deploy key for project updates, configure credentials as follows:

```bash
# Start the SSH agent
eval "$(ssh-agent -s)"

# Add the deploy key (replace `your_deploy_key` with your actual deploy key file name)
ssh-add ~/.ssh/your_deploy_key

# Set the correct Git remote URL (replace `USERNAME` and `REPOSITORY` with your GitHub account name and repository name)
git remote set-url origin git@github.com:USERNAME/REPOSITORY.git
```
Ensure you replace `your_deploy_key`, `USERNAME`, and `REPOSITORY` with your credentials and repository details.

---
```

