from brownie import ARE_INT_Storage, accounts
import numpy as np

def main():
    # Use Brownie’s default account[0] (local Ganache in ephemeral mode)
    deployer = accounts[0]  
    # Make sure that the size array does not exceed the number of pre-fund account in the test network
    size = 5 

    # Deploy the ARE protocol
    storage_contract = ARE_INT_Storage.deploy(5, {"from": deployer})

    # Execute the ARE protocol
    for i in range(size): 
        storage_contract.setValue(i, {"from": accounts[i]})

    # Test the result
    assert storage_contract.getCurrentCount() == size
    assert np.sum(np.array(storage_contract.getStorage())) == 10
    print("sucessfully pass the test")
    
    return storage_contract