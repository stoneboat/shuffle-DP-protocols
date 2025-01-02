#!/bin/bash

# Navigate to the child folder
cd src/anonymous-ether/ || exit

# Use find to locate directories named .ipynb_checkpoints and remove them
find . -type d -name ".ipynb_checkpoints" -exec rm -rf {} +

brownie compile

brownie run scripts/deploy.py --network ganache-local