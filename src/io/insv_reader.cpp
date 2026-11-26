#include "io/insv_reader.hpp"

#include "core/gpu_info.hpp"

#include <algorithm>
#include <fstream>
#include <vector>

#ifdef PS_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#endif

namespace ps::io {

#ifdef PS_HAVE_FFMPEG
namespace {

struct HwFormatHolder {
    AVPixelFormat format = AV_PIX_FMT_NONE;
};

AVPixelFormat chooseHardwareFormat(AVCodecContext* ctx, const AVPixelFormat* formats) {
    auto* holder = static_cast<HwFormatHolder*>(ctx->opaque);
    if (holder)
        for (const AVPixelFormat* f = formats; *f != AV_PIX_FMT_NONE; ++f)
            if (*f == holder->format) return *f;
    return AV_PIX_FMT_NONE;
}

struct DecoderState {
    AVCodecContext* codec = nullptr;
    SwsContext* sws = nullptr;
    AVBufferRef* hwDevice = nullptr;
    AVPixelFormat hwFormat = AV_PIX_FMT_NONE;
    HwFormatHolder holder;
    bool hardware = false;
};

AVHWDeviceType hwDeviceType(DecodeBackend backend) {
    switch (backend) {
        case DecodeBackend::Cuda: return AV_HWDEVICE_TYPE_CUDA;
        case DecodeBackend::Qsv: return AV_HWDEVICE_TYPE_QSV;
        default: return AV_HWDEVICE_TYPE_VAAPI;
    }
}

// Tries a hardware decoder first. Any failure at setup drops back to software
// transparently, which matters on iGPUs whose VA-API driver cannot decode HEVC.
bool openHardwareDecoder(DecoderState& state, const AVCodec* codec,
                         AVCodecParameters* parameters, DecodeBackend backend) {
    if (backend == DecodeBackend::Cpu) return false;
    const AVHWDeviceType type = hwDeviceType(backend);
    const AVCodecHWConfig* config = nullptr;
    for (int i = 0; (config = avcodec_get_hw_config(codec, i)) != nullptr; ++i)
        if (config->device_type == type &&
            (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) break;
    if (!config) return false;
    const char* device = backend == DecodeBackend::Vaapi ? "/dev/dri/renderD128" : nullptr;
    if (av_hwdevice_ctx_create(&state.hwDevice, type, device, nullptr, 0) < 0) return false;
    state.codec = avcodec_alloc_context3(codec);
    if (!state.codec || avcodec_parameters_to_context(state.codec, parameters) < 0) {
        avcodec_free_context(&state.codec);
        av_buffer_unref(&state.hwDevice);
        return false;
    }
    state.holder.format = config->pix_fmt;
    state.hwFormat = config->pix_fmt;
    state.codec->opaque = &state.holder;
    state.codec->get_format = chooseHardwareFormat;
    state.codec->hw_device_ctx = av_buffer_ref(state.hwDevice);
    if (avcodec_open2(state.codec, codec, nullptr) < 0) {
        avcodec_free_context(&state.codec);
        av_buffer_unref(&state.hwDevice);
        state.hwFormat = AV_PIX_FMT_NONE;
        return false;
    }
    state.hardware = true;
    return true;
}

bool openSoftwareDecoder(DecoderState& state, const AVCodec* codec,
                         AVCodecParameters* parameters) {
    state.codec = avcodec_alloc_context3(codec);
    if (!state.codec || avcodec_parameters_to_context(state.codec, parameters) < 0 ||
        avcodec_open2(state.codec, codec, nullptr) < 0) {
        avcodec_free_context(&state.codec);
        return false;
    }
    return true;
}

// ffmpeg still reports full range HEVC as the deprecated YUVJ formats.
// Swscale warns about them, so use the equivalent non deprecated format and
// tell swscale the source is full range.
AVPixelFormat softwarePixelFormat(AVPixelFormat format) {
    switch (format) {
        case AV_PIX_FMT_YUVJ420P: return AV_PIX_FMT_YUV420P;
        case AV_PIX_FMT_YUVJ422P: return AV_PIX_FMT_YUV422P;
        case AV_PIX_FMT_YUVJ444P: return AV_PIX_FMT_YUV444P;
        case AV_PIX_FMT_YUVJ440P: return AV_PIX_FMT_YUV440P;
        default: return format;
    }
}

bool isFullRange(AVPixelFormat format) {
    return format == AV_PIX_FMT_YUVJ420P || format == AV_PIX_FMT_YUVJ422P ||
           format == AV_PIX_FMT_YUVJ444P || format == AV_PIX_FMT_YUVJ440P;
}

void applyFullRange(SwsContext* context, bool fullRange) {
    if (!fullRange) return;
    sws_setColorspaceDetails(context, sws_getCoefficients(SWS_CS_ITU601), 1,
                             sws_getCoefficients(SWS_CS_DEFAULT), 0,
                             0, 1 << 16, 1 << 16);
}

AVFrame* cpuFrame(AVFrame* decoded, AVFrame* scratch, AVPixelFormat hwFormat) {
    if (decoded->format != hwFormat) return decoded;
    return av_hwframe_transfer_data(scratch, decoded, 0) >= 0 ? scratch : decoded;
}

void releaseDecoder(DecoderState& state) {
    if (state.sws) sws_freeContext(state.sws);
    if (state.codec) avcodec_free_context(&state.codec);
    if (state.hwDevice) av_buffer_unref(&state.hwDevice);
}

} // namespace
#endif

bool isInsvFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    char header[12] = {};
    in.read(header, sizeof(header));
    // ftyp at offset 4, brand 'avc1' or 'isom' at offset 8.
    return header[4] == 'f' && header[5] == 't' && header[6] == 'y' && header[7] == 'p';
}

double insvFrameRate(const std::string& path) {
#ifndef PS_HAVE_FFMPEG
    (void)path;
    return 25.0;
#else
    AVFormatContext* ctx = nullptr;
    if (avformat_open_input(&ctx, path.c_str(), nullptr, nullptr) < 0) return 25.0;
    if (avformat_find_stream_info(ctx, nullptr) < 0) {
        avformat_close_input(&ctx);
        return 25.0;
    }
    double rate = 25.0;
    for (unsigned i = 0; i < ctx->nb_streams; ++i) {
        AVStream* s = ctx->streams[i];
        if (s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            s->avg_frame_rate.num > 0 && s->avg_frame_rate.den > 0) {
            rate = av_q2d(s->avg_frame_rate);
            break;
        }
    }
    avformat_close_input(&ctx);
    return rate > 0.0 && rate < 1000.0 ? rate : 25.0;
#endif
}

double insvDuration(const std::string& path) {
#ifndef PS_HAVE_FFMPEG
    (void)path;
    return 0.0;
#else
    AVFormatContext* ctx = nullptr;
    if (avformat_open_input(&ctx, path.c_str(), nullptr, nullptr) < 0) return 0.0;
    double duration = 0.0;
    if (avformat_find_stream_info(ctx, nullptr) >= 0 && ctx->duration != AV_NOPTS_VALUE)
        duration = static_cast<double>(ctx->duration) / AV_TIME_BASE;
    avformat_close_input(&ctx);
    return duration;
#endif
}

bool readInsvAtTimes(const std::string& path, const std::vector<double>& times,
                     std::vector<LensFrame>& out, std::string& error) {
    out.clear();
#ifndef PS_HAVE_FFMPEG
    (void)path; (void)times;
    error = "built without libav: INSV input is disabled";
    return false;
#else
    AVFormatContext* ctx = nullptr;
    if (avformat_open_input(&ctx, path.c_str(), nullptr, nullptr) < 0) {
        error = "cannot open INSV container";
        return false;
    }
    if (avformat_find_stream_info(ctx, nullptr) < 0) {
        error = "cannot read INSV streams";
        avformat_close_input(&ctx);
        return false;
    }
    std::vector<int> streams;
    for (unsigned i = 0; i < ctx->nb_streams; ++i)
        if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            streams.push_back(static_cast<int>(i));
    if (streams.size() < 2) {
        error = "expected two fisheye video tracks";
        avformat_close_input(&ctx);
        return false;
    }
    const DecodeBackend backend = detectGpu().preferredDecodeBackend();
    std::vector<DecoderState> decoders(streams.size());
    for (size_t k = 0; k < streams.size(); ++k) {
        const AVCodec* codec = avcodec_find_decoder(ctx->streams[streams[k]]->codecpar->codec_id);
        if (!codec) { error = "missing decoder"; break; }
        AVCodecParameters* parameters = ctx->streams[streams[k]]->codecpar;
        if (!openHardwareDecoder(decoders[k], codec, parameters, backend) &&
            !openSoftwareDecoder(decoders[k], codec, parameters)) {
            error = "cannot initialise decoder";
            break;
        }
    }
    auto cleanup = [&]() {
        for (auto& d : decoders) releaseDecoder(d);
        avformat_close_input(&ctx);
    };
    if (!error.empty()) { cleanup(); return false; }

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* scratch = av_frame_alloc();
    for (size_t sample = 0; sample < times.size(); ++sample) {
        const double target = std::max(0.0, times[sample]);
        const int64_t seekTs = static_cast<int64_t>(std::max(0.0, target - 0.5) * AV_TIME_BASE);
        av_seek_frame(ctx, -1, seekTs, AVSEEK_FLAG_BACKWARD);
        for (auto& d : decoders) avcodec_flush_buffers(d.codec);
        const bool twoTracks = streams.size() >= 2;
        std::vector<LensFrame> picked(2);
        std::vector<bool> found(2, false);
        int guard = 0;
        while (guard++ < 400) {
            const int rc = av_read_frame(ctx, packet);
            if (rc < 0) break;
            auto it = std::find(streams.begin(), streams.end(), packet->stream_index);
            if (it == streams.end()) { av_packet_unref(packet); continue; }
            const size_t track = static_cast<size_t>(it - streams.begin());
            const size_t decoderIndex = std::min(track, decoders.size() - 1);
            if (avcodec_send_packet(decoders[decoderIndex].codec, packet) < 0) {
                av_packet_unref(packet);
                continue;
            }
            int rr = 0;
            while ((rr = avcodec_receive_frame(decoders[decoderIndex].codec, frame)) == 0) {
                const AVRational tb = ctx->streams[streams[decoderIndex]]->time_base;
                const double ts = static_cast<double>(frame->best_effort_timestamp) * av_q2d(tb);
                const bool wanted = twoTracks ? !found[track] : (!found[0] || !found[1]);
                if (wanted && ts >= target - 0.3) {
                    AVFrame* ready = cpuFrame(frame, scratch, decoders[decoderIndex].hwFormat);
                    const int w = ready->width, h = ready->height;
                    cv::Mat bgr(h, w, CV_8UC3);
                    if (!decoders[decoderIndex].sws) {
                        const auto source = static_cast<AVPixelFormat>(ready->format);
                        decoders[decoderIndex].sws = sws_getContext(
                            w, h, softwarePixelFormat(source),
                            w, h, AV_PIX_FMT_BGR24, SWS_BILINEAR,
                            nullptr, nullptr, nullptr);
                        applyFullRange(decoders[decoderIndex].sws, isFullRange(source));
                    }
                    uint8_t* dst[] = {bgr.data};
                    int stride[] = {static_cast<int>(bgr.step)};
                    sws_scale(decoders[decoderIndex].sws, ready->data, ready->linesize,
                              0, h, dst, stride);
                    auto makeFrame = [&](int lens, const cv::Mat& image) {
                        picked[lens].image = image.clone();
                        picked[lens].lens = lens;
                        picked[lens].source = path;
                        picked[lens].sequence = static_cast<int>(sample);
                        picked[lens].timestamp = ts;
                        found[lens] = true;
                    };
                    if (twoTracks) {
                        makeFrame(static_cast<int>(track), bgr);
                    } else {
                        // Single stream: side by side or stacked dual fisheye.
                        if (bgr.cols == 2 * bgr.rows) {
                            makeFrame(0, bgr(cv::Rect(0, 0, bgr.cols / 2, bgr.rows)));
                            makeFrame(1, bgr(cv::Rect(bgr.cols / 2, 0,
                                                      bgr.cols / 2, bgr.rows)));
                        } else if (bgr.rows == 2 * bgr.cols) {
                            makeFrame(0, bgr(cv::Rect(0, 0, bgr.cols, bgr.rows / 2)));
                            makeFrame(1, bgr(cv::Rect(0, bgr.rows / 2,
                                                      bgr.cols, bgr.rows / 2)));
                        } else {
                            makeFrame(0, bgr);
                            found[1] = true; // no second lens, pairing will skip
                        }
                    }
                }
                av_frame_unref(frame);
                bool all = true;
                for (bool f : found) if (!f) all = false;
                if (all) break;
            }
            av_packet_unref(packet);
            bool all = true;
            for (bool f : found) if (!f) all = false;
            if (all) break;
        }
        bool all = true;
        for (bool f : found) if (!f) all = false;
        if (!all) continue;
        for (auto& f : picked) out.push_back(std::move(f));
    }
    av_frame_free(&frame);
    av_frame_free(&scratch);
    av_packet_free(&packet);
    cleanup();
    if (out.empty()) error = "no frames decoded at the requested timestamps";
    return !out.empty();
#endif
}

bool readInsv(const std::string& path, std::vector<LensFrame>& out,
              int maxFrames, int step, std::string& error) {
    out.clear();
#ifndef PS_HAVE_FFMPEG
    error = "built without libav: INSV input is disabled";
    return false;
#else
    if (!isInsvFile(path)) { error = "not an MP4/INSV container"; return false; }
    if (step < 1) step = 1;

    AVFormatContext* ctx = nullptr;
    if (avformat_open_input(&ctx, path.c_str(), nullptr, nullptr) < 0) {
        error = "cannot open INSV container";
        return false;
    }
    if (avformat_find_stream_info(ctx, nullptr) < 0) {
        error = "cannot read INSV streams";
        avformat_close_input(&ctx);
        return false;
    }
    std::vector<int> videoStreams;
    for (unsigned i = 0; i < ctx->nb_streams; ++i)
        if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            videoStreams.push_back(static_cast<int>(i));
    if (videoStreams.empty()) {
        error = "no video tracks in INSV";
        avformat_close_input(&ctx);
        return false;
    }

    const DecodeBackend backend = detectGpu().preferredDecodeBackend();
    std::vector<DecoderState> decoders(videoStreams.size());
    for (size_t k = 0; k < videoStreams.size(); ++k) {
        AVStream* stream = ctx->streams[videoStreams[k]];
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) { error = "missing decoder for INSV video"; break; }
        if (!openHardwareDecoder(decoders[k], codec, stream->codecpar, backend) &&
            !openSoftwareDecoder(decoders[k], codec, stream->codecpar)) {
            error = "cannot initialise INSV decoder";
            break;
        }
    }
    if (!error.empty()) {
        for (auto& d : decoders) releaseDecoder(d);
        avformat_close_input(&ctx);
        return false;
    }

    const bool twoTracks = videoStreams.size() >= 2;
    std::vector<std::vector<LensFrame>> perLens(2);
    std::vector<long long> decodedCount(videoStreams.size(), 0);
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* scratch = av_frame_alloc();
    long long frameIndex = 0;
    int readResult = 0;
    while ((readResult = av_read_frame(ctx, packet)) >= 0) {
        auto it = std::find(videoStreams.begin(), videoStreams.end(), packet->stream_index);
        if (it == videoStreams.end()) { av_packet_unref(packet); continue; }
        const size_t lens = static_cast<size_t>(it - videoStreams.begin());
        if (lens >= decoders.size()) { av_packet_unref(packet); continue; }
        DecoderState& decoder = decoders[lens];
        if (avcodec_send_packet(decoder.codec, packet) < 0) { av_packet_unref(packet); continue; }
        int receiveResult = 0;
        while ((receiveResult = avcodec_receive_frame(decoder.codec, frame)) == 0) {
            // Decimation is per lens. The two tracks are interleaved, so a
            // global counter would pick alternating lenses and break pairing.
            if (decodedCount[lens] % step == 0) {
                AVFrame* ready = cpuFrame(frame, scratch, decoder.hwFormat);
                const int w = ready->width, h = ready->height;
                cv::Mat bgr(h, w, CV_8UC3);
                if (!decoder.sws) {
                    const auto source = static_cast<AVPixelFormat>(ready->format);
                    decoder.sws = sws_getContext(w, h, softwarePixelFormat(source),
                                                 w, h, AV_PIX_FMT_BGR24, SWS_BILINEAR,
                                                 nullptr, nullptr, nullptr);
                    applyFullRange(decoder.sws, isFullRange(source));
                }
                uint8_t* dst[] = {bgr.data};
                int dstLine[] = {static_cast<int>(bgr.step)};
                sws_scale(decoder.sws, ready->data, ready->linesize, 0, h, dst, dstLine);
                const AVRational tb = ctx->streams[packet->stream_index]->time_base;
                const double ts = static_cast<double>(frame->best_effort_timestamp) *
                                  av_q2d(tb);
                auto emit = [&](int outLens, const cv::Mat& image) {
                    LensFrame lf;
                    lf.image = image.clone();
                    lf.lens = outLens;
                    lf.source = path;
                    lf.sequence = static_cast<int>(perLens[outLens].size());
                    lf.timestamp = ts;
                    perLens[outLens].push_back(std::move(lf));
                };
                if (twoTracks) {
                    emit(static_cast<int>(lens), bgr);
                } else if (bgr.cols == 2 * bgr.rows) {
                    emit(0, bgr(cv::Rect(0, 0, bgr.cols / 2, bgr.rows)));
                    emit(1, bgr(cv::Rect(bgr.cols / 2, 0, bgr.cols / 2, bgr.rows)));
                } else if (bgr.rows == 2 * bgr.cols) {
                    emit(0, bgr(cv::Rect(0, 0, bgr.cols, bgr.rows / 2)));
                    emit(1, bgr(cv::Rect(0, bgr.rows / 2, bgr.cols, bgr.rows / 2)));
                } else {
                    emit(0, bgr);
                }
            }
            ++frameIndex;
            ++decodedCount[lens];
            av_frame_unref(frame);
        }
        av_packet_unref(packet);
        if (maxFrames > 0) {
            bool allReady = true;
            for (const auto& v : perLens)
                if (static_cast<int>(v.size()) < maxFrames) allReady = false;
            if (allReady) break;
        }
    }
    av_frame_free(&frame);
    av_frame_free(&scratch);
    av_packet_free(&packet);
    for (auto& d : decoders) releaseDecoder(d);
    avformat_close_input(&ctx);

    size_t count = perLens.front().size();
    for (auto& v : perLens) count = std::min(count, v.size());
    for (size_t i = 0; i < count; ++i)
        for (size_t lens = 0; lens < perLens.size(); ++lens)
            out.push_back(perLens[lens][i]);
    if (out.empty()) error = "no frames decoded from INSV";
    return !out.empty();
#endif
}

} // namespace ps::io
