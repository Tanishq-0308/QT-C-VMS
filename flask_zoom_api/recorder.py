import subprocess
import os
import datetime

LOG_FILE = "recording_error.log"

class Recorder:
    def __init__(self):
        self.process = None
        self.output_file = None
        self.start_time = None
        self.log_handle = None

    def start(self, rtsp_url, output_path, gpu=True):
        # An ffmpeg that already exited (e.g. stream lost) no longer blocks a new recording
        if self.process is not None and self.process.poll() is None:
            return False, "Recording already in progress."
        self._close_log()
        self.process = None

        self.output_file = output_path
        self.start_time = datetime.datetime.now()

        command = [
            "ffmpeg",
            "-y",
            "-nostats",
            "-loglevel", "warning",
            "-rtsp_transport", "tcp",
            "-i", rtsp_url,
        ]

        if gpu:
            command += ["-c:v", "h264_nvenc", "-preset", "llhq", "-b:v", "5M"]
        else:
            command += ["-c:v", "libx264", "-preset", "fast", "-b:v", "2M"]

        command += ["-pix_fmt", "yuv420p", self.output_file]

        try:
            # ffmpeg's output goes straight to a log file. Pipes that nobody reads fill up after a
            # few minutes and then block ffmpeg, which stalls the recording.
            self.log_handle = open(LOG_FILE, "a")
            self.log_handle.write(f"--- {self.start_time.isoformat()} recording {self.output_file}\n")
            self.log_handle.flush()
            self.process = subprocess.Popen(
                command,
                stdin=subprocess.PIPE,
                stdout=subprocess.DEVNULL,
                stderr=self.log_handle
            )
            return True, f"Recording started: {self.output_file}"
        except Exception as e:
            self._close_log()
            self.process = None
            return False, f"Failed to start recording: {str(e)}"

    def stop(self):
        if not self.process:
            return False, "No recording in progress."

        process = self.process
        self.process = None
        try:
            # 'q' lets ffmpeg finish the file properly; terminate/kill are fallbacks
            if process.poll() is None:
                try:
                    process.stdin.write(b"q")
                    process.stdin.flush()
                except (BrokenPipeError, OSError):
                    process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    return False, "FFmpeg process killed after timeout."
            return True, f"Recording stopped and saved to {self.output_file}"
        finally:
            self._close_log()

    def _close_log(self):
        if self.log_handle:
            self.log_handle.close()
            self.log_handle = None

recorder = Recorder()
