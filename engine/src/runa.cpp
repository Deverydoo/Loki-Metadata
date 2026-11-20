// Runa: container tags through FFmpeg. See runa.h.

#include "runa.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavutil/mem.h>
}

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

void quietLogs()
{
    static std::once_flag once;
    std::call_once(once, [] { av_log_set_level(AV_LOG_ERROR); });
}

int fail(char* error, int size, const std::string& why)
{
    if (error && size > 0) {
        const size_t n = std::min(why.size(), size_t(size - 1));
        std::memcpy(error, why.data(), n);
        error[n] = 0;
    }
    return 1;
}

std::string avError(int code)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buf, sizeof buf);
    return buf;
}

std::wstring widen(const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

struct Input {
    AVFormatContext* fc = nullptr;
    ~Input() { avformat_close_input(&fc); }
};

struct Output {
    AVFormatContext* fc = nullptr;
    ~Output()
    {
        if (!fc) return;
        if (fc->pb && !(fc->oformat->flags & AVFMT_NOFILE)) avio_closep(&fc->pb);
        avformat_free_context(fc);
    }
};

int openInput(Input& in, const char* path, char* error, int errorSize)
{
    int r = avformat_open_input(&in.fc, path, nullptr, nullptr);
    if (r < 0) return fail(error, errorSize, "Can't open the file: " + avError(r));
    // One decoder thread per stream while probing: by default each video
    // decoder starts a thread (and frame buffers) per core, for every file
    // read at once, which adds up to hundreds of MB over a big folder.
    std::vector<AVDictionary*> opts(in.fc->nb_streams, nullptr);
    for (AVDictionary*& d : opts) av_dict_set(&d, "threads", "1", 0);
    r = avformat_find_stream_info(in.fc, opts.empty() ? nullptr : opts.data());
    for (AVDictionary*& d : opts) av_dict_free(&d);
    if (r < 0) return fail(error, errorSize, "Can't read the file's streams: " + avError(r));
    return 0;
}

std::string clock(double seconds)
{
    const long long s = (long long)(seconds + 0.5);
    char buf[32];
    if (s >= 3600) std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    else std::snprintf(buf, sizeof buf, "%lld:%02lld", s / 60, s % 60);
    return buf;
}

std::string codecName(AVCodecID id)
{
    switch (id) {
    case AV_CODEC_ID_H264: return "H.264";
    case AV_CODEC_ID_HEVC: return "HEVC";
    case AV_CODEC_ID_AV1: return "AV1";
    case AV_CODEC_ID_VP9: return "VP9";
    case AV_CODEC_ID_VP8: return "VP8";
    case AV_CODEC_ID_MPEG4: return "MPEG-4";
    case AV_CODEC_ID_PRORES: return "ProRes";
    case AV_CODEC_ID_AAC: return "AAC";
    case AV_CODEC_ID_MP3: return "MP3";
    case AV_CODEC_ID_OPUS: return "Opus";
    case AV_CODEC_ID_VORBIS: return "Vorbis";
    case AV_CODEC_ID_FLAC: return "FLAC";
    case AV_CODEC_ID_AC3: return "AC-3";
    case AV_CODEC_ID_EAC3: return "E-AC-3";
    case AV_CODEC_ID_MJPEG: return "JPEG";
    case AV_CODEC_ID_PNG: return "PNG";
    default: break;
    }
    if (const AVCodecDescriptor* d = avcodec_descriptor_get(id)) {
        std::string n = d->name;
        if (n.rfind("pcm_", 0) == 0) {
            const int bits = av_get_bits_per_sample(id);
            return bits > 0 ? "PCM " + std::to_string(bits) + "-bit" : "PCM";
        }
        for (char& c : n) c = char(toupper((unsigned char)c));
        return n;
    }
    return "Unknown";
}

std::string channels(int n)
{
    if (n == 1) return "mono";
    if (n == 2) return "stereo";
    if (n == 6) return "5.1";
    if (n == 8) return "7.1";
    return std::to_string(n) + " channels";
}

std::string fps(AVRational r)
{
    if (r.num <= 0 || r.den <= 0) return {};
    const double f = av_q2d(r);
    char buf[32];
    if (std::abs(f - (long long)(f + 0.5)) < 0.005) std::snprintf(buf, sizeof buf, "%lld fps", (long long)(f + 0.5));
    else std::snprintf(buf, sizeof buf, "%.2f fps", f);
    return buf;
}

// Keep the stream's codec tag when the output format knows it for this
// codec; otherwise let the muxer choose (as FFmpeg's own remux does).
unsigned int keptTag(const AVOutputFormat* of, const AVCodecParameters* p)
{
    if (!p->codec_tag || !of->codec_tag) return p->codec_tag;
    if (av_codec_get_id(of->codec_tag, p->codec_tag) == p->codec_id) return p->codec_tag;
    unsigned int other = 0;
    if (!av_codec_get_tag2(of->codec_tag, p->codec_id, &other)) return p->codec_tag;
    return 0;
}

} // namespace

RUNA_API int runa_read(const char* path, runa_pair_fn tag, runa_pair_fn info, void* ctx, char* error, int errorSize)
{
    quietLogs();
    Input in;
    if (openInput(in, path, error, errorSize)) return 1;
    AVFormatContext* fc = in.fc;

    if (tag) {
        const AVDictionaryEntry* e = nullptr;
        while ((e = av_dict_iterate(fc->metadata, e))) tag(ctx, e->key, e->value);
    }
    if (!info) return 0;

    info(ctx, "Format", fc->iformat->long_name ? fc->iformat->long_name : fc->iformat->name);
    if (fc->duration > 0) info(ctx, "Duration", clock(fc->duration / double(AV_TIME_BASE)).c_str());
    if (fc->bit_rate > 0) info(ctx, "Bitrate", (std::to_string((fc->bit_rate + 500) / 1000) + " kb/s").c_str());

    int videos = 0, audios = 0, subs = 0;
    for (unsigned i = 0; i < fc->nb_streams; ++i) {
        const AVCodecParameters* p = fc->streams[i]->codecpar;
        if (p->codec_type == AVMEDIA_TYPE_VIDEO && !(fc->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC)) ++videos;
        else if (p->codec_type == AVMEDIA_TYPE_AUDIO) ++audios;
        else if (p->codec_type == AVMEDIA_TYPE_SUBTITLE) ++subs;
    }
    int v = 0, a = 0, s = 0;
    for (unsigned i = 0; i < fc->nb_streams; ++i) {
        const AVStream* st = fc->streams[i];
        const AVCodecParameters* p = st->codecpar;
        std::string label, text = codecName(p->codec_id);
        if (p->codec_type == AVMEDIA_TYPE_VIDEO && (st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            label = "Cover art";
            text += ", " + std::to_string(p->width) + "\xC3\x97" + std::to_string(p->height);
        } else if (p->codec_type == AVMEDIA_TYPE_VIDEO) {
            ++v;
            label = videos > 1 ? "Video " + std::to_string(v) : "Video";
            text += ", " + std::to_string(p->width) + "\xC3\x97" + std::to_string(p->height);
            const std::string f = fps(st->avg_frame_rate.num ? st->avg_frame_rate : st->r_frame_rate);
            if (!f.empty()) text += ", " + f;
        } else if (p->codec_type == AVMEDIA_TYPE_AUDIO) {
            ++a;
            label = audios > 1 ? "Audio " + std::to_string(a) : "Audio";
            if (p->sample_rate > 0) {
                char buf[32];
                if (p->sample_rate % 1000 == 0) std::snprintf(buf, sizeof buf, "%d kHz", p->sample_rate / 1000);
                else std::snprintf(buf, sizeof buf, "%.1f kHz", p->sample_rate / 1000.0);
                text += std::string(", ") + buf;
            }
            if (p->ch_layout.nb_channels > 0) text += ", " + channels(p->ch_layout.nb_channels);
            // A track's name tells several apart (Game, Mic...); with one, it is just the song's title.
            if (const AVDictionaryEntry* t = av_dict_get(st->metadata, "title", nullptr, 0); t && audios > 1)
                text += " (" + std::string(t->value) + ")";
        } else if (p->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            ++s;
            label = subs > 1 ? "Subtitles " + std::to_string(s) : "Subtitles";
        } else {
            label = "Data";
        }
        if (const AVDictionaryEntry* l = av_dict_get(st->metadata, "language", nullptr, 0);
            l && p->codec_type != AVMEDIA_TYPE_VIDEO && std::strcmp(l->value, "und") != 0)
            text += ", " + std::string(l->value);
        info(ctx, label.c_str(), text.c_str());
    }
    if (fc->nb_chapters) info(ctx, "Chapters", std::to_string(fc->nb_chapters).c_str());
    return 0;
}

RUNA_API int runa_write(const char* path, const char* const* keys, const char* const* values, int count, char* error,
                        int errorSize)
{
    quietLogs();
    const std::string target = path;
    // The new file goes beside the old one, with the same extension so the
    // right format is picked: "song.mp3" is written as "song.runa-tmp.mp3".
    const size_t slash = target.find_last_of("/\\");
    const size_t dot = target.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return fail(error, errorSize, "The file has no extension, so its format can't be told.");
    const std::string temp = target.substr(0, dot) + ".runa-tmp" + target.substr(dot);

    const AVOutputFormat* of = av_guess_format(nullptr, path, nullptr);
    if (!of) return fail(error, errorSize, "Writing tags isn't supported for this kind of file.");

    {
        Input in;
        if (openInput(in, path, error, errorSize)) return 1;

        Output out;
        int r = avformat_alloc_output_context2(&out.fc, of, nullptr, temp.c_str());
        if (r < 0 || !out.fc) return fail(error, errorSize, "Can't start writing: " + avError(r));

        // The tags: the file's own, with the changes on top.
        av_dict_copy(&out.fc->metadata, in.fc->metadata, 0);
        for (int i = 0; i < count; ++i) {
            const char* value = values[i] && *values[i] ? values[i] : nullptr;
            av_dict_set(&out.fc->metadata, keys[i], value, 0);
        }

        // Every stream, copied as it is.
        std::vector<int> map(in.fc->nb_streams, -1);
        for (unsigned i = 0; i < in.fc->nb_streams; ++i) {
            const AVStream* is = in.fc->streams[i];
            AVStream* os = avformat_new_stream(out.fc, nullptr);
            if (!os) return fail(error, errorSize, "Out of memory.");
            avcodec_parameters_copy(os->codecpar, is->codecpar);
            os->codecpar->codec_tag = keptTag(of, is->codecpar);
            os->time_base = is->time_base;
            os->avg_frame_rate = is->avg_frame_rate;
            os->r_frame_rate = is->r_frame_rate;
            os->sample_aspect_ratio = is->sample_aspect_ratio;
            os->disposition = is->disposition;
            av_dict_copy(&os->metadata, is->metadata, 0);
            map[i] = os->index;
        }

        // Chapters.
        if (in.fc->nb_chapters) {
            out.fc->chapters = static_cast<AVChapter**>(av_calloc(in.fc->nb_chapters, sizeof(AVChapter*)));
            if (!out.fc->chapters) return fail(error, errorSize, "Out of memory.");
        }
        for (unsigned i = 0; i < in.fc->nb_chapters; ++i) {
            const AVChapter* ic = in.fc->chapters[i];
            auto* oc = static_cast<AVChapter*>(av_mallocz(sizeof(AVChapter)));
            if (!oc) return fail(error, errorSize, "Out of memory.");
            oc->id = ic->id;
            oc->time_base = ic->time_base;
            oc->start = ic->start;
            oc->end = ic->end;
            av_dict_copy(&oc->metadata, ic->metadata, 0);
            out.fc->chapters[out.fc->nb_chapters++] = oc;
        }

        if (!(of->flags & AVFMT_NOFILE)) {
            r = avio_open(&out.fc->pb, temp.c_str(), AVIO_FLAG_WRITE);
            if (r < 0) return fail(error, errorSize, "Can't write in this folder: " + avError(r));
        }

        AVDictionary* opts = nullptr;
        const std::string name = of->name;
        if (name == "mp3") av_dict_set(&opts, "id3v2_version", "3", 0);   // what Windows and most players read best
        if (name == "wav") {
            av_dict_set(&opts, "write_bext", "1", 0);   // keep broadcast-WAV details
            av_dict_set(&opts, "rf64", "auto", 0);       // files over 4 GB
        }
        r = avformat_write_header(out.fc, &opts);
        av_dict_free(&opts);
        auto abandon = [&](const std::string& why) {
            if (out.fc->pb) avio_closep(&out.fc->pb);
            DeleteFileW(widen(temp).c_str());
            return fail(error, errorSize, why);
        };
        if (r < 0) return abandon("This file's streams can't be written back as " + name + ": " + avError(r));

        AVPacket* pkt = av_packet_alloc();
        while ((r = av_read_frame(in.fc, pkt)) >= 0) {
            const int to = pkt->stream_index < int(map.size()) ? map[pkt->stream_index] : -1;
            if (to < 0) {
                av_packet_unref(pkt);
                continue;
            }
            av_packet_rescale_ts(pkt, in.fc->streams[pkt->stream_index]->time_base, out.fc->streams[to]->time_base);
            pkt->stream_index = to;
            pkt->pos = -1;
            r = av_interleaved_write_frame(out.fc, pkt);
            if (r < 0) {
                av_packet_free(&pkt);
                return abandon("Copying the streams failed: " + avError(r));
            }
        }
        av_packet_free(&pkt);
        if (r != AVERROR_EOF) return abandon("Reading the file failed partway: " + avError(r));
        r = av_write_trailer(out.fc);
        if (r < 0) return abandon("Finishing the file failed: " + avError(r));
        avio_closep(&out.fc->pb);
    }   // input closed here, so the original can be replaced

    // Swap the new file in. ReplaceFile keeps the original's creation date,
    // attributes and permissions.
    const std::wstring wTarget = widen(target), wTemp = widen(temp);
    if (!ReplaceFileW(wTarget.c_str(), wTemp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
        const DWORD code = GetLastError();
        DeleteFileW(wTemp.c_str());
        if (code == ERROR_SHARING_VIOLATION || code == ERROR_ACCESS_DENIED || code == ERROR_UNABLE_TO_REMOVE_REPLACED)
            return fail(error, errorSize, "The file is open in another program, or read-only. Close it and try again.");
        return fail(error, errorSize, "Couldn't replace the file (Windows error " + std::to_string(code) + ").");
    }
    return 0;
}
