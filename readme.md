cd ~/Desktop/sdkDev1/medical_qt_app

cd ..
rm -rf build
mkdir -p build && cd build
cmake ..
make -j$(nproc)

DeckLink → Frame → RecordingThread (background) → Encoder → MP4 file


unset LD_LIBRARY_PATH ./medical_qt_app


RTSP (network) --> FFmpeg demuxer --> CUVID GPU decoder --> NVENC GPU encoder --> Save to file



sudo add-apt-repository ppa:savoury1/ffmpeg4
sudo apt update
sudo apt install ffmpeg


ffmpeg -hide_banner -encoders | grep nvenc

# ===================================================================================

sudo apt update
sudo apt install -y \
  autoconf automake build-essential cmake git-core libtool make \
  nasm pkg-config texinfo wget yasm zlib1g-dev libnuma-dev \
  libx264-dev libx265-dev libvpx-dev libfdk-aac-dev libopus-dev \
  libunistring-dev libaom-dev libfreetype6-dev libfontconfig1-dev \
  libass-dev libssl-dev


cd ~
git clone https://git.ffmpeg.org/ffmpeg.git ffmpeg-nvenc
cd ffmpeg-nvenc


./configure \
  --prefix=/opt/ffmpeg-nvenc \
  --pkg-config-flags="--static" \
  --extra-cflags="-I/usr/local/cuda/include" \
  --extra-ldflags="-L/usr/local/cuda/lib64" \
  --extra-libs="-lpthread -lm" \
  --enable-cuda \
  --enable-cuvid \
  --enable-nvenc \
  --enable-libnpp \
  --enable-nonfree \
  --enable-gpl \
  --enable-libx264 \
  --enable-libx265 \
  --enable-libvpx \
  --enable-libfdk-aac \
  --enable-libopus \
  --enable-libass \
  --enable-libfreetype \
  --enable-openssl \
  --disable-debug \
  --disable-doc



make -j$(nproc)
sudo make install



cmake
 make nv12_to_mp4_test



 sudo apt update
sudo apt install -y \
  autoconf automake build-essential cmake git libass-dev libfreetype6-dev \
  libgnutls28-dev libsdl2-dev libtool libva-dev libvdpau-dev libvorbis-dev \
  libxcb1-dev libxcb-shm0-dev libxcb-xfixes0-dev meson ninja-build pkg-config \
  texinfo wget yasm zlib1g-dev nasm libx264-dev libx265-dev libnuma-dev


./configure \
  --enable-nonfree \
  --enable-cuda \
  --enable-cuvid \
  --enable-nvenc \
  --extra-cflags="-I$HOME/Desktop/sdk/Video_Codec_SDK_13.0.19/Interface" \
  --extra-ldflags="-L/usr/local/cuda/lib64" \
  --enable-libx264 \
  --enable-libx265 \
  --enable-gpl


make -j$(nproc)
sudo make install


ffmpeg -encoders | grep nvenc




curl -X POST http://localhost:8007/start-recording \
  -H "Content-Type: application/json" \
  -d '{"output_file": "recordings/video_test.mp4"}'


curl -X POST http://localhost:8007/stop-recording


curl -X POST http://localhost:8007/take-snapshot \
  -H "Content-Type: application/json" \
  -d '{"output_file": "snapshots/snapshot_20250602_150000.jpg"}'


curl -X POST http://localhost:5000/generate-pdf \
     -H "Content-Type: application/json" \
     -d '{"patient_id": "P1001", "surgery_id": 1}'


# deploy

https://github.com/probonopd/linuxdeployqt/releases

sudo apt update
sudo apt install libfuse2


./linuxdeployqt-continuous-x86_64.AppImage yourapp -appimage
./linuxdeployqt-continuous-x86_64.AppImage build/medical_qt_app -appimage

./linuxdeployqt-continuous-x86_64.AppImage build/medical_qt_app -appimage -desktop-file=medical_qt_app.desktop -icon-file=medical_qt_app.png

chmod +x Medical_Qt_App-873ab4f-x86_64.AppImage












Commands to run (all need sudo)
1. Freeze the kernel and drivers


sudo apt-mark hold linux-generic-hwe-22.04 linux-image-generic-hwe-22.04 linux-headers-generic-hwe-22.04 \
  linux-image-6.8.0-138-generic linux-headers-6.8.0-138-generic \
  linux-modules-6.8.0-138-generic linux-modules-extra-6.8.0-138-generic \
  desktopvideo desktopvideo-gui mediaexpress
sudo apt-mark hold $(dpkg -l | awk '/^ii/ && $2 ~ /^(nvidia|libnvidia|cuda)/ {print $2}')
apt-mark showhold        # check the list
2. Stop automatic updates


sudo systemctl disable --now unattended-upgrades
sudo systemctl mask apt-daily.timer apt-daily-upgrade.timer apt-daily.service apt-daily-upgrade.service
3. Stop the desktop's own updater


gsettings set org.gnome.software download-updates false          # no sudo, run as your user
gsettings set org.gnome.software allow-updates false
4. Stop snap auto-refresh


sudo snap refresh --hold