// runa_samples <folder>: writes small test files with known tags, for the
// tests. sample.mp3 and sample.wav are written by hand; the rest are encoded
// (a second of grey frames and a tone) with FFmpeg's own encoders.
// Every file starts with title "Sample" and artist "Someone".
//
// runa_samples --packets <file>: a fingerprint of every packet in every
// stream, to check that writing tags left the media itself alone.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/adler32.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

void be32(std::vector<unsigned char>& b, unsigned v)
{
    for (int s = 24; s >= 0; s -= 8) b.push_back((unsigned char)(v >> s));
}
void le32(std::vector<unsigned char>& b, unsigned v)
{
    for (int s = 0; s < 32; s += 8) b.push_back((unsigned char)(v >> s));
}
void le16(std::vector<unsigned char>& b, unsigned v)
{
    b.push_back((unsigned char)v);
    b.push_back((unsigned char)(v >> 8));
}
void put(std::vector<unsigned char>& b, const std::string& s) { b.insert(b.end(), s.begin(), s.end()); }

bool save(const std::string& path, const std::vector<unsigned char>& b)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(b.data(), 1, b.size(), f);
    std::fclose(f);
    return true;
}

// ID3v2.3 with TIT2 and TPE1, then a second of silent MPEG-1 layer III frames
// (128 kb/s, 44.1 kHz: 417 bytes each; all-zero side info decodes to silence).
bool makeMp3(const std::string& path)
{
    std::vector<unsigned char> frames;
    auto frame = [&](const char* id, const std::string& text) {
        put(frames, id);
        be32(frames, unsigned(text.size() + 1));
        frames.push_back(0);
        frames.push_back(0);
        frames.push_back(0);   // ISO-8859-1
        put(frames, text);
    };
    frame("TIT2", "Sample");
    frame("TPE1", "Someone");
    std::vector<unsigned char> b;
    put(b, "ID3");
    b.push_back(3);
    b.push_back(0);
    b.push_back(0);
    const unsigned n = unsigned(frames.size());
    b.push_back((n >> 21) & 0x7f);
    b.push_back((n >> 14) & 0x7f);
    b.push_back((n >> 7) & 0x7f);
    b.push_back(n & 0x7f);
    b.insert(b.end(), frames.begin(), frames.end());
    for (int i = 0; i < 40; ++i) {
        b.push_back(0xFF);
        b.push_back(0xFB);
        b.push_back(0x90);
        b.push_back(0x00);
        b.insert(b.end(), 413, 0);
    }
    return save(path, b);
}

// 16-bit stereo PCM, half a second of a tone, with a LIST INFO chunk.
bool makeWav(const std::string& path)
{
    const int rate = 44100, samples = rate / 2;
    std::vector<unsigned char> pcm;
    for (int i = 0; i < samples; ++i) {
        const int v = int(8000 * std::sin(2 * 3.14159265 * 440 * i / rate));
        le16(pcm, unsigned(v & 0xffff));
        le16(pcm, unsigned(v & 0xffff));
    }
    std::vector<unsigned char> info;
    put(info, "INFO");
    auto item = [&](const char* id, const std::string& text) {
        put(info, id);
        le32(info, unsigned(text.size() + 1));
        put(info, text);
        info.push_back(0);
        if ((text.size() + 1) & 1) info.push_back(0);
    };
    item("INAM", "Sample");
    item("IART", "Someone");

    std::vector<unsigned char> b;
    put(b, "RIFF");
    le32(b, unsigned(4 + 8 + 16 + 8 + info.size() + 8 + pcm.size()));
    put(b, "WAVE");
    put(b, "fmt ");
    le32(b, 16);
    le16(b, 1);
    le16(b, 2);
    le32(b, rate);
    le32(b, rate * 4);
    le16(b, 4);
    le16(b, 16);
    put(b, "LIST");
    le32(b, unsigned(info.size()));
    b.insert(b.end(), info.begin(), info.end());
    put(b, "data");
    le32(b, unsigned(pcm.size()));
    b.insert(b.end(), pcm.begin(), pcm.end());
    return save(path, b);
}

bool drain(AVCodecContext* enc, AVFrame* frame, AVStream* st, AVFormatContext* oc)
{
    if (avcodec_send_frame(enc, frame) < 0) return false;
    AVPacket* pkt = av_packet_alloc();
    while (avcodec_receive_packet(enc, pkt) == 0) {
        av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
        pkt->stream_index = st->index;
        if (av_interleaved_write_frame(oc, pkt) < 0) {
            av_packet_free(&pkt);
            return false;
        }
    }
    av_packet_free(&pkt);
    return true;
}

// A second of a tone, encoded with @p id.
bool makeAudio(const std::string& path, AVCodecID id, AVSampleFormat fmt, int rate)
{
    AVFormatContext* oc = nullptr;
    if (avformat_alloc_output_context2(&oc, nullptr, nullptr, path.c_str()) < 0) return false;
    const AVCodec* codec = avcodec_find_encoder(id);
    if (!codec) return false;
    AVCodecContext* enc = avcodec_alloc_context3(codec);
    enc->sample_fmt = fmt;
    enc->sample_rate = rate;
    av_channel_layout_default(&enc->ch_layout, 2);
    enc->bit_rate = 128000;
    enc->time_base = {1, rate};
    enc->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;   // FFmpeg's own Vorbis and Opus encoders
    if (oc->oformat->flags & AVFMT_GLOBALHEADER) enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(enc, codec, nullptr) < 0) return false;
    AVStream* st = avformat_new_stream(oc, nullptr);
    avcodec_parameters_from_context(st->codecpar, enc);
    st->time_base = enc->time_base;
    av_dict_set(&oc->metadata, "title", "Sample", 0);
    av_dict_set(&oc->metadata, "artist", "Someone", 0);
    if (std::string(oc->oformat->name) == "aiff") av_dict_set(&oc->metadata, "author", "Someone", 0);   // AIFF's AUTH chunk
    if (avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) return false;
    if (avformat_write_header(oc, nullptr) < 0) return false;

    AVFrame* a = av_frame_alloc();
    a->format = fmt;
    a->nb_samples = enc->frame_size > 0 ? enc->frame_size : 1024;
    a->sample_rate = rate;
    av_channel_layout_copy(&a->ch_layout, &enc->ch_layout);
    av_frame_get_buffer(a, 0);
    for (int done = 0; done < rate; done += a->nb_samples) {
        av_frame_make_writable(a);
        for (int i = 0; i < a->nb_samples; ++i) {
            const double v = 0.2 * std::sin(2 * 3.14159265 * 440 * (done + i) / double(rate));
            if (fmt == AV_SAMPLE_FMT_FLTP) {
                reinterpret_cast<float*>(a->data[0])[i] = float(v);
                reinterpret_cast<float*>(a->data[1])[i] = float(v);
            } else {   // 16-bit, interleaved
                reinterpret_cast<int16_t*>(a->data[0])[2 * i] = int16_t(v * 32767);
                reinterpret_cast<int16_t*>(a->data[0])[2 * i + 1] = int16_t(v * 32767);
            }
        }
        a->pts = done;
        if (!drain(enc, a, st, oc)) return false;
    }
    drain(enc, nullptr, st, oc);
    av_write_trailer(oc);
    avio_closep(&oc->pb);
    av_frame_free(&a);
    avcodec_free_context(&enc);
    avformat_free_context(oc);
    return true;
}

// Prints, per stream: packets, bytes, and an Adler-32 of all their data.
int packets(const char* path)
{
    AVFormatContext* ic = nullptr;
    if (avformat_open_input(&ic, path, nullptr, nullptr) < 0) return 1;
    avformat_find_stream_info(ic, nullptr);
    std::vector<unsigned long> sums(ic->nb_streams, 1);
    std::vector<long long> counts(ic->nb_streams, 0), bytes(ic->nb_streams, 0);
    AVPacket* pkt = av_packet_alloc();
    while (av_read_frame(ic, pkt) >= 0) {
        const int s = pkt->stream_index;
        sums[s] = av_adler32_update(sums[s], pkt->data, size_t(pkt->size));
        ++counts[s];
        bytes[s] += pkt->size;
        av_packet_unref(pkt);
    }
    for (unsigned i = 0; i < ic->nb_streams; ++i) std::printf("%u %lld %lld %08lx\n", i, counts[i], bytes[i], sums[i]);
    av_packet_free(&pkt);
    avformat_close_input(&ic);
    return 0;
}

// A second of 64x64 MPEG-4 video at 10 fps and an AAC tone.
bool makeVideo(const std::string& path, const char* movflags = nullptr)
{
    AVFormatContext* oc = nullptr;
    if (avformat_alloc_output_context2(&oc, nullptr, nullptr, path.c_str()) < 0) return false;
    const bool global = oc->oformat->flags & AVFMT_GLOBALHEADER;

    const AVCodec* vc = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    AVCodecContext* ve = avcodec_alloc_context3(vc);
    ve->width = 64;
    ve->height = 64;
    ve->pix_fmt = AV_PIX_FMT_YUV420P;
    ve->time_base = {1, 10};
    ve->framerate = {10, 1};
    ve->bit_rate = 200000;
    if (global) ve->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(ve, vc, nullptr) < 0) return false;
    AVStream* vs = avformat_new_stream(oc, nullptr);
    avcodec_parameters_from_context(vs->codecpar, ve);
    vs->time_base = ve->time_base;

    const AVCodec* ac = avcodec_find_encoder(AV_CODEC_ID_AAC);
    AVCodecContext* ae = avcodec_alloc_context3(ac);
    ae->sample_fmt = AV_SAMPLE_FMT_FLTP;
    ae->sample_rate = 48000;
    av_channel_layout_default(&ae->ch_layout, 2);
    ae->bit_rate = 96000;
    ae->time_base = {1, 48000};
    if (global) ae->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(ae, ac, nullptr) < 0) return false;
    AVStream* as = avformat_new_stream(oc, nullptr);
    avcodec_parameters_from_context(as->codecpar, ae);
    as->time_base = {1, 48000};

    av_dict_set(&oc->metadata, "title", "Sample", 0);
    av_dict_set(&oc->metadata, "artist", "Someone", 0);
    if (avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) return false;
    AVDictionary* opts = nullptr;
    if (movflags) av_dict_set(&opts, "movflags", movflags, 0);
    const int header = avformat_write_header(oc, &opts);
    av_dict_free(&opts);
    if (header < 0) return false;

    AVFrame* f = av_frame_alloc();
    f->format = ve->pix_fmt;
    f->width = ve->width;
    f->height = ve->height;
    av_frame_get_buffer(f, 0);
    for (int i = 0; i < 10; ++i) {
        av_frame_make_writable(f);
        for (int y = 0; y < 64; ++y) std::memset(f->data[0] + y * f->linesize[0], 40 + i * 15, 64);
        for (int y = 0; y < 32; ++y) {
            std::memset(f->data[1] + y * f->linesize[1], 128, 32);
            std::memset(f->data[2] + y * f->linesize[2], 128, 32);
        }
        f->pts = i;
        if (!drain(ve, f, vs, oc)) return false;
    }
    drain(ve, nullptr, vs, oc);

    AVFrame* a = av_frame_alloc();
    a->format = ae->sample_fmt;
    a->nb_samples = ae->frame_size;
    a->sample_rate = ae->sample_rate;
    av_channel_layout_copy(&a->ch_layout, &ae->ch_layout);
    av_frame_get_buffer(a, 0);
    for (int done = 0; done < 48000; done += a->nb_samples) {
        av_frame_make_writable(a);
        for (int c = 0; c < 2; ++c) {
            auto* s = reinterpret_cast<float*>(a->data[c]);
            for (int i = 0; i < a->nb_samples; ++i) s[i] = 0.2f * float(std::sin(2 * 3.14159265 * 440 * (done + i) / 48000.0));
        }
        a->pts = done;
        if (!drain(ae, a, as, oc)) return false;
    }
    drain(ae, nullptr, as, oc);

    av_write_trailer(oc);
    avio_closep(&oc->pb);
    av_frame_free(&f);
    av_frame_free(&a);
    avcodec_free_context(&ve);
    avcodec_free_context(&ae);
    avformat_free_context(oc);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: runa_samples <folder> | --packets <file>\n");
        return 2;
    }
    av_log_set_level(AV_LOG_QUIET);
    if (std::string(argv[1]) == "--packets" && argc > 2) return packets(argv[2]);
    const std::string dir = std::string(argv[1]) + "/";
    bool ok = makeMp3(dir + "sample.mp3") && makeWav(dir + "sample.wav");
    for (const char* ext : {"mp4", "mov", "mkv", "avi"}) ok = ok && makeVideo(dir + "sample." + ext);
    ok = ok && makeVideo(dir + "faststart.mp4", "+faststart");                 // moov before the media
    ok = ok && makeVideo(dir + "fragmented.mp4", "frag_keyframe+empty_moov");  // like OBS's fragmented MP4
    ok = ok && makeAudio(dir + "sample.flac", AV_CODEC_ID_FLAC, AV_SAMPLE_FMT_S16, 44100);
    ok = ok && makeAudio(dir + "sample.ogg", AV_CODEC_ID_VORBIS, AV_SAMPLE_FMT_FLTP, 44100);
    ok = ok && makeAudio(dir + "sample.opus", AV_CODEC_ID_OPUS, AV_SAMPLE_FMT_FLTP, 48000);
    ok = ok && makeAudio(dir + "sample.aiff", AV_CODEC_ID_PCM_S16BE, AV_SAMPLE_FMT_S16, 44100);
    ok = ok && makeAudio(dir + "sample.m4a", AV_CODEC_ID_AAC, AV_SAMPLE_FMT_FLTP, 48000);
    std::printf(ok ? "samples written\n" : "could not write the samples\n");
    return ok ? 0 : 1;
}
