import numpy as np
import os
import sys

from flask import Flask, request
from brownie import ARE_FILE_Storage, accounts

app = Flask(__name__)

# Use Brownie’s default account[0] (local Ganache in ephemeral mode)
deployer = accounts[0]  
# Make sure that the size array does not exceed the number of pre-fund account in the test network
size = 5 
# Deploy the ARE protocol
storage_contract = ARE_FILE_Storage.deploy(size, {"from": deployer})

# Directory to store received files
project_dir = os.path.abspath(os.path.join(os.getcwd(), '../..'))
storage_dir = os.path.join(project_dir, 'hidden_relay_service')
src_dir = os.path.join(project_dir, 'src')
os.makedirs(storage_dir, exist_ok=True)

sys.path.append(src_dir)
from utils.ipfs import *

@app.route('/message', methods=['POST'])
def receive_message():
    data = request.get_json()
    message = data.get('message', '')

    if storage_contract.getCurrentCount() == size:
        print(f"Miner received message: {message} but the ARE_INT computation has ended")

    if storage_contract.getCurrentCount() < size:
        storage_contract.setCID(message['value'], {"from": accounts[message['accountId']]})
        print(f"Miner received message: {message}, and mined the transaction")

    if storage_contract.getCurrentCount() == size:
        tmp_file_path = os.path.join(storage_dir, "tmp.txt")
        
        cids = list(storage_contract.getStorage())
        total = 0
        for cid in cids:
            tmp_file_path = asyncio.run(get_from_ipfs(cid, tmp_file_path))
            with open(tmp_file_path, 'r') as file:
                first_line = file.readline().strip()  # Read and strip any whitespace/newline
                total += int(first_line)  # Convert the string to an integer

        print(f"ARE_INT computation has ended, and the sum of encoding is {total}")       
    return {'status': 'success'}, 200

    
def main():
    app.run(host='0.0.0.0', port=9001)

    return storage_contract