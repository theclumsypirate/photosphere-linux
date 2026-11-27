#include "io/image_io.hpp"

#include "io/exif_pose.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <vector>

#ifdef PS_HAVE_LIBRAW
#include <libraw/libraw.h>
#endif

namespace ps::io {
namespace {

RawOptions g_rawOptions;

bool isDngName(const std::string& path) {
    auto pos = path.find_last_of('.');
    if (pos == std::string::npos) return false;
    std::string ext = path.substr(pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == "dng";
}

// Minimal TIFF reader for uncompressed single strip CFA DNG files, used only
// when LibRaw is not available. It does not apply DNG opcodes or colour
// matrices; LibRaw remains the preferred path.
struct RawInfo {
    int width = 0;
    int height = 0;
    int bits = 0;
    uint64_t stripOffset = 0;
    uint64_t stripBytes = 0;
    int cfa[4] = {0, 1, 1, 2}; // R G / G B
    double black = 0.0;
    double white = 65535.0;
    std::vector<double> colorMatrix; // DNG ColorMatrix1, camera RGB to XYZ
    std::vector<double> asShotNeutral;
};

struct TiffEntry {
    uint16_t type = 0;
    uint32_t count = 0;
    uint32_t valueOffset = 0;
    std::vector<uint8_t> bytes;
};

uint16_t readU16(const std::vector<uint8_t>& d, size_t p, bool le) {
    if (p + 1 >= d.size()) return 0;
    if (le) return static_cast<uint16_t>(d[p] | (d[p + 1] << 8));
    return static_cast<uint16_t>((d[p] << 8) | d[p + 1]);
}

uint32_t readU32(const std::vector<uint8_t>& d, size_t p, bool le) {
    if (p + 3 >= d.size()) return 0;
    if (le) {
        return static_cast<uint32_t>(d[p]) |
               (static_cast<uint32_t>(d[p + 1]) << 8) |
               (static_cast<uint32_t>(d[p + 2]) << 16) |
               (static_cast<uint32_t>(d[p + 3]) << 24);
    }
    return (static_cast<uint32_t>(d[p]) << 24) |
           (static_cast<uint32_t>(d[p + 1]) << 16) |
           (static_cast<uint32_t>(d[p + 2]) << 8) |
           static_cast<uint32_t>(d[p + 3]);
}

size_t typeSize(uint16_t t) {
    switch (t) {
        case 1: case 2: case 6: case 7: return 1;
        case 3: case 8: return 2;
        case 4: case 9: case 11: return 4;
        case 5: case 10: case 12: return 8;
        default: return 0;
    }
}

std::vector<double> numericValues(const TiffEntry& e, bool le) {
    std::vector<double> out;
    const size_t ts = typeSize(e.type);
    if (ts == 0 || e.bytes.size() < ts * e.count) return out;
    out.reserve(e.count);
    for (uint32_t i = 0; i < e.count; ++i) {
        const size_t p = static_cast<size_t>(i) * ts;
        double v = 0.0;
        switch (e.type) {
            case 1: case 7: v = e.bytes[p]; break;
            case 3: v = readU16(e.bytes, p, le); break;
            case 4: v = readU32(e.bytes, p, le); break;
            case 6: v = static_cast<int8_t>(e.bytes[p]); break;
            case 8: v = static_cast<int16_t>(readU16(e.bytes, p, le)); break;
            case 9: v = static_cast<int32_t>(readU32(e.bytes, p, le)); break;
            case 11: {
                uint32_t u = readU32(e.bytes, p, le);
                float f;
                std::memcpy(&f, &u, sizeof(f));
                v = f;
                break;
            }
            case 12: {
                uint64_t u = 0;
                for (int b = 0; b < 8; ++b)
                    u |= static_cast<uint64_t>(e.bytes[p + (le ? b : 7 - b)]) << (8 * b);
                double d;
                std::memcpy(&d, &u, sizeof(d));
                v = d;
                break;
            }
            case 5: {
                uint32_t a = readU32(e.bytes, p, le);
                uint32_t b = readU32(e.bytes, p + 4, le);
                v = b == 0 ? 0.0 : static_cast<double>(a) / b;
                break;
            }
            case 10: {
                int32_t a = static_cast<int32_t>(readU32(e.bytes, p, le));
                int32_t b = static_cast<int32_t>(readU32(e.bytes, p + 4, le));
                v = b == 0 ? 0.0 : static_cast<double>(a) / b;
                break;
            }
            default: break;
        }
        out.push_back(v);
    }
    return out;
}

bool readRawTiff(const std::string& path, RawInfo& info, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "cannot open " + path; return false; }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    if (data.size() < 16) { error = "file too small"; return false; }
    const bool le = data[0] == 'I' && data[1] == 'I';
    if (!le && !(data[0] == 'M' && data[1] == 'M')) {
        error = "not a TIFF file"; return false;
    }
    const uint16_t magic = readU16(data, 2, le);
    if (magic != 42) { error = "unsupported TIFF magic"; return false; }
    const uint32_t ifd = readU32(data, 4, le);
    if (static_cast<size_t>(ifd) + 2 > data.size()) { error = "bad IFD offset"; return false; }
    const uint16_t n = readU16(data, ifd, le);

    std::vector<TiffEntry> entries(n);
    for (uint16_t i = 0; i < n; ++i) {
        const size_t p = static_cast<size_t>(ifd) + 2 + static_cast<size_t>(i) * 12;
        if (p + 12 > data.size()) break;
        const uint16_t tag = readU16(data, p, le);
        TiffEntry& e = entries[i];
        e.type = readU16(data, p + 2, le);
        e.count = readU32(data, p + 4, le);
        const size_t ts = typeSize(e.type);
        const size_t total = ts * e.count;
        if (total <= 4) {
            e.bytes.assign(data.begin() + p + 8, data.begin() + p + 8 + total);
        } else {
            e.valueOffset = readU32(data, p + 8, le);
            if (static_cast<size_t>(e.valueOffset) + total <= data.size())
                e.bytes.assign(data.begin() + e.valueOffset,
                               data.begin() + e.valueOffset + total);
        }
        switch (tag) {
            case 256: if (!numericValues(e, le).empty()) info.width = static_cast<int>(numericValues(e, le)[0]); break;
            case 257: if (!numericValues(e, le).empty()) info.height = static_cast<int>(numericValues(e, le)[0]); break;
            case 258: { auto v = numericValues(e, le); if (!v.empty()) info.bits = static_cast<int>(v[0]); break; }
            case 259: { auto v = numericValues(e, le); if (!v.empty() && v[0] != 1) { error = "compressed DNG needs LibRaw"; return false; } break; }
            case 273: { auto v = numericValues(e, le); if (!v.empty()) info.stripOffset = static_cast<uint64_t>(v[0]); break; }
            case 279: { auto v = numericValues(e, le); if (!v.empty()) info.stripBytes = static_cast<uint64_t>(v[0]); break; }
            case 33422: { auto v = numericValues(e, le); for (size_t k = 0; k < 4 && k < v.size(); ++k) info.cfa[k] = static_cast<int>(v[k]); break; }
            case 50714: { auto v = numericValues(e, le); if (!v.empty()) info.black = v[0]; break; }
            case 50717: { auto v = numericValues(e, le); if (!v.empty()) info.white = v[0]; break; }
            case 50721: info.colorMatrix = numericValues(e, le); break;
            case 50728: info.asShotNeutral = numericValues(e, le); break;
            default: break;
        }
    }
    if (info.width <= 0 || info.height <= 0 || info.bits != 16 ||
        info.stripOffset == 0 || info.stripBytes == 0) {
        error = "unsupported raw layout, LibRaw required"; return false;
    }
    if (info.stripOffset + info.stripBytes > data.size()) {
        error = "truncated raw data"; return false;
    }
    const uint64_t needed = static_cast<uint64_t>(info.width) * info.height * 2;
    if (info.stripBytes < needed) { error = "short raw strip"; return false; }
    return true;
}

} // namespace

void setRawOptions(const RawOptions& options) { g_rawOptions = options; }

RawOptions rawOptions() { return g_rawOptions; }

bool readStillLinear(const std::string& path, cv::Mat& outLinear,
                     double& exposureSeconds, double& iso, std::string& error) {
    exposureSeconds = 0.0;
    iso = 0.0;
#ifdef PS_HAVE_LIBRAW
    LibRaw raw;
    int rc = raw.open_file(path.c_str());
    if (rc != LIBRAW_SUCCESS) { error = LibRaw::strerror(rc); return false; }
    rc = raw.unpack();
    if (rc != LIBRAW_SUCCESS) { error = LibRaw::strerror(rc); return false; }
    exposureSeconds = static_cast<double>(raw.imgdata.other.shutter);
    iso = static_cast<double>(raw.imgdata.other.iso_speed);
    if (exposureSeconds <= 0.0 || iso <= 0.0)
        readExifExposure(path, exposureSeconds, iso);
    auto& p = raw.imgdata.params;
    p.output_bps = 16;
    p.output_color = 1;
    p.use_camera_wb = 1;
    p.no_auto_bright = 1;
    p.gamm[0] = 1.0;
    p.gamm[1] = 1.0;
    p.bright = static_cast<float>(std::pow(2.0, g_rawOptions.exposureEV));
    rc = raw.dcraw_process();
    if (rc != LIBRAW_SUCCESS) { error = LibRaw::strerror(rc); return false; }
    libraw_processed_image_t* image = raw.dcraw_make_mem_image(&rc);
    if (!image || rc != LIBRAW_SUCCESS || image->bits != 16 || image->colors != 3) {
        error = "RAW linear development failed";
        LibRaw::dcraw_clear_mem(image);
        return false;
    }
    const int count = image->width * image->height;
    cv::Mat bgr16(image->height, image->width, CV_16UC3);
    const uint16_t* src = reinterpret_cast<const uint16_t*>(image->data);
    for (int i = 0; i < count; ++i) {
        auto* dst = reinterpret_cast<uint16_t*>(bgr16.data) + i * 3;
        dst[0] = src[i * 3 + 2]; // LibRaw gives RGB, OpenCV expects BGR
        dst[1] = src[i * 3 + 1];
        dst[2] = src[i * 3];
    }
    LibRaw::dcraw_clear_mem(image);
    bgr16.convertTo(outLinear, CV_32FC3, 1.0 / 65535.0);
    return true;
#else
    (void)path; (void)outLinear; (void)exposureSeconds; (void)iso;
    error = "built without LibRaw: linear RAW decoding unavailable";
    return false;
#endif
}

bool parseInsta360Calibration(const std::string& comment, CameraModel& model) {
    const size_t p = comment.find("INSTA360");
    if (p == std::string::npos) return false;
    std::string body = comment.substr(p);
    std::replace(body.begin(), body.end(), '_', ' ');
    std::istringstream ss(body);
    std::vector<std::string> tokens;
    std::string t;
    while (ss >> t) tokens.push_back(t);
    // INSTA3602 <a> <b> <c> ... <w> <h> ...
    if (tokens.size() < 6) return false;
    std::vector<double> nums;
    for (size_t i = 1; i < tokens.size(); ++i) {
        try { nums.push_back(std::stod(tokens[i])); } catch (...) { break; }
    }
    if (nums.size() < 5) return false;
    model.center = cv::Point2d(nums[1], nums[2]);
    model.focal = nums[3];
    model.fovDeg = 195.0;
    for (size_t i = 0; i + 1 < nums.size(); ++i) {
        if (nums[i] > 2000.0 && nums[i] < 8000.0 &&
            nums[i + 1] > 2000.0 && nums[i + 1] < 8000.0) {
            model.fovDeg = 180.0; // replaced by the lens model calibration below
            break;
        }
    }
    model.calibrated = true;
    return true;
}

bool readStill(const std::string& path, std::vector<cv::Mat>& out,
               std::string& error, CameraModel* model) {
    out.clear();
    error.clear();
    if (path.empty()) { error = "empty path"; return false; }

    if (isDngName(path)) {
#ifdef PS_HAVE_LIBRAW
        LibRaw raw;
        int rc = raw.open_file(path.c_str());
        if (rc != LIBRAW_SUCCESS) { error = LibRaw::strerror(rc); return false; }
        rc = raw.unpack();
        if (rc != LIBRAW_SUCCESS) { error = LibRaw::strerror(rc); return false; }
        auto& p = raw.imgdata.params;
        p.output_bps = 8;
        p.output_color = 1; // sRGB
        p.use_camera_wb = 1;
        p.no_auto_bright = g_rawOptions.autoBright ? 0 : 1;
        // LibRaw calls the EV multiplier "bright" (1.0 means unchanged).
        p.bright = static_cast<float>(std::pow(2.0, g_rawOptions.exposureEV));
        rc = raw.dcraw_process();
        if (rc != LIBRAW_SUCCESS) { error = LibRaw::strerror(rc); return false; }
        libraw_processed_image_t* img = raw.dcraw_make_mem_image(&rc);
        if (!img || rc != LIBRAW_SUCCESS) { error = "RAW processing failed"; return false; }
        if (img->colors == 3 && img->bits == 8) {
            cv::Mat rgb(img->height, img->width, CV_8UC3, img->data);
            cv::Mat bgr;
            cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
            out.push_back(bgr.clone());
        }
        LibRaw::dcraw_clear_mem(img);
        if (out.empty()) { error = "unsupported RAW output format"; return false; }
#else
        RawInfo info;
        if (!readRawTiff(path, info, error)) return false;
        std::ifstream in(path, std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
        const bool le = data[0] == 'I';
        cv::Mat buf(info.height, info.width, CV_16UC1);
        const uint8_t* src = data.data() + info.stripOffset;
        for (size_t i = 0; i < buf.total(); ++i)
            buf.ptr<uint16_t>(0)[i] = le
                ? static_cast<uint16_t>(src[2 * i] | (src[2 * i + 1] << 8))
                : static_cast<uint16_t>((src[2 * i] << 8) | src[2 * i + 1]);
        const double denom = std::max(1.0, info.white - info.black);
        for (int y = 0; y < buf.rows; ++y) {
            auto* row = buf.ptr<uint16_t>(y);
            for (int x = 0; x < buf.cols; ++x) {
                const double v = (static_cast<double>(row[x]) - info.black) / denom;
                row[x] = static_cast<uint16_t>(std::clamp(v, 0.0, 1.0) * 65535.0);
            }
        }
        int code = cv::COLOR_BayerRG2BGR;
        if (info.cfa[0] == 2 && info.cfa[3] == 0) code = cv::COLOR_BayerBG2BGR;
        else if (info.cfa[0] == 1) code = cv::COLOR_BayerGB2BGR;
        cv::Mat bgr16;
        cv::demosaicing(buf, bgr16, code);
        cv::Mat rgb;
        bgr16.convertTo(rgb, CV_32FC3, 1.0 / 65535.0);
        cv::cvtColor(rgb, rgb, cv::COLOR_BGR2RGB);

        // The built-in reader cannot apply DNG opcodes, but the colour matrix
        // and as-shot neutral are enough to remove the strong green cast of a
        // raw Bayer image. Channels are B,G,R in the Mat, converted above.
        double wb[3] = {1.0, 1.0, 1.0};
        if (info.asShotNeutral.size() >= 3 && info.asShotNeutral[1] > 1e-6) {
            for (int c = 0; c < 3; ++c)
                wb[c] = std::sqrt(info.asShotNeutral[1] /
                                  std::max(1e-6, info.asShotNeutral[c]));
        }
        double m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        // The DNG colour matrix is intentionally skipped in this fallback:
        // Insta360's GainMap opcodes already modify the raw samples, so a
        // second white balance and matrix double-correct the colours.
        (void)m;
        // XYZ to linear sRGB (sRGB D65 primaries).
        const double toRgb[9] = {3.2406, -1.5372, -0.4986,
                                 -0.9689, 1.8758, 0.0415,
                                 0.0557, -0.2040, 1.0570};
        const double rgbFromXyz[9] = {
            toRgb[0] * m[0] + toRgb[1] * m[3] + toRgb[2] * m[6],
            toRgb[0] * m[1] + toRgb[1] * m[4] + toRgb[2] * m[7],
            toRgb[0] * m[2] + toRgb[1] * m[5] + toRgb[2] * m[8],
            toRgb[3] * m[0] + toRgb[4] * m[3] + toRgb[5] * m[6],
            toRgb[3] * m[1] + toRgb[4] * m[4] + toRgb[5] * m[7],
            toRgb[3] * m[2] + toRgb[4] * m[5] + toRgb[5] * m[8],
            toRgb[6] * m[0] + toRgb[7] * m[3] + toRgb[8] * m[6],
            toRgb[6] * m[1] + toRgb[7] * m[4] + toRgb[8] * m[7],
            toRgb[6] * m[2] + toRgb[7] * m[5] + toRgb[8] * m[8],
        };
        for (int y = 0; y < rgb.rows; ++y) {
            auto* row = rgb.ptr<float>(y);
            for (int x = 0; x < rgb.cols * 3; ++x)
                row[x] = static_cast<float>(std::max(0.0, row[x] * wb[x % 3]));
        }
        // Apply the combined camera to sRGB matrix channel by channel.
        for (int y = 0; y < rgb.rows; ++y) {
            auto* row = rgb.ptr<float>(y);
            for (int x = 0; x < rgb.cols; ++x) {
                const float c0 = row[3 * x], c1 = row[3 * x + 1], c2 = row[3 * x + 2];
                row[3 * x] = static_cast<float>(std::max(0.0, rgbFromXyz[0] * c0 +
                                                        rgbFromXyz[1] * c1 + rgbFromXyz[2] * c2));
                row[3 * x + 1] = static_cast<float>(std::max(0.0, rgbFromXyz[3] * c0 +
                                                           rgbFromXyz[4] * c1 + rgbFromXyz[5] * c2));
                row[3 * x + 2] = static_cast<float>(std::max(0.0, rgbFromXyz[6] * c0 +
                                                           rgbFromXyz[7] * c1 + rgbFromXyz[8] * c2));
            }
        }
        // Auto exposure for the underexposed test frames, then sRGB gamma.
        const double mean = cv::mean(rgb)[0];
        const double autoGain = g_rawOptions.autoBright
                                    ? std::clamp(0.45 / std::max(1e-4, mean), 0.5, 12.0)
                                    : 1.0;
        const double exposure = autoGain * std::pow(2.0, g_rawOptions.exposureEV);
        rgb *= exposure;
        cv::pow(rgb, 1.0 / 2.2, rgb);
        cv::Mat bgr8;
        rgb.convertTo(bgr8, CV_8UC3, 255.0);
        cv::cvtColor(bgr8, bgr8, cv::COLOR_RGB2BGR);
        out.push_back(bgr8);
#endif
        // The stacked raw is split into two lenses by the caller.
        if (model && !out.empty()) {
            // Parse the UserComment through exiftool is not available in C++,
            // so the caller may supply a calibration separately.
        }
    } else {
        cv::Mat img = cv::imread(path, cv::IMREAD_COLOR);
        if (img.empty()) { error = "cannot decode image " + path; return false; }
        if (img.cols == 2 * img.rows) {
            int half = img.cols / 2;
            out.push_back(img(cv::Rect(0, 0, half, img.rows)).clone());
            out.push_back(img(cv::Rect(half, 0, half, img.rows)).clone());
        } else {
            out.push_back(img);
        }
    }
    return true;
}

} // namespace ps::io
