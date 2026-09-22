# Tools

## decklink_probe
Read-only check of what the DeckLink card sees on its HDMI input: PCIe link, signal lock, detected
mode and colour format, frames per second, "no input" frames. Close every other app using the card first.
```sh
g++ -std=c++17 -O1 tests/tools/decklink_probe/probe.cpp include/DeckLinkAPIDispatch.cpp -Iinclude -ldl -lpthread -o /tmp/probe
/tmp/probe 12                 # 12 s, captures as 8-bit YUV
PF=argb /tmp/probe 12         # capture as 8-bit ARGB (needed for RGB sources)
PF=rgb10 /tmp/probe 12        # capture as 10-bit RGB
```

## recorder_smoke
Feeds synthetic 1080p60 UYVY frames through VideoRecorder at real-time pace and verifies the file.
```sh
cmake -S tests/tools/recorder_smoke -B /tmp/smoke && cmake --build /tmp/smoke -j4
QT_QPA_PLATFORM=offscreen /tmp/smoke/smoke 10 /tmp/smoke.mp4
g++ tests/tools/recorder_smoke/check.cpp -o /tmp/check $(pkg-config --cflags --libs libavformat libavcodec libavutil) && /tmp/check /tmp/smoke.mp4
```
