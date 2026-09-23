extern "C" {
#include <libavformat/avformat.h>
}
#include <cstdio>
int main(int c, char** v) { AVFormatContext* fc = nullptr; int r = avformat_open_input(&fc, v[1], nullptr, nullptr);
 if (r < 0) { char b[128]; av_strerror(r, b, 128); printf("NOT PLAYABLE: %s\n", b); return 1; }
 avformat_find_stream_info(fc, nullptr); AVPacket* p = av_packet_alloc(); long n = 0; int64_t last = 0;
 while (av_read_frame(fc, p) >= 0) { n++; last = p->pts; av_packet_unref(p); }
 printf("PLAYABLE: packets=%ld last_pts_seconds=%.3f\n", n, last * av_q2d(fc->streams[0]->time_base)); return 0; }
