import requests
import os

# Configure Tor SOCKS proxy
proxies = {
    'http': 'socks5h://127.0.0.1:9051',
    'https': 'socks5h://127.0.0.1:9051',
}

project_dir = os.path.abspath(os.getcwd())
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

def send_message(message):
    url = f"http://{TOR_ONION_ADDRESS}:9001/message"
    payload = {'message': message}
    try:
        response = requests.post(url, json=payload, proxies=proxies)
        if response.status_code == 200:
            print("Message sent successfully!")
        else:
            print(f"Failed to send message. Status Code: {response.status_code}")
    except Exception as e:
        print(f"An error occurred: {e}")

if __name__ == "__main__":
    send_message("hello world")
