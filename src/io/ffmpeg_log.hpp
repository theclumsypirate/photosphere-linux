#pragma once

#include <functional>
#include <string>

namespace ps::io {

// ffmpeg writes its diagnostics to stderr by default. A registered sink also
// receives a copy of every line, for example for the GUI log panel. The
// terminal output is always kept.
using LogSink = std::function<void(const std::string&)>;

void setFFmpegLogSink(LogSink sink);

} // namespace ps::io
