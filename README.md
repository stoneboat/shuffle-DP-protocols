# About

This branch focuses on testing the process of sending randomized encodings from a client to an Ethereum blockchain through Tor. The implementation leverages Brownie, a Python-based Ethereum development framework, IPFS (InterPlanetary File System) for distributed file storage, and a local Ganache network to simulate both the client and the in-memory blockchain environment.

In this project, the client uploads randomized encodings—which may be stored as long strings in a file—to the IPFS network. It then sends a smart contract command along with the corresponding Content Identifier (CID) of the file to an Ethereum node via Tor. The Ethereum node subsequently executes the smart contract function, storing the CID of the randomized encodings on the Ethereum blockchain.

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

#### IPFS (InterPlanetary File System) Installation

To install the IPFS daemon:
```bash
wget https://dist.ipfs.tech/go-ipfs/v0.14.0/go-ipfs_v0.14.0_linux-amd64.tar.gz
tar xvfz go-ipfs_v0.14.0_linux-amd64.tar.gz
cd go-ipfs
sudo bash install.sh
```

If this is the first time using IPFS, initialize it with:
```bash
ipfs init
```


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
   sudo apt install python3-venv  # Ensure the correct version of Python
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

### Set Up IPFS Network
Start the IPFS daemon with:
```bash
ipfs daemon
```

### Set Up Blockchain Network

To set up a blockchain test network, follow these steps:

1. Open a new shell and start the Ganache test network:

   ```bash
   ganache
   ```

2. Add the test network to Brownie:

   ```bash
   cd src/anonymous-ether
   brownie networks add Ethereum ganache-local host=http://127.0.0.1:8545 chainid=1337
   ```

   Replace the `host` URL and `chainid` with the details of the test network you are using. In the provided batch scripts, the test network is referred to as `ganache-local`. If you use a different network name, update the scripts accordingly.

### Anonymous Mining

To simulate the mining process, open a new shell and run the following shell script:

1. Start the Ethereum mining service:

   ```bash
   ./src/anonymous-ether/start_eth_service.sh
   ```

This script performs two main actions:

- **Start a Tor process**: Uses the configuration file located at `./src/anonymous-ether/torrc`. This file sets up a hidden service on port 9001 and routes incoming messages to the local port 9001.
- **Run a network node**: The network node (miner) listens on the configured local port (9001 in the example) via Tor, receives transactions, and processes them.

### Client

To send a test message from the client, open a new shell and run:

```bash
./src/client/start_client.sh
```

### Notes

- The client and Miner set up separate Tor processes, each with different listening and receiving ports. Ensure all such ports are free and properly configured.
- Configuration files:
  - Tor client: `src/client/torrc`
  - Tor ETH miner: `src/anonymous-ether/torrc`
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

