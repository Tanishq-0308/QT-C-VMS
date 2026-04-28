ffmpeg -f v4l2 -input_format yuyv422 -video_size 640x480 -framerate 30 -i /dev/video0 \
  -vf "format=nv12" \
  -c:v h264_qsv -b:v 5M \
  output_qsv_camera.mp4