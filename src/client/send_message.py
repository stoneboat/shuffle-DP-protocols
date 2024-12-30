import requests

# Configure Tor SOCKS proxy
proxies = {
    'http': 'socks5h://127.0.0.1:9050',
    'https': 'socks5h://127.0.0.1:9050',
}


# Replace this with Program B's onion address
TOR_ONION_ADDRESS = "zqusliokkkskzxbwo7uvhsbznf36nsfmk7cbfiqumljxego3yb5lq4ad.onion"

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
