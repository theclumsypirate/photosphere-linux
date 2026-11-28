#include "io/ffmpeg_log.hpp"

#include <mutex>
#include <utility>

#ifdef PS_HAVE_FFMPEG
extern "C" {
#include <libavutil/log.h>
}
#endif

namespace ps::io {

namespace {

#ifdef PS_HAVE_FFMPEG
std::mutex sinkMutex;
LogSink currentSink;

void logCallback(void* avcl, int level, const char* format, va_list args) {
    // av_log() calls a custom callback for every level, including debug and
    // trace. The default callback normally filters those out.
    if (level > av_log_get_level()) return;

    LogSink sink;
    {
        std::lock_guard<std::mutex> lock(sinkMutex);
        sink = currentSink;
    }

    char line[1024] = {0};
    if (sink) {
        // Copy the arguments before the default callback consumes them.
        va_list copy;
        va_copy(copy, args);
        int printPrefix = 1;
        av_log_format_line2(avcl, level, format, copy, line, sizeof(line),
                            &printPrefix);
        va_end(copy);
    }
    // Keep the normal stderr output for terminal users.
    av_log_default_callback(avcl, level, format, args);
    if (sink) sink(line);
}
#endif

} // namespace

void setFFmpegLogSink(LogSink sink) {
#ifdef PS_HAVE_FFMPEG
    static bool installed = false;
    if (!installed) {
        av_log_set_callback(logCallback);
        installed = true;
    }
    std::lock_guard<std::mutex> lock(sinkMutex);
    currentSink = std::move(sink);
#else
    (void)sink;
#endif
}

} // namespace ps::io
