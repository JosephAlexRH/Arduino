from flask import Flask, request, jsonify
import os
from datetime import datetime

app = Flask(__name__)

SAVE_FOLDER= r"C:\Users\Joe\Pictures\ESP32Cam"
os.makedirs(SAVE_FOLDER, exist_ok=True)

@app.route("/receive", methods=["POST"])
def receive():
    timestamp=datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    filename=timestamp+".jpg"
    filepath = os.path.join(SAVE_FOLDER, filename)

    with open(filepath, "wb") as f:
        f.write(request.data)

    print(filepath)
    return jsonify({"filename":filename})

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)