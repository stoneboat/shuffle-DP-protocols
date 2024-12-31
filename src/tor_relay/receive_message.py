from flask import Flask, request
import os

app = Flask(__name__)

# Directory to store received files
project_dir = os.path.abspath(os.getcwd())
storage_dir = os.path.join(project_dir, 'hidden_relay_service')
os.makedirs(storage_dir, exist_ok=True)

@app.route('/message', methods=['POST'])
def receive_message():
    data = request.get_json()
    message = data.get('message', '')
    print(f"Received message: {message}")
    return {'status': 'success'}, 200

@app.route('/upload', methods=['POST'])
def receive_file():
    if 'file' not in request.files:
        return {'status': 'error', 'message': 'No file part in the request'}, 400

    file = request.files['file']

    if file.filename == '':
        return {'status': 'error', 'message': 'No selected file'}, 400

    try:
        file_path = os.path.join(storage_dir, file.filename)
        file.save(file_path)
        print(f"File received and saved to: {file_path}")
        return {'status': 'success', 'file_path': file_path}, 200
    except Exception as e:
        print(f"An error occurred while saving the file: {e}")
        return {'status': 'error', 'message': str(e)}, 500

        

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=9001)