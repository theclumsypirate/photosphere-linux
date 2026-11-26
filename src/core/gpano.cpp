#include "core/gpano.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <vector>

namespace ps {
namespace {

constexpr uint8_t kMarkerPrefix = 0xFF;
constexpr uint8_t kSoi = 0xD8;
constexpr uint8_t kApp0 = 0xE0;
constexpr uint8_t kApp1 = 0xE1;
constexpr uint8_t kApp15 = 0xEF;
constexpr uint8_t kCom = 0xFE;
const std::string kXmpSignature = "http://ns.adobe.com/xap/1.0/\0";
const std::string kToolkit = "PhotoSphere Linux";
const std::string kGpanoNs = "http://ns.google.com/photos/1.0/panorama/";
const std::string kRdfNs = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";

struct Segment {
    uint8_t marker = 0;
    std::vector<uint8_t> payload;
};

bool isXmp(const Segment& s) {
    return s.marker == kApp1 && s.payload.size() >= kXmpSignature.size() &&
           std::equal(kXmpSignature.begin(), kXmpSignature.end(), s.payload.begin());
}

} // namespace

GPanoMetadata GPanoMetadata::forFullPano(int width, int height) {
    GPanoMetadata m;
    m.fullWidth = width;
    m.fullHeight = height;
    m.croppedWidth = width;
    m.croppedHeight = height;
    m.croppedLeft = 0;
    m.croppedTop = 0;
    return m;
}

std::string buildXmpPacket(const GPanoMetadata& m) {
    std::string out;
    out += "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n";
    out += "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"" + kToolkit + "\">\n";
    out += "  <rdf:RDF xmlns:rdf=\"" + kRdfNs + "\">\n";
    out += "    <rdf:Description rdf:about=\"\"\n";
    out += "        xmlns:GPano=\"" + kGpanoNs + "\"\n";
    out += std::string("        GPano:UsePanoramaViewer=\"") +
           (m.usePanoramaViewer ? "True" : "False") + "\"\n";
    out += "        GPano:ProjectionType=\"" + m.projectionType + "\"\n";
    out += "        GPano:FullPanoWidthPixels=\"" + std::to_string(m.fullWidth) + "\"\n";
    out += "        GPano:FullPanoHeightPixels=\"" + std::to_string(m.fullHeight) + "\"\n";
    out += "        GPano:CroppedAreaImageWidthPixels=\"" + std::to_string(m.croppedWidth) + "\"\n";
    out += "        GPano:CroppedAreaImageHeightPixels=\"" + std::to_string(m.croppedHeight) + "\"\n";
    out += "        GPano:CroppedAreaLeftPixels=\"" + std::to_string(m.croppedLeft) + "\"\n";
    out += "        GPano:CroppedAreaTopPixels=\"" + std::to_string(m.croppedTop) + "\"/>\n";
    out += "  </rdf:RDF>\n";
    out += "</x:xmpmeta>\n";
    out += "<?xpacket end=\"r\"?>";
    return out;
}

bool injectGpano(const std::string& inputPath, const std::string& outputPath,
                 const GPanoMetadata& metadata, std::string& error) {
    std::ifstream in(inputPath, std::ios::binary);
    if (!in) { error = "cannot open " + inputPath; return false; }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    if (data.size() < 4 || data[0] != kMarkerPrefix || data[1] != kSoi) {
        error = "not a JPEG";
        return false;
    }
    const std::string packet = buildXmpPacket(metadata);
    std::vector<uint8_t> payload(kXmpSignature.begin(), kXmpSignature.end());
    payload.insert(payload.end(), packet.begin(), packet.end());
    if (payload.size() + 2 > 0xFFFF) { error = "XMP packet too large"; return false; }

    std::vector<Segment> leading;
    size_t pos = 2;
    uint8_t nextMarker = 0;
    while (pos + 4 <= data.size()) {
        if (data[pos] != kMarkerPrefix) { error = "malformed JPEG segment"; return false; }
        const uint8_t marker = data[pos + 1];
        if (marker == kMarkerPrefix) { ++pos; continue; }
        const size_t length = (static_cast<size_t>(data[pos + 2]) << 8) | data[pos + 3];
        if (length < 2 || pos + 2 + length > data.size()) {
            error = "truncated JPEG segment";
            return false;
        }
        const bool isMetadata = (marker >= kApp0 && marker <= kApp15) || marker == kCom;
        if (!isMetadata) { nextMarker = marker; break; }
        Segment seg;
        seg.marker = marker;
        seg.payload.assign(data.begin() + pos + 4, data.begin() + pos + 2 + length);
        leading.push_back(std::move(seg));
        pos += 2 + length;
    }
    if (nextMarker == 0) { error = "no image data found"; return false; }

    size_t insertAt = 0;
    for (size_t i = 0; i < leading.size(); ++i)
        if (leading[i].marker == kApp0 || leading[i].marker == kApp1) insertAt = i + 1;

    std::ofstream out(outputPath, std::ios::binary);
    if (!out) { error = "cannot write " + outputPath; return false; }
    out.write(reinterpret_cast<const char*>(data.data()), 2);
    auto writeSegment = [&out](uint8_t marker, const std::vector<uint8_t>& p) {
        const size_t length = p.size() + 2;
        const uint8_t header[] = {kMarkerPrefix, marker,
                                  static_cast<uint8_t>(length >> 8),
                                  static_cast<uint8_t>(length & 0xFF)};
        out.write(reinterpret_cast<const char*>(header), 4);
        out.write(reinterpret_cast<const char*>(p.data()), static_cast<std::streamsize>(p.size()));
    };
    for (size_t i = 0; i < leading.size(); ++i) {
        if (i == insertAt) writeSegment(kApp1, payload);
        if (isXmp(leading[i])) continue;
        writeSegment(leading[i].marker, leading[i].payload);
    }
    if (insertAt >= leading.size()) writeSegment(kApp1, payload);
    const uint8_t markerHeader[] = {kMarkerPrefix, nextMarker};
    out.write(reinterpret_cast<const char*>(markerHeader), 2);
    out.write(reinterpret_cast<const char*>(data.data() + pos),
              static_cast<std::streamsize>(data.size() - pos));
    return true;
}

} // namespace ps
