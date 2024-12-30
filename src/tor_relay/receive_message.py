from flask import Flask, request

app = Flask(__name__)

@app.route('/message', methods=['POST'])
def receive_message():
    data = request.get_json()
    message = data.get('message', '')
    print(f"Received message: {message}")
    return {'status': 'success'}, 200

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=9001)