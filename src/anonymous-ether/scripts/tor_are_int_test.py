import numpy as np
import os

from flask import Flask, request
from brownie import ARE_INT_Storage, accounts

app = Flask(__name__)

# Use Brownie’s default account[0] (local Ganache in ephemeral mode)
deployer = accounts[0]  
# Make sure that the size array does not exceed the number of pre-fund account in the test network
size = 5 
# Deploy the ARE protocol
storage_contract = ARE_INT_Storage.deploy(size, {"from": deployer})

# Directory to store received files
project_dir = os.path.abspath(os.path.join(os.getcwd(), '../..'))
storage_dir = os.path.join(project_dir, 'hidden_relay_service')
os.makedirs(storage_dir, exist_ok=True)

@app.route('/message', methods=['POST'])
def receive_message():
    data = request.get_json()
    message = data.get('message', '')

    if storage_contract.getCurrentCount() == size:
        print(f"Miner received message: {message} but the ARE_INT computation has ended")

    if storage_contract.getCurrentCount() < size:
        storage_contract.setValue(message['value'], {"from": accounts[message['accountId']]})
        print(f"Miner received message: {message}, and mined the transaction")

    if storage_contract.getCurrentCount() == size:
        test_sum = np.sum(np.array(storage_contract.getStorage()))
        print(f"ARE_INT computation has ended, and the sum of encoding is {test_sum}")       
    return {'status': 'success'}, 200

    
def main():
    app.run(host='0.0.0.0', port=9001)