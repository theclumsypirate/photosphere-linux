#include "io/video_writer.hpp"

#ifdef PS_HAVE_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <vector>

namespace ps::io {

namespace {
bool g_preferHardwareEncoder = true;
}

struct Mp4Writer::Impl {
    std::string path;
    int width = 0;
    int height = 0;
    double fps = 25.0;
    AVFormatContext* format = nullptr;
    AVCodecContext* encoder = nullptr;
    AVStream* stream = nullptr;
    SwsContext* sws = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    int64_t pts = 0;
    bool opened = false;
    bool finished = false;
};

void Mp4Writer::setPreferHardwareEncoder(bool enabled) {
    g_preferHardwareEncoder = enabled;
}

Mp4Writer::Mp4Writer(const std::string& path, int width, int height, double fps)
    : impl_(new Impl{path, width, height, fps, nullptr, nullptr, nullptr, nullptr,
                     nullptr, nullptr, 0, false, false}) {}

Mp4Writer::~Mp4Writer() {
    finish();
    delete impl_;
}

bool Mp4Writer::open(std::string& error) {
    if (impl_->width <= 0 || impl_->height <= 0 || (impl_->width & 1) || (impl_->height & 1)) {
        error = "video dimensions must be positive and even";
        return false;
    }
    int rc = avformat_alloc_output_context2(&impl_->format, nullptr, "mp4",
                                            impl_->path.c_str());
    if (rc < 0 || !impl_->format) { error = "cannot create MP4 output"; return false; }
    std::vector<const AVCodec*> candidates;
    if (g_preferHardwareEncoder) {
        if (const AVCodec* nvenc = avcodec_find_encoder_by_name("h264_nvenc"))
            candidates.push_back(nvenc);
    }
    if (const AVCodec* h264 = avcodec_find_encoder(AV_CODEC_ID_H264))
        candidates.push_back(h264);
    if (const AVCodec* mpeg4 = avcodec_find_encoder(AV_CODEC_ID_MPEG4))
        candidates.push_back(mpeg4);
    if (candidates.empty()) { error = "no available video encoder"; return false; }

    const AVCodec* codec = nullptr;
    std::string openError;
    for (const AVCodec* candidate : candidates) {
        AVCodecContext* context = avcodec_alloc_context3(candidate);
        if (!context) continue;
        context->codec_id = candidate->id;
        context->bit_rate = 12'000'000;
        context->width = impl_->width;
        context->height = impl_->height;
        context->time_base = AVRational{1, std::max(1, static_cast<int>(impl_->fps))};
        context->framerate = AVRational{std::max(1, static_cast<int>(impl_->fps)), 1};
        context->gop_size = 25;
        context->max_b_frames = 0;
        context->pix_fmt = AV_PIX_FMT_YUV420P;
        if (candidate->pix_fmts) {
            bool supported = false;
            for (const AVPixelFormat* p = candidate->pix_fmts; *p != AV_PIX_FMT_NONE; ++p)
                if (*p == context->pix_fmt) supported = true;
            if (!supported) context->pix_fmt = candidate->pix_fmts[0];
        }
        if (avcodec_open2(context, candidate, nullptr) >= 0) {
            impl_->encoder = context;
            codec = candidate;
            break;
        }
        openError = "cannot open video encoder";
        avcodec_free_context(&context);
    }
    if (!impl_->encoder) { error = openError.empty() ? "cannot open video encoder" : openError; return false; }
    auto* ctx = impl_->encoder;
    (void)codec;
    impl_->stream = avformat_new_stream(impl_->format, nullptr);
    if (!impl_->stream) { error = "cannot create video stream"; return false; }
    if (avcodec_parameters_from_context(impl_->stream->codecpar, ctx) < 0) {
        error = "cannot copy encoder parameters";
        return false;
    }
    impl_->stream->time_base = ctx->time_base;
    impl_->stream->avg_frame_rate = ctx->framerate;
    if (!(impl_->format->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&impl_->format->pb, impl_->path.c_str(), AVIO_FLAG_WRITE) < 0) {
            error = "cannot open output file";
            return false;
        }
    }
    AVDictionary* options = nullptr;
    av_dict_set(&options, "movflags", "+faststart", 0);
    const int headerResult = avformat_write_header(impl_->format, &options);
    av_dict_free(&options);
    if (headerResult < 0) {
        error = "cannot write MP4 header";
        return false;
    }
    impl_->frame = av_frame_alloc();
    impl_->packet = av_packet_alloc();
    if (!impl_->frame || !impl_->packet) { error = "cannot allocate frame or packet"; return false; }
    impl_->frame->format = ctx->pix_fmt;
    impl_->frame->width = impl_->width;
    impl_->frame->height = impl_->height;
    if (av_frame_get_buffer(impl_->frame, 32) < 0) { error = "cannot allocate frame buffer"; return false; }
    impl_->opened = true;
    return true;
}

bool Mp4Writer::write(const cv::Mat& bgr, std::string& error) {
    if (!impl_->opened || impl_->finished) { error = "writer is not open"; return false; }
    if (bgr.empty() || bgr.cols != impl_->width || bgr.rows != impl_->height) {
        error = "frame dimensions do not match the writer";
        return false;
    }
    if (av_frame_make_writable(impl_->frame) < 0) { error = "cannot make frame writable"; return false; }
    if (!impl_->sws) {
        impl_->sws = sws_getContext(impl_->width, impl_->height, AV_PIX_FMT_BGR24,
                                    impl_->width, impl_->height, impl_->encoder->pix_fmt,
                                    SWS_BILINEAR, nullptr, nullptr, nullptr);
    }
    const uint8_t* src[] = {bgr.data};
    int srcLine[] = {static_cast<int>(bgr.step)};
    sws_scale(impl_->sws, src, srcLine, 0, impl_->height,
              impl_->frame->data, impl_->frame->linesize);
    impl_->frame->pts = impl_->pts++;
    if (avcodec_send_frame(impl_->encoder, impl_->frame) < 0) { error = "cannot encode frame"; return false; }
    int rc = 0;
    while ((rc = avcodec_receive_packet(impl_->encoder, impl_->packet)) == 0) {
        av_packet_rescale_ts(impl_->packet, impl_->encoder->time_base,
                             impl_->stream->time_base);
        impl_->packet->duration = av_rescale_q(1, impl_->encoder->time_base,
                                               impl_->stream->time_base);
        impl_->packet->stream_index = impl_->stream->index;
        av_interleaved_write_frame(impl_->format, impl_->packet);
        av_packet_unref(impl_->packet);
    }
    return true;
}

void Mp4Writer::finish() {
    if (!impl_->opened || impl_->finished) return;
    impl_->finished = true;
    avcodec_send_frame(impl_->encoder, nullptr);
    int rc = 0;
    while ((rc = avcodec_receive_packet(impl_->encoder, impl_->packet)) == 0) {
        av_packet_rescale_ts(impl_->packet, impl_->encoder->time_base,
                             impl_->stream->time_base);
        impl_->packet->duration = av_rescale_q(1, impl_->encoder->time_base,
                                               impl_->stream->time_base);
        impl_->packet->stream_index = impl_->stream->index;
        av_interleaved_write_frame(impl_->format, impl_->packet);
        av_packet_unref(impl_->packet);
    }
    av_write_trailer(impl_->format);
    if (impl_->sws) sws_freeContext(impl_->sws);
    if (impl_->frame) av_frame_free(&impl_->frame);
    if (impl_->packet) av_packet_free(&impl_->packet);
    if (!(impl_->format->oformat->flags & AVFMT_NOFILE) && impl_->format->pb)
        avio_closep(&impl_->format->pb);
    avcodec_free_context(&impl_->encoder);
    avformat_free_context(impl_->format);
}

} // namespace ps::io

#else

namespace ps::io {

struct Mp4Writer::Impl {};

void Mp4Writer::setPreferHardwareEncoder(bool) {}

Mp4Writer::Mp4Writer(const std::string&, int, int, double) : impl_(new Impl()) {}
Mp4Writer::~Mp4Writer() { delete impl_; }
bool Mp4Writer::open(std::string& error) {
    error = "built without libav: MP4 output is disabled";
    return false;
}
bool Mp4Writer::write(const cv::Mat&, std::string& error) {
    error = "built without libav: MP4 output is disabled";
    return false;
}
void Mp4Writer::finish() {}

} // namespace ps::io

#endif
