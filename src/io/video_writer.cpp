#include "io/video_writer.hpp"

#ifdef PS_HAVE_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/spherical.h>
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
    double crf = 18.0;
    int64_t bitrate = 0;
    bool spherical = true;
    bool sphericalWritten = false;
    bool hevc = false;
    bool opened = false;
    bool finished = false;

    std::string audioPath;
    double audioStart = 0.0;
    double audioEnd = 0.0;
    bool hasAudioSource = false;
    AVFormatContext* audioFormat = nullptr;
    AVStream* audioOutputStream = nullptr;
    int audioStreamIndex = -1;
    std::vector<AVPacket*> audioPackets;
    size_t audioIndex = 0;
};

void Mp4Writer::setPreferHardwareEncoder(bool enabled) {
    g_preferHardwareEncoder = enabled;
}

void Mp4Writer::setVideoQuality(double crf, int64_t bitrate) {
    impl_->crf = std::clamp(crf, 0.0, 51.0);
    impl_->bitrate = bitrate;
}

void Mp4Writer::setSphericalMetadata(bool enabled) { impl_->spherical = enabled; }

void Mp4Writer::setHevc(bool enabled) { impl_->hevc = enabled; }

void Mp4Writer::setAudioSource(const std::string& inputPath, double startTime,
                               double endTime) {
    impl_->audioPath = inputPath;
    impl_->audioStart = std::max(0.0, startTime);
    impl_->audioEnd = endTime;
    impl_->hasAudioSource = !inputPath.empty();
}

Mp4Writer::Mp4Writer(const std::string& path, int width, int height, double fps)
    : impl_(new Impl{path, width, height, fps, nullptr, nullptr, nullptr, nullptr,
                     nullptr, nullptr, 0, 18.0, 0, true, false, false, false, false}) {}

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
    // The muxer writes the sv3d box only when unofficial ISO extensions are
    // allowed. Spatial Media metadata is such an extension.
    impl_->format->strict_std_compliance = -1;
    const AVCodecID preferredId = impl_->hevc ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
    std::vector<const AVCodec*> candidates;
    if (g_preferHardwareEncoder) {
        const char* names[] = {impl_->hevc ? "hevc_nvenc" : "h264_nvenc",
                               impl_->hevc ? "hevc_qsv" : "h264_qsv"};
        for (const char* name : names)
            if (const AVCodec* hardware = avcodec_find_encoder_by_name(name))
                candidates.push_back(hardware);
    }
    if (const AVCodec* software = avcodec_find_encoder(preferredId))
        candidates.push_back(software);
    if (!impl_->hevc)
        if (const AVCodec* mpeg4 = avcodec_find_encoder(AV_CODEC_ID_MPEG4))
            candidates.push_back(mpeg4);
    if (candidates.empty()) { error = "no available video encoder"; return false; }

    std::string openError;
    for (const AVCodec* candidate : candidates) {
        AVCodecContext* context = avcodec_alloc_context3(candidate);
        if (!context) continue;
        context->codec_id = candidate->id;
        context->bit_rate = impl_->bitrate;
        context->width = impl_->width;
        context->height = impl_->height;
        const AVRational rate = av_d2q(impl_->fps, 1000);
        context->framerate = rate;
        context->time_base = av_inv_q(rate);
        context->gop_size = 25;
        context->max_b_frames = 0;
        context->pix_fmt = AV_PIX_FMT_YUV420P;
        if (candidate->pix_fmts) {
            bool supported = false;
            for (const AVPixelFormat* p = candidate->pix_fmts; *p != AV_PIX_FMT_NONE; ++p)
                if (*p == context->pix_fmt) supported = true;
            if (!supported) context->pix_fmt = candidate->pix_fmts[0];
        }
        AVDictionary* options = nullptr;
        if (impl_->bitrate <= 0)
            av_dict_set(&options, "crf", std::to_string(impl_->crf).c_str(), 0);
        const int opened = avcodec_open2(context, candidate, &options);
        av_dict_free(&options);
        if (opened >= 0) { impl_->encoder = context; break; }
        openError = "cannot open video encoder";
        avcodec_free_context(&context);
    }
    if (!impl_->encoder) { error = openError.empty() ? "cannot open video encoder" : openError; return false; }
    auto* ctx = impl_->encoder;
    impl_->stream = avformat_new_stream(impl_->format, nullptr);
    if (!impl_->stream) { error = "cannot create video stream"; return false; }
    if (avcodec_parameters_from_context(impl_->stream->codecpar, ctx) < 0) {
        error = "cannot copy encoder parameters";
        return false;
    }
    impl_->stream->time_base = ctx->time_base;
    impl_->stream->avg_frame_rate = ctx->framerate;
    if (impl_->spherical) {
        auto* mapping = static_cast<AVSphericalMapping*>(
            av_mallocz(sizeof(AVSphericalMapping)));
        if (mapping) {
            mapping->projection = AV_SPHERICAL_EQUIRECTANGULAR;
            av_packet_side_data_add(&impl_->stream->codecpar->coded_side_data,
                                    &impl_->stream->codecpar->nb_coded_side_data,
                                    AV_PKT_DATA_SPHERICAL,
                                    reinterpret_cast<uint8_t*>(mapping),
                                    sizeof(AVSphericalMapping), 0);
        }
    }
    // Copy the source audio track. Packets are buffered so we can trim the
    // selected time range and interleave them with the encoded video frames.
    if (impl_->hasAudioSource &&
        avformat_open_input(&impl_->audioFormat, impl_->audioPath.c_str(),
                            nullptr, nullptr) >= 0 &&
        avformat_find_stream_info(impl_->audioFormat, nullptr) >= 0) {
        for (unsigned i = 0; i < impl_->audioFormat->nb_streams; ++i) {
            AVStream* input = impl_->audioFormat->streams[i];
            if (input->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) continue;
            AVStream* output = avformat_new_stream(impl_->format, nullptr);
            if (!output || avcodec_parameters_copy(output->codecpar,
                                                   input->codecpar) < 0) break;
            output->codecpar->codec_tag = 0;
            output->time_base = input->time_base;
            impl_->audioOutputStream = output;
            impl_->audioStreamIndex = static_cast<int>(i);
            const double timeBase = av_q2d(input->time_base);
            AVPacket* packet = av_packet_alloc();
            if (packet) {
                while (av_read_frame(impl_->audioFormat, packet) >= 0) {
                    if (packet->stream_index == static_cast<int>(i)) {
                        const int64_t timestamp = packet->pts != AV_NOPTS_VALUE
                                                      ? packet->pts
                                                      : packet->dts;
                        const double seconds = timestamp * timeBase;
                        if (seconds >= impl_->audioStart &&
                            (impl_->audioEnd <= 0 || seconds <= impl_->audioEnd)) {
                            AVPacket* copy = av_packet_alloc();
                            if (copy && av_packet_ref(copy, packet) >= 0)
                                impl_->audioPackets.push_back(copy);
                            else
                                av_packet_free(&copy);
                        }
                    }
                    av_packet_unref(packet);
                }
                av_packet_free(&packet);
            }
            break;
        }
    }

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

void Mp4Writer::pumpAudio(double uptoSeconds, bool all) {
    if (!impl_->audioOutputStream || impl_->audioStreamIndex < 0) return;
    AVStream* input = impl_->audioFormat->streams[impl_->audioStreamIndex];
    AVStream* output = impl_->audioOutputStream;
    const AVRational inTimeBase = input->time_base;
    const double inSecondsPerTick = std::max(1e-9, av_q2d(inTimeBase));
    const int64_t startTicks = static_cast<int64_t>(impl_->audioStart / inSecondsPerTick);
    while (impl_->audioIndex < impl_->audioPackets.size()) {
        AVPacket* packet = impl_->audioPackets[impl_->audioIndex];
        const int64_t timestamp = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
        const double seconds = timestamp * inSecondsPerTick;
        if (!all && seconds > uptoSeconds) break;
        if (packet->pts != AV_NOPTS_VALUE)
            packet->pts = av_rescale_q(packet->pts - startTicks, inTimeBase,
                                       output->time_base);
        if (packet->dts != AV_NOPTS_VALUE)
            packet->dts = av_rescale_q(packet->dts - startTicks, inTimeBase,
                                       output->time_base);
        packet->duration = av_rescale_q(packet->duration, inTimeBase, output->time_base);
        packet->stream_index = output->index;
        av_interleaved_write_frame(impl_->format, packet);
        av_packet_unref(packet);
        ++impl_->audioIndex;
    }
}

bool Mp4Writer::write(const cv::Mat& bgr, std::string& error) {
    if (!impl_->opened || impl_->finished) { error = "writer is not open"; return false; }
    if (bgr.empty() || bgr.cols != impl_->width || bgr.rows != impl_->height) {
        error = "frame dimensions do not match the writer";
        return false;
    }
    pumpAudio(static_cast<double>(impl_->pts) / impl_->fps, false);
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
        if (impl_->spherical && !impl_->sphericalWritten) {
            auto* mapping = static_cast<AVSphericalMapping*>(
                av_mallocz(sizeof(AVSphericalMapping)));
            if (mapping) {
                mapping->projection = AV_SPHERICAL_EQUIRECTANGULAR;
                av_packet_add_side_data(impl_->packet, AV_PKT_DATA_SPHERICAL,
                                        reinterpret_cast<uint8_t*>(mapping),
                                        sizeof(AVSphericalMapping));
                impl_->sphericalWritten = true;
            }
        }
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
    pumpAudio(0, true);
    av_write_trailer(impl_->format);
    for (AVPacket* packet : impl_->audioPackets) av_packet_free(&packet);
    impl_->audioPackets.clear();
    if (impl_->audioFormat) avformat_close_input(&impl_->audioFormat);
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
void Mp4Writer::setVideoQuality(double, int64_t) {}
void Mp4Writer::setSphericalMetadata(bool) {}
void Mp4Writer::setHevc(bool) {}
void Mp4Writer::setAudioSource(const std::string&, double, double) {}
void Mp4Writer::pumpAudio(double, bool) {}

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
