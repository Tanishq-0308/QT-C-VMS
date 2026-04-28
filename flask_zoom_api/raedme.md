cd flask_zoom_api
pip install -r requirements.txt
python3 app.py


post : http://localhost:8001/move-camera

{
    "direction": "zoom_in",
    "speed": 0.5
}
