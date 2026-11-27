#include "io/insv_telemetry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string_view>

namespace ps::io {
namespace {

constexpr size_t kFooterSize = 72;
constexpr size_t kHeaderSize = 32 + 4 + 4 + 32;
constexpr const char* kMagic = "8db42d694ccc418790edff439fe026bf";

uint16_t u16(const std::vector<uint8_t>& d, size_t p) {
    if (p + 1 >= d.size()) return 0;
    return static_cast<uint16_t>(d[p]) |
           (static_cast<uint16_t>(d[p + 1]) << 8);
}

uint32_t u32(const std::vector<uint8_t>& d, size_t p) {
    if (p + 3 >= d.size()) return 0;
    return static_cast<uint32_t>(d[p]) |
           (static_cast<uint32_t>(d[p + 1]) << 8) |
           (static_cast<uint32_t>(d[p + 2]) << 16) |
           (static_cast<uint32_t>(d[p + 3]) << 24);
}

uint64_t u64(const std::vector<uint8_t>& d, size_t p) {
    if (p + 7 >= d.size()) return 0;
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | d[p + static_cast<size_t>(i)];
    return v;
}

double f64(const std::vector<uint8_t>& d, size_t p) {
    const uint64_t bits = u64(d, p);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::string text(const std::vector<uint8_t>& d, size_t p, size_t n) {
    if (p >= d.size()) return {};
    n = std::min(n, d.size() - p);
    return std::string(reinterpret_cast<const char*>(d.data() + p), n);
}

struct VarintResult {
    uint64_t value = 0;
    size_t next = 0;
    bool ok = false;
};

VarintResult readVarint(const std::vector<uint8_t>& d, size_t p) {
    VarintResult out;
    out.next = p;
    uint64_t value = 0;
    int shift = 0;
    while (out.next < d.size() && shift < 64) {
        const uint8_t byte = d[out.next++];
        value |= static_cast<uint64_t>(byte & 0x7f) << shift;
        if (byte < 0x80) {
            out.value = value;
            out.ok = true;
            return out;
        }
        shift += 7;
    }
    return out;
}

struct ProtoValue {
    int wire = 0;
    uint64_t integer = 0;
    double real = 0.0;
    std::string bytes;
};

// Reads top-level protobuf fields in [begin, end). Length-delimited values are
// kept as bytes so nested messages can be decoded separately.
std::map<int, ProtoValue> protoFields(const std::vector<uint8_t>& d,
                                      size_t begin, size_t end) {
    std::map<int, ProtoValue> out;
    size_t p = begin;
    while (p < end) {
        const auto tag = readVarint(d, p);
        if (!tag.ok) break;
        p = tag.next;
        const int field = static_cast<int>(tag.value >> 3);
        const int wire = static_cast<int>(tag.value & 7);
        if (field == 0) break;
        ProtoValue value;
        value.wire = wire;
        if (wire == 0) {
            const auto var = readVarint(d, p);
            if (!var.ok) break;
            p = var.next;
            value.integer = var.value;
        } else if (wire == 1) {
            if (p + 8 > end) break;
            value.real = f64(d, p);
            p += 8;
        } else if (wire == 2) {
            const auto length = readVarint(d, p);
            if (!length.ok || length.value > end - p) break;
            p = length.next;
            value.bytes = text(d, p, static_cast<size_t>(length.value));
            p += static_cast<size_t>(length.value);
        } else if (wire == 5) {
            if (p + 4 > end) break;
            value.integer = u32(d, p);
            p += 4;
        } else {
            break;
        }
        out[field] = value;
    }
    return out;
}

std::vector<std::string> splitUnderscore(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        const size_t pos = s.find('_', start);
        out.push_back(s.substr(start, pos == std::string::npos ? std::string::npos
                                                              : pos - start));
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return out;
}

bool parseOffsetYpr(const std::string& s, double& yaw, double& pitch,
                    double& roll) {
    if (s.size() < 4) return false;
    const auto tokens = splitUnderscore(s);
    if (tokens.size() < 4) return false;
    long long flags = 0;
    try {
        flags = std::stoll(tokens.back());
    } catch (...) {
        return false;
    }
    if (flags < 0) return false;
    const int version = static_cast<int>(flags >> 16);
    int perLens = 0;
    int yawIndex = 0;
    switch (version) {
        case 1: case 2: perLens = version == 1 ? 6 : 16; yawIndex = 3; break;
        case 3: perLens = 19; yawIndex = 5; break;
        case 6: perLens = 27; yawIndex = 5; break;
        default: return false;
    }
    int lenses = 0;
    try {
        lenses = std::stoi(tokens.front());
    } catch (...) {
        return false;
    }
    if (lenses < 1) return false;
    const size_t expected = static_cast<size_t>(lenses) * perLens +
                            (version == 1 ? 4 : 2);
    if (tokens.size() != expected) return false;
    try {
        yaw = std::stod(tokens[1 + yawIndex]);
        pitch = std::stod(tokens[2 + yawIndex]);
        roll = std::stod(tokens[3 + yawIndex]);
    } catch (...) {
        return false;
    }
    return std::isfinite(yaw) && std::isfinite(pitch) && std::isfinite(roll);
}

// The IMU axes are not the lens axes. The camera's calibration string carries
// the rotation that maps measurements into lens 0. This is the vendor's own
// Euler composition, kept verbatim so existing clips keep their orientation.
std::array<double, 9> imuToLens(double yawDeg, double pitchDeg,
                                double rollDeg) {
    constexpr double kDeg = 3.14159265358979323846 / 180.0;
    const double yaw = yawDeg * kDeg;
    const double pitch = pitchDeg * kDeg;
    const double roll = rollDeg * kDeg;
    const double sr = std::sin(yaw), cr = std::cos(yaw);
    const double sp = std::sin(pitch), cp = std::cos(pitch);
    const double sy = std::sin(roll), cy = std::cos(roll);
    return {cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr,
            sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr,
            -sp, cp * sr, cp * cr};
}

void rotateVector(const std::array<double, 9>& m, double v[3]) {
    const double x = v[0], y = v[1], z = v[2];
    for (int i = 0; i < 3; ++i)
        v[i] = m[static_cast<size_t>(i * 3)] * x +
               m[static_cast<size_t>(i * 3 + 1)] * y +
               m[static_cast<size_t>(i * 3 + 2)] * z;
}

// The vendor matrix puts gravity along -x of lens 0. Equirectangular
// directions in this project use +z up, so gravity must end up along -z.
// This is a 90 degree turn about y: lens +x becomes world +z.
constexpr std::array<double, 9> kLensToWorld{
    0.0, 0.0, -1.0,
    0.0, 1.0, 0.0,
    1.0, 0.0, 0.0};

struct Record {
    uint8_t id = 0;
    uint8_t format = 0;
    size_t offset = 0;
    size_t size = 0;
};

bool findRecords(const std::vector<uint8_t>& trailer,
                 std::map<uint8_t, Record>& out) {
    const size_t size = trailer.size();
    if (size < kHeaderSize || std::memcmp(trailer.data() + size - 32, kMagic, 32) != 0)
        return false;
    size_t off = kHeaderSize + 4 + 1 + 1;
    for (int count = 0; off + 6 <= size && count < 256; ++count) {
        const size_t descriptor = size - off;
        const uint8_t format = trailer[descriptor];
        const uint8_t id = trailer[descriptor + 1];
        const size_t length = u32(trailer, descriptor + 2);
        if (length > size || off + length > size) break;
        if (id == 0 && length >= 10) {
            for (size_t p = size - off - length; p + 10 <= size - off; p += 10) {
                const uint8_t recordId = trailer[p];
                if (recordId == 0) continue;
                out[recordId] = Record{recordId, trailer[p + 1],
                                       u32(trailer, p + 6), u32(trailer, p + 2)};
            }
            return !out.empty();
        }
        out[id] = Record{id, format, size - off - length, length};
        off += length + 6;
    }
    return !out.empty();
}

void parseMetadata(const std::vector<uint8_t>& trailer, const Record& record,
                   InsvTelemetry& out) {
    if (record.offset + record.size > trailer.size()) return;
    const auto fields = protoFields(trailer, record.offset,
                                    record.offset + record.size);
    for (const auto& [field, value] : fields) {
        switch (field) {
            case 2:
                if (value.wire == 2) out.cameraModel = value.bytes;
                break;
            case 20:
                if (value.wire == 0) out.fps = static_cast<double>(value.integer);
                break;
            case 24:
                if (value.wire == 0)
                    out.firstFrameTimestampUs = static_cast<double>(value.integer);
                break;
            case 25:
                if (value.wire == 1) out.rollingShutterMs = value.real;
                break;
            case 28:
                if (value.wire == 1) out.gyroTimestampMs = value.real;
                break;
            case 62:
                if (value.wire == 0) out.raw = value.integer != 0;
                break;
            case 65: {
                if (value.wire != 2) break;
                const auto parsed = protoFields(
                    std::vector<uint8_t>(value.bytes.begin(), value.bytes.end()),
                    0, value.bytes.size());
                for (const auto& [nested, nestedValue] : parsed) {
                    if (nestedValue.wire != 0) continue;
                    if (nested == 1) out.accRangeG = static_cast<double>(nestedValue.integer);
                    if (nested == 2) out.gyroRangeDps = static_cast<double>(nestedValue.integer);
                }
                break;
            }
            default:
                break;
        }
    }
    // Newer strings describe the lenses better. The orientation is carried by
    // every generation, so the first one present is enough here.
    for (const int field : {111, 54, 53, 5}) {
        const auto it = fields.find(field);
        if (it == fields.end() || it->second.wire != 2) continue;
        if (parseOffsetYpr(it->second.bytes, out.lensYawDeg, out.lensPitchDeg,
                           out.lensRollDeg)) {
            out.hasLensPose = true;
            break;
        }
    }
}

void parseGyro(const std::vector<uint8_t>& trailer, const Record& record,
               const InsvTelemetry& meta, std::vector<ImuSample>& out) {
    if (record.offset + record.size > trailer.size()) return;
    const size_t itemSize = meta.raw ? 20 : 56;
    if (itemSize == 0 || record.size < itemSize) return;
    const auto matrix = imuToLens(meta.lensYawDeg, meta.lensPitchDeg,
                                  meta.lensRollDeg);
    constexpr double kPi = 3.14159265358979323846;
    const double gyroScale = meta.raw ? meta.gyroRangeDps / 32768.0 * kPi / 180.0 : 1.0;
    const double accScale = meta.raw ? meta.accRangeG / 32768.0 : 1.0;
    out.reserve(record.size / itemSize);
    for (size_t p = record.offset; p + itemSize <= record.offset + record.size;
         p += itemSize) {
        ImuSample sample;
        sample.timestampUs = static_cast<double>(u64(trailer, p));
        if (meta.raw) {
            for (int i = 0; i < 3; ++i) {
                sample.acc[i] = (static_cast<double>(u16(trailer, p + 8 + i * 2)) - 32768.0) * accScale;
                sample.gyro[i] = (static_cast<double>(u16(trailer, p + 14 + i * 2)) - 32768.0) * gyroScale;
            }
        } else {
            for (int i = 0; i < 3; ++i) {
                sample.acc[i] = f64(trailer, p + 8 + i * 8);
                sample.gyro[i] = f64(trailer, p + 32 + i * 8);
            }
        }
        rotateVector(matrix, sample.acc);
        rotateVector(matrix, sample.gyro);
        rotateVector(kLensToWorld, sample.acc);
        rotateVector(kLensToWorld, sample.gyro);
        out.push_back(sample);
    }
}

} // namespace

double InsvTelemetry::secondsAt(size_t index) const {
    if (samples.empty() || index >= samples.size()) return 0.0;
    return (samples[index].timestampUs - samples.front().timestampUs) / 1e6;
}

bool readInsvTelemetry(const std::string& path, InsvTelemetry& out,
                       std::string& error) {
    out = {};
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff fileSize = in.tellg();
    if (fileSize < static_cast<std::streamoff>(kFooterSize)) {
        error = "file is too small to hold an INSV trailer";
        return false;
    }
    std::array<uint8_t, kFooterSize> footer{};
    in.seekg(fileSize - static_cast<std::streamoff>(kFooterSize));
    in.read(reinterpret_cast<char*>(footer.data()), kFooterSize);
    if (std::memcmp(footer.data() + 40, kMagic, 32) != 0) {
        error = "no Insta360 telemetry trailer found";
        return false;
    }
    const uint32_t trailerSize = static_cast<uint32_t>(footer[32]) |
        (static_cast<uint32_t>(footer[33]) << 8) |
        (static_cast<uint32_t>(footer[34]) << 16) |
        (static_cast<uint32_t>(footer[35]) << 24);
    if (trailerSize < kHeaderSize || trailerSize > static_cast<uint64_t>(fileSize)) {
        error = "invalid telemetry trailer size";
        return false;
    }
    std::vector<uint8_t> trailer(trailerSize);
    in.seekg(fileSize - static_cast<std::streamoff>(trailerSize));
    in.read(reinterpret_cast<char*>(trailer.data()), trailerSize);

    std::map<uint8_t, Record> records;
    if (!findRecords(trailer, records)) {
        error = "no records found in telemetry trailer";
        return false;
    }
    const auto metadata = records.find(1);
    if (metadata == records.end()) {
        error = "no metadata record in telemetry trailer";
        return false;
    }
    parseMetadata(trailer, metadata->second, out);
    const auto gyro = records.find(3);
    if (gyro == records.end()) {
        error = "no gyroscope record in telemetry trailer";
        return false;
    }
    parseGyro(trailer, gyro->second, out, out.samples);
    if (out.samples.empty()) {
        error = "gyroscope record holds no samples";
        return false;
    }
    out.videoOffsetSeconds =
        (out.firstFrameTimestampUs - out.samples.front().timestampUs) / 1e6;
    return true;
}

} // namespace ps::io
