import requests
import os
import asyncio

from utils.ipfs import *

# Configure Tor SOCKS proxy
proxies = {
    'http': 'socks5h://127.0.0.1:9051',
    'https': 'socks5h://127.0.0.1:9051',
}

project_dir = os.path.abspath(os.path.join(os.getcwd(), '..'))
src_dir = os.path.join(project_dir, 'src')
tor_relay_address_path = os.path.join(project_dir, 'hidden_relay_service/hostname')
# Replace this with Toy relay's onion address, which is at hidden_relay_service hostname
try:
    # Open the file and read the Onion address
    print(tor_relay_address_path)
    with open(tor_relay_address_path, 'r') as file:
        TOR_ONION_ADDRESS = file.read().strip()  # Remove any leading/trailing whitespace
        print(f"The Tor relay onion address has found: {TOR_ONION_ADDRESS}")
except FileNotFoundError:
    print(f"Error: Tor relay onion address does not exist in the default path.")
except Exception as e:
    print(f"An error occurred: {e}")
    TOR_ONION_ADDRESS = None


def send_file(_id, file_path):
    if not TOR_ONION_ADDRESS:
        print("Tor relay onion address is not set.")
        return
        
    url = f"http://{TOR_ONION_ADDRESS}:9001/message"
    try:
        with open(file_path, 'rb') as file:
            CID = asyncio.run(add_to_ipfs(file_path))
            payload = {'message': {'accountId': _id, 'value': CID}}
            response = requests.post(url, json=payload, proxies=proxies)
        
        if response.status_code == 200:
            print("File sent successfully!")
        else:
            print(f"Failed to send file. Status Code: {response.status_code}")
    except FileNotFoundError:
        print("Error: File does not exist.")
    except Exception as e:
        print(f"An error occurred: {e}")


def generate_test_file(file_path, size_kb, self_defined_text = "This is a line of readable text.\n"):
    ''' Generate a file with size at lease size_kb KB'''
    with open(file_path, 'w') as file:
        file.write(self_defined_text)
        text = "This is a line of readable text.\n"
        while file.tell() < size_kb * 1024:
            file.write(text)

        
if __name__ == "__main__":    
    if not TOR_ONION_ADDRESS:
        print("Tor relay onion address is not set.")
    else:
        # Test for sending a large file
        size = 5
        for i in range(size):   
            file_name = "example_file" + str(i) + ".txt"
            relative_path = "hidden_client/" + file_name
            test_file_path = os.path.join(project_dir, relative_path)
            generate_test_file(test_file_path, 1, self_defined_text = str(i)+"\n")  # Generate a test file with approximately 1 KB size
            send_file(i, test_file_path)








    
