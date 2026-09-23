from flask import Flask, request, jsonify
import asyncio
import threading
import time
import requests
import os
import datetime
import sqlite3
import subprocess
import uuid
import websockets

from onvif import ONVIFCamera
from recorder import recorder
from generate_report import generate_pdf_report

app = Flask(__name__)

# --- CONFIG ---
ESP_WS_URL = "ws://192.168.4.1:80"
ws = None

# Replace this with dynamic settings or DB later
def get_camera_settings():
    # return "192.168.1.88", 80, "admin", "123456", "8MP-HD"
    return "192.168.4.1", 80, "admin", "admin", "ESP32"

# --- WebSocket Client ---
async def connect_to_esp():
    global ws
    while True:
        try:
            print(f"Connecting to ESP32 WebSocket at {ESP_WS_URL}...")
            ws = await websockets.connect(ESP_WS_URL)
            print(f"✅ Connected to ESP32: {ws}")
            break
        except Exception as e:
            print(f"❌ Failed to connect: {e}")
            await asyncio.sleep(5)

async def send_command_to_esp(action):
    global ws
    try:
        if ws is None or not hasattr(ws, "closed") or ws.closed:
            print("🔁 Reconnecting to ESP32...")
            await connect_to_esp()

        await ws.send(action)
        print(f"✅ Sent to ESP: {action}")
        return {"status": "success", "action": action}
    except Exception as e:
        print(f"❌ Error sending command: {e}")
        return {"status": "error", "message": str(e)}

# --- Background Event Loop ---
loop = asyncio.new_event_loop()
def start_loop():
    asyncio.set_event_loop(loop)
    loop.run_until_complete(connect_to_esp())
    loop.run_forever()

t = threading.Thread(target=start_loop)
t.daemon = True
t.start()

# --- Database Helper ---
def get_rtsp_url_from_db(db_path="../sqlite.db"):
    try:
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()
        cursor.execute("SELECT rtsp_link FROM settings ORDER BY id LIMIT 1")
        row = cursor.fetchone()
        conn.close()
        return row[0] if row and row[0] else None
    except Exception as e:
        print("Database error:", str(e))
        return None

# --- ROUTES ---

@app.route("/test-camera-connection", methods=["GET"])
def test_camera_connection():
    try:
        camera_ip, port, username, password, model = get_camera_settings()
        return jsonify({
            "camera_ip": camera_ip,
            "port": port,
            "username": username,
            "password": password,
            "model": model
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route("/get-profiles", methods=["GET"])
def get_profiles():
    try:
        camera_ip, port, username, password, model = get_camera_settings()
        camera = ONVIFCamera(camera_ip, port, username, password)
        media_service = camera.create_media_service()
        profiles = media_service.GetProfiles()
        profile_list = [{'token': profile.token, 'name': profile.Name} for profile in profiles]
        return jsonify(profile_list), 200
    except Exception as e:
        return jsonify({"error": "Unable to connect to camera: " + str(e)}), 500

@app.route("/move-camera", methods=["POST"])
def move_camera():
    try:
        data = request.json
        direction = data.get("direction")
        speed = float(data.get("speed", 0.5))

        if direction not in ["zoom_in", "zoom_out"]:
            return jsonify({"error": "Invalid direction. Use 'zoom_in' or 'zoom_out'."}), 400

        camera_ip, port, username, password, model = get_camera_settings()

        if model == "8MP-HD":
            camera = ONVIFCamera(camera_ip, port, username, password)
            ptz = camera.create_ptz_service()
            profiles = camera.create_media_service().GetProfiles()
            token = profiles[0].token
            velocity = {"Zoom": {"x": speed if direction == "zoom_in" else -speed}}
            ptz.ContinuousMove({'ProfileToken': token, 'Velocity': velocity})
            time.sleep(0.5)
            ptz.Stop({'ProfileToken': token})

        elif model == "2MP":
            action = "zoomin" if direction == "zoom_in" else "zoomout"
            url = f"http://{camera_ip}/web/cgi-bin/hi3510/ptzctrl.cgi?-step=0&-act={action}&-speed={speed}"
            requests.get(url, auth=(username, password))
            time.sleep(0.5)
            stop_url = f"http://{camera_ip}/web/cgi-bin/hi3510/ptzctrl.cgi?-step=0&-act=stop&-speed={speed}"
            requests.get(stop_url, auth=(username, password))

        elif model == "ESP32":
            esp_command = "$Z_P#" if direction == "zoom_in" else "$Z_M#"
            future = asyncio.run_coroutine_threadsafe(send_command_to_esp(esp_command), loop)
            result = future.result()
            if result["status"] != "success":
                return jsonify(result), 500

        else:
            return jsonify({"error": f"Unsupported camera model: {model}"}), 400

        return jsonify({"message": f"Moved {direction} at speed {speed}."}), 200

    except Exception as e:
        return jsonify({"error": "Unable to connect to camera or perform movement: " + str(e)}), 500

@app.route("/start-recording", methods=["POST"])
def start_recording():
    try:
        data = request.json or {}
        rtsp_url = get_rtsp_url_from_db()
        if not rtsp_url:
            return jsonify({"error": "RTSP URL not found in database"}), 500

        output_file = data.get("output_file")
        if not output_file:
            timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            output_file = f"recordings/video_{timestamp}.mp4"

        os.makedirs(os.path.dirname(output_file), exist_ok=True)
        success, message = recorder.start(rtsp_url, output_file)
        return jsonify({"message": message, "output_file": output_file}), 200 if success else 400

    except Exception as e:
        return jsonify({"error": f"Failed to start recording: {str(e)}"}), 500

@app.route("/stop-recording", methods=["POST"])
def stop_recording():
    try:
        success, message = recorder.stop()
        return jsonify({"message": message}), 200 if success else 400
    except Exception as e:
        return jsonify({"error": f"Failed to stop recording: {str(e)}"}), 500

@app.route("/take-snapshot", methods=["POST"])
def take_snapshot():
    try:
        data = request.json or {}
        rtsp_url = get_rtsp_url_from_db()
        if not rtsp_url:
            return jsonify({"error": "RTSP URL not found in database"}), 500

        output_file = data.get("output_file")
        if not output_file:
            return jsonify({"error": "Missing 'output_file' in request body"}), 400

        os.makedirs(os.path.dirname(output_file), exist_ok=True)

        command = [
            "ffmpeg", "-y", "-rtsp_transport", "tcp", "-i", rtsp_url,
            "-frames:v", "1", "-q:v", "2", output_file
        ]

        proc = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if proc.returncode == 0:
            return jsonify({"message": f"Snapshot saved to {output_file}"}), 200
        else:
            return jsonify({"error": f"FFmpeg error: {proc.stderr.decode()}"}), 500

    except Exception as e:
        return jsonify({"error": f"Exception during snapshot: {str(e)}"}), 500

@app.route("/generate-pdf", methods=["POST"])
def generate_pdf():
    try:
        data = request.json
        patient_id = data.get("patient_id")
        surgery_id = data.get("surgery_id")

        if not patient_id or not surgery_id:
            return jsonify({"error": "Missing patient_id or surgery_id"}), 400

        filename = os.path.abspath("reports/report.pdf")
        os.makedirs(os.path.dirname(filename), exist_ok=True)

        success, result = generate_pdf_report("../sqlite.db", patient_id, surgery_id, filename)
        if not success:
            return jsonify({"error": result}), 500

        return jsonify({
            "message": "PDF generated successfully.",
            "pdf_path": result
        })

    except Exception as e:
        return jsonify({"error": f"Unexpected error: {str(e)}"}), 500

@app.route("/status", methods=["GET"])
def status():
    if ws is None:
        return jsonify({"websocket_connected": False, "reason": "ws is None"})
    elif not hasattr(ws, "closed"):
        return jsonify({"websocket_connected": False, "reason": "ws has no 'closed' attribute"})
    else:
        return jsonify({"websocket_connected": not ws.closed})

# --- Entry Point ---
if __name__ == "__main__":
    # Bind to localhost only: this API is used by the local desktop app. On 0.0.0.0 it exposed
    # patient report data and camera credentials to everyone on the network, unauthenticated.
    app.run(host="127.0.0.1", port=8001)
