# receiver.py
from flask import Flask, request
import os
from datetime import datetime

app = Flask(__name__)

SAVE_DIR = "/home/pi/bird_images"
os.makedirs(SAVE_DIR, exist_ok=True)


@app.route("/receive", methods=["POST"])
def upload():
    if "file" not in request.files:
        return "No file", 400

    file = request.files["file"]
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = f"{SAVE_DIR}/bird_{timestamp}.jpg"

    file.save(filename)
    print(f"Saved: {filename}")

    # inference hook — we'll add this later
    # result = classify(filename)

    return "OK", 200


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)