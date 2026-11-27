#include "io/video_writer.hpp"

#include "audio/ambisonics.hpp"

#ifdef PS_HAVE_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/spherical.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cstring>
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
    std::vector<double> audioPacketTimes;
    size_t audioIndex = 0;
    std::function<Mat3(double)> audioRotation;
    bool forceAmbisonic = false;
    bool audioPacketsEncoded = false;

    bool encodeRotatedAudio(std::string& error);
};

bool Mp4Writer::Impl::encodeRotatedAudio(std::string& error) {
    AVStream* input = audioFormat->streams[audioStreamIndex];
    if (!audioRotation) {
        error = "no ambisonic rotation callback";
        return false;
    }
    const AVCodec* decoder = avcodec_find_decoder(input->codecpar->codec_id);
    const AVCodec* encoderCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!decoder || !encoderCodec) {
        error = "no decoder or AAC encoder for ambisonic audio";
        return false;
    }
    AVCodecContext* dec = avcodec_alloc_context3(decoder);
    AVCodecContext* enc = avcodec_alloc_context3(encoderCodec);
    AVFrame* decFrame = av_frame_alloc();
    AVFrame* encFrame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    AVPacket* outPacket = av_packet_alloc();
    SwrContext* swr = nullptr;
    if (!dec || !enc || !decFrame || !encFrame || !packet || !outPacket) {
        error = "cannot allocate ambisonic audio contexts";
        return false;
    }
    bool ok = avcodec_parameters_to_context(dec, input->codecpar) >= 0 &&
              avcodec_open2(dec, decoder, nullptr) >= 0;
    if (!ok) { error = "cannot open ambisonic audio decoder"; return false; }

    enc->sample_fmt = AV_SAMPLE_FMT_FLTP;
    enc->sample_rate = dec->sample_rate;
    // AAC does not accept the ambisonic channel order. Encode the four ACN
    // channels with a plain layout and tag the output stream as ambisonics.
    enc->ch_layout = AV_CHANNEL_LAYOUT_QUAD;
    enc->bit_rate = 256000;
    enc->time_base = {1, std::max(1, dec->sample_rate)};
    if (avcodec_open2(enc, encoderCodec, nullptr) < 0) {
        error = "cannot open four-channel audio encoder";
        return false;
    }
    AVStream* output = avformat_new_stream(format, nullptr);
    if (!output || avcodec_parameters_from_context(output->codecpar, enc) < 0) {
        error = "cannot create rotated audio stream";
        return false;
    }
    output->codecpar->ch_layout = AV_CHANNEL_LAYOUT_AMBISONIC_FIRST_ORDER;
    output->codecpar->codec_tag = 0;
    output->time_base = enc->time_base;
    audioOutputStream = output;
    audioPacketsEncoded = true;

    const int64_t startSamples =
        static_cast<int64_t>(audioStart * static_cast<double>(enc->sample_rate));
    const double secondsPerTick = std::max(1e-9, av_q2d(input->time_base));
    auto emit = [&](AVFrame* audioFrame, double seconds) {
        AVFrame* source = audioFrame;
        float* planes[4] = {nullptr, nullptr, nullptr, nullptr};
        uint8_t** converted = nullptr;
        if (source->format != AV_SAMPLE_FMT_FLTP || source->ch_layout.nb_channels != 4) {
            if (!swr) {
                AVChannelLayout outLayout{AV_CHANNEL_ORDER_UNSPEC, 4, 0, nullptr};
                AVChannelLayout inLayout{AV_CHANNEL_ORDER_UNSPEC, 4, 0, nullptr};
                if (swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLTP,
                                        enc->sample_rate, &inLayout,
                                        static_cast<AVSampleFormat>(source->format),
                                        source->sample_rate, 0, nullptr) < 0 ||
                    swr_init(swr) < 0) {
                    return;
                }
            }
            if (av_samples_alloc_array_and_samples(&converted, nullptr, 4,
                                                   source->nb_samples,
                                                   AV_SAMPLE_FMT_FLTP, 0) < 0)
                return;
            const uint8_t* inData[4] = {source->data[0], source->data[1],
                                        source->data[2], source->data[3]};
            if (swr_convert(swr, converted, source->nb_samples, inData,
                            source->nb_samples) < 0) {
                av_freep(&converted[0]);
                return;
            }
            for (int i = 0; i < 4; ++i)
                planes[i] = reinterpret_cast<float*>(converted[i]);
        } else {
            for (int i = 0; i < 4; ++i)
                planes[i] = reinterpret_cast<float*>(source->data[i]);
        }
        if (seconds < audioStart || (audioEnd > 0 && seconds > audioEnd)) {
            if (converted && converted[0]) av_freep(&converted[0]);
            return;
        }
        audio::FoaMatrix matrix = audio::foaRotation(audioRotation(seconds));
        audio::rotateFoa(planes, source->nb_samples, matrix);
        av_frame_unref(encFrame);
        encFrame->format = AV_SAMPLE_FMT_FLTP;
        encFrame->sample_rate = enc->sample_rate;
        encFrame->ch_layout = enc->ch_layout;
        encFrame->nb_samples = source->nb_samples;
        encFrame->pts = static_cast<int64_t>(seconds * enc->sample_rate) - startSamples;
        if (av_frame_get_buffer(encFrame, 0) >= 0) {
            for (int i = 0; i < 4; ++i)
                std::memcpy(encFrame->data[i], planes[i],
                            static_cast<size_t>(source->nb_samples) * sizeof(float));
            avcodec_send_frame(enc, encFrame);
            int receive = 0;
            while ((receive = avcodec_receive_packet(enc, outPacket)) == 0) {
                av_packet_rescale_ts(outPacket, enc->time_base, output->time_base);
                AVPacket* copy = av_packet_alloc();
                if (copy && av_packet_ref(copy, outPacket) >= 0) {
                    audioPackets.push_back(copy);
                    audioPacketTimes.push_back(seconds - audioStart);
                } else {
                    av_packet_free(&copy);
                }
                av_packet_unref(outPacket);
            }
        }
        if (converted && converted[0]) av_freep(&converted[0]);
    };

    av_seek_frame(audioFormat, audioStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
    while (av_read_frame(audioFormat, packet) >= 0) {
        if (packet->stream_index == audioStreamIndex) {
            const int64_t pts = packet->pts != AV_NOPTS_VALUE ? packet->pts
                                                              : packet->dts;
            const double packetSeconds = pts != AV_NOPTS_VALUE
                                             ? static_cast<double>(pts) * secondsPerTick
                                             : 0.0;
            avcodec_send_packet(dec, packet);
            int64_t consumed = 0;
            int receive = 0;
            while ((receive = avcodec_receive_frame(dec, decFrame)) == 0) {
                const double seconds = packetSeconds +
                    static_cast<double>(consumed) / std::max(1, dec->sample_rate);
                consumed += decFrame->nb_samples;
                emit(decFrame, seconds);
            }
        }
        av_packet_unref(packet);
    }
    avcodec_send_packet(dec, nullptr);
    int receive = 0;
    double tailSeconds = audioPackets.empty() ? 0.0
                                              : audioPacketTimes.back() + audioStart;
    while ((receive = avcodec_receive_frame(dec, decFrame)) == 0) {
        tailSeconds += static_cast<double>(decFrame->nb_samples) /
                       std::max(1, dec->sample_rate);
        emit(decFrame, tailSeconds);
    }
    avcodec_send_frame(enc, nullptr);
    while ((receive = avcodec_receive_packet(enc, outPacket)) == 0) {
        av_packet_rescale_ts(outPacket, enc->time_base, output->time_base);
        AVPacket* copy = av_packet_alloc();
        if (copy && av_packet_ref(copy, outPacket) >= 0) {
            audioPackets.push_back(copy);
            audioPacketTimes.push_back(audioPackets.empty() ? 0.0 : audioPacketTimes.back());
        } else {
            av_packet_free(&copy);
        }
        av_packet_unref(outPacket);
    }

    if (swr) swr_free(&swr);
    av_packet_free(&packet);
    av_packet_free(&outPacket);
    av_frame_free(&decFrame);
    av_frame_free(&encFrame);
    avcodec_free_context(&dec);
    avcodec_free_context(&enc);
    return true;
}

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

void Mp4Writer::setAmbisonicRotation(std::function<Mat3(double)> rotationAt) {
    impl_->audioRotation = std::move(rotationAt);
}

void Mp4Writer::setForceAmbisonic(bool enabled) {
    impl_->forceAmbisonic = enabled;
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
            impl_->audioStreamIndex = static_cast<int>(i);
            bool ambisonicLayout = input->codecpar->ch_layout.order ==
                                   AV_CHANNEL_ORDER_AMBISONIC;
            if (!ambisonicLayout && input->codecpar->ch_layout.order ==
                                        AV_CHANNEL_ORDER_CUSTOM) {
                for (int c = 0; c < 4; ++c) {
                    const int id = input->codecpar->ch_layout.u.map[c].id;
                    if (id >= AV_CHAN_AMBISONIC_BASE && id <= AV_CHAN_AMBISONIC_END)
                        ambisonicLayout = true;
                }
            }
            const bool firstOrder = input->codecpar->ch_layout.nb_channels == 4 &&
                                    (ambisonicLayout || impl_->forceAmbisonic);
            if (firstOrder && impl_->audioRotation &&
                impl_->encodeRotatedAudio(error)) {
                break;
            }
            impl_->audioOutputStream = nullptr;
            impl_->audioPacketsEncoded = false;
            AVStream* output = avformat_new_stream(impl_->format, nullptr);
            if (!output || avcodec_parameters_copy(output->codecpar,
                                                   input->codecpar) < 0) break;
            output->codecpar->codec_tag = 0;
            output->time_base = input->time_base;
            impl_->audioOutputStream = output;
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
                            if (copy && av_packet_ref(copy, packet) >= 0) {
                                impl_->audioPackets.push_back(copy);
                                impl_->audioPacketTimes.push_back(seconds - impl_->audioStart);
                            } else {
                                av_packet_free(&copy);
                            }
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
    const int64_t startTicks = static_cast<int64_t>(
        impl_->audioStart / std::max(1e-9, av_q2d(inTimeBase)));
    while (impl_->audioIndex < impl_->audioPackets.size()) {
        AVPacket* packet = impl_->audioPackets[impl_->audioIndex];
        const double seconds = impl_->audioIndex < impl_->audioPacketTimes.size()
                                   ? impl_->audioPacketTimes[impl_->audioIndex]
                                   : 0.0;
        if (!all && seconds > uptoSeconds) break;
        if (!impl_->audioPacketsEncoded) {
            if (packet->pts != AV_NOPTS_VALUE)
                packet->pts = av_rescale_q(packet->pts - startTicks, inTimeBase,
                                           output->time_base);
            if (packet->dts != AV_NOPTS_VALUE)
                packet->dts = av_rescale_q(packet->dts - startTicks, inTimeBase,
                                           output->time_base);
            packet->duration = av_rescale_q(packet->duration, inTimeBase,
                                            output->time_base);
        }
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
    impl_->audioPacketTimes.clear();
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
void Mp4Writer::setAmbisonicRotation(std::function<Mat3(double)>) {}
void Mp4Writer::setForceAmbisonic(bool) {}
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
