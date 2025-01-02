from brownie import ARE_Storage, accounts

def main():
    # Use Brownie’s default account[0] (local Ganache in ephemeral mode)
    deployer = accounts[0]  
    storage_contract = ARE_Storage.deploy({"from": deployer})
    storage_contract.setValue(42, {"from": accounts[0]})
    return storage_contract