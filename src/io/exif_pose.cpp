#include "io/exif_pose.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <vector>

namespace ps::io {
namespace {

uint16_t readU16(const std::vector<uint8_t>& d, size_t p, bool le) {
    if (p + 1 >= d.size()) return 0;
    return le ? static_cast<uint16_t>(d[p] | (d[p + 1] << 8))
              : static_cast<uint16_t>((d[p] << 8) | d[p + 1]);
}

uint32_t readU32(const std::vector<uint8_t>& d, size_t p, bool le) {
    if (p + 3 >= d.size()) return 0;
    if (le)
        return static_cast<uint32_t>(d[p]) |
               (static_cast<uint32_t>(d[p + 1]) << 8) |
               (static_cast<uint32_t>(d[p + 2]) << 16) |
               (static_cast<uint32_t>(d[p + 3]) << 24);
    return (static_cast<uint32_t>(d[p]) << 24) |
           (static_cast<uint32_t>(d[p + 1]) << 16) |
           (static_cast<uint32_t>(d[p + 2]) << 8) |
           static_cast<uint32_t>(d[p + 3]);
}

size_t typeSize(uint16_t type) {
    switch (type) {
        case 1: case 2: case 6: case 7: return 1;
        case 3: case 8: return 2;
        case 4: case 9: case 11: return 4;
        case 5: case 10: case 12: return 8;
        default: return 0;
    }
}

// Returns the UserComment text from an Exif TIFF block, or an empty string.
std::string findUserComment(const std::vector<uint8_t>& tiff, size_t base, bool le) {
    if (base + 8 > tiff.size() || readU16(tiff, base + 2, le) != 42) return {};
    const uint32_t ifd0 = readU32(tiff, base + 4, le);
    uint32_t exifIfd = 0;
    if (static_cast<size_t>(ifd0) + 2 <= tiff.size()) {
        const uint16_t count = readU16(tiff, base + ifd0, le);
        for (uint16_t i = 0; i < count; ++i) {
            const size_t e = base + ifd0 + 2 + static_cast<size_t>(i) * 12;
            if (e + 12 > tiff.size()) break;
            if (readU16(tiff, e, le) == 0x8769)
                exifIfd = readU32(tiff, e + 8, le);
        }
    }
    if (!exifIfd || static_cast<size_t>(exifIfd) + 2 > tiff.size()) return {};
    const uint16_t count = readU16(tiff, base + exifIfd, le);
    for (uint16_t i = 0; i < count; ++i) {
        const size_t e = base + exifIfd + 2 + static_cast<size_t>(i) * 12;
        if (e + 12 > tiff.size()) break;
        if (readU16(tiff, e, le) != 0x9286) continue;
        const uint16_t type = readU16(tiff, e + 2, le);
        const uint32_t n = readU32(tiff, e + 4, le);
        const size_t bytes = static_cast<size_t>(n) * typeSize(type);
        size_t start = e + 8;
        if (bytes > 4) start = base + readU32(tiff, e + 8, le);
        if (start + bytes > tiff.size()) return {};
        std::string s(reinterpret_cast<const char*>(tiff.data() + start), bytes);
        // Strip a charset prefix such as "ASCII\0\0\0" when present.
        if (s.size() > 8 && (s.rfind("ASCII", 0) == 0 || s.rfind("UNICODE", 0) == 0))
            s = s.substr(8);
        return s;
    }
    return {};
}

double valueAfter(const std::string& text, const std::string& key) {
    const size_t p = text.find(key + "=");
    if (p == std::string::npos) return 0.0;
    try {
        return std::stod(text.substr(p + key.size() + 1));
    } catch (...) {
        return 0.0;
    }
}

} // namespace

bool readExifPose(const std::string& path, ExifPose& out) {
    out = {};
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    if (data.size() < 4 || data[0] != 0xFF || data[1] != 0xD8) return false;
    size_t pos = 2;
    while (pos + 4 <= data.size()) {
        if (data[pos] != 0xFF) { ++pos; continue; }
        const uint8_t marker = data[pos + 1];
        if (marker == 0xD9 || marker == 0xDA) break;
        if (marker == 0xFF) { ++pos; continue; }
        const size_t length = (static_cast<size_t>(data[pos + 2]) << 8) | data[pos + 3];
        if (length < 2 || pos + 2 + length > data.size()) break;
        if (marker == 0xE1 && length > 8) {
            const size_t start = pos + 4;
            const std::string id(reinterpret_cast<const char*>(data.data() + start), 6);
            if (id == std::string("Exif\0\0", 6)) {
                const bool le = data[start + 6] == 'I' && data[start + 7] == 'I';
                const std::string comment = findUserComment(data, start + 6, le);
                if (comment.empty()) return false;
                const size_t iy = comment.find("yaw=");
                const size_t ip = comment.find("pitch=");
                if (iy == std::string::npos && ip == std::string::npos) return false;
                out.yawDegrees = valueAfter(comment, "yaw");
                out.pitchDegrees = valueAfter(comment, "pitch");
                out.rollDegrees = valueAfter(comment, "roll");
                out.horizontalFovDegrees = valueAfter(comment, "hfov");
                out.verticalFovDegrees = valueAfter(comment, "vfov");
                const size_t ii = comment.find("index=");
                if (ii != std::string::npos) {
                    try { out.index = std::stoi(comment.substr(ii + 6)); } catch (...) {}
                }
                const size_t im = comment.find("matrix=");
                if (im != std::string::npos) {
                    const std::string values = comment.substr(im + 7);
                    std::stringstream ss(values);
                    std::string token;
                    std::array<double, 9> parsed{};
                    int count = 0;
                    while (count < 9 && std::getline(ss, token, ',')) {
                        try { parsed[count++] = std::stod(token); } catch (...) { break; }
                    }
                    if (count == 9) { out.matrix = parsed; out.hasMatrix = true; }
                }
                out.valid = true;
                return true;
            }
        }
        pos += 2 + length;
    }
    return false;
}

} // namespace ps::io
