import subprocess
import os
import datetime

class Recorder:
    def __init__(self):
        self.process = None
        self.output_file = None
        self.start_time = None

    def start(self, rtsp_url, output_path, gpu=True):
        if self.process is not None:
            return False, "Recording already in progress."

        self.output_file = output_path
        self.start_time = datetime.datetime.now()

        command = [
            "ffmpeg",
            "-y",
            "-rtsp_transport", "tcp",
            "-i", rtsp_url,
        ]

        if gpu:
            command += ["-c:v", "h264_nvenc", "-preset", "llhq", "-b:v", "5M"]
        else:
            command += ["-c:v", "libx264", "-preset", "fast", "-b:v", "2M"]

        command += ["-pix_fmt", "yuv420p", self.output_file]

        try:
            self.process = subprocess.Popen(
                command,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE
            )
            return True, f"Recording started: {self.output_file}"
        except Exception as e:
            return False, f"Failed to start recording: {str(e)}"

    def stop(self):
        if self.process:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
                stdout, stderr = self.process.communicate()
                self.process = None

                # Optional: log ffmpeg output for debugging
                if stderr:
                    with open("recording_error.log", "a") as log_file:
                        log_file.write(stderr.decode("utf-8") + "\n")

                return True, f"Recording stopped and saved to {self.output_file}"
            except subprocess.TimeoutExpired:
                self.process.kill()
                return False, "FFmpeg process killed after timeout."
        else:
            return False, "No recording in progress."

recorder = Recorder()
