extern "C" {
#include <libavutil/pixdesc.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
#include <cstdio>
#include <cstdlib>
// Frame k in the file was fed with i = k+1: bar where ((x + i*8) % 400) < 40 -> Y=235 else 16, U=V=128
int main(int c, char** v) {
  AVFormatContext* fc = nullptr; avformat_open_input(&fc, v[1], nullptr, nullptr); avformat_find_stream_info(fc, nullptr);
  const AVCodec* dec = avcodec_find_decoder(fc->streams[0]->codecpar->codec_id);
  AVCodecContext* dc = avcodec_alloc_context3(dec); avcodec_parameters_to_context(dc, fc->streams[0]->codecpar); avcodec_open2(dc, dec, nullptr);
  AVPacket* p = av_packet_alloc(); AVFrame* f = av_frame_alloc(); int k = 0, checked = 0; double maxErrY = 0, maxErrC = 0; long bad = 0;
  while (av_read_frame(fc, p) >= 0 && checked < 5) {
    avcodec_send_packet(dc, p); av_packet_unref(p);
    while (avcodec_receive_frame(dc, f) == 0) {
      if (k % 60 == 0) { int i = k + 1;
        for (int y = 100; y < 1000; y += 97) for (int x = 0; x < 1920; ++x) {
          int xe = x & ~1; bool bar = ((xe + i * 8) % 400) < 40; bool edge = ((xe + i*8) % 400) < 4 || (((xe + i*8) % 400) >= 36 && ((xe+i*8)%400) < 44) || ((xe+i*8)%400) >= 396;
          int Y = f->data[0][y * f->linesize[0] + x]; int exp = bar ? 235 : 16; if (!edge) { double e = abs(Y - exp); if (e > maxErrY) maxErrY = e; if (e > 20) bad++; } }
        for (int y = 50; y < 540; y += 53) for (int x = 0; x < 960; ++x) { int U = f->data[1][y * f->linesize[1] + x]; double e = abs(U - 128); if (e > maxErrC) maxErrC = e; }
        printf("frame %d: format=%s %dx%d checked\n", k, av_get_pix_fmt_name((AVPixelFormat)f->format), f->width, f->height); checked++; }
      k++; } }
  printf("max |Y err| (away from edges) = %.0f, samples >20 off = %ld, max |U-128| = %.0f\n", maxErrY, bad, maxErrC);
  return 0; }
