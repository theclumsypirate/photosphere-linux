#include "core/gpano.hpp"
#include "core/gpu_info.hpp"
#include "core/little_planet.hpp"
#include "core/stitcher.hpp"
#include "io/exif_pose.hpp"
#include "io/frame_store.hpp"
#include "io/image_io.hpp"
#include "io/insv_reader.hpp"
#include "io/video_writer.hpp"

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void usage() {
    std::cout << "photosphere-cli - offline 360 photo tools\n\n"
              << "Commands:\n"
              << "  gpu       show detected GPU and preferred decode backend\n"
              << "Global options for stitch/auto/batch/video: --refine, --seams,\n"
              << "  --no-multiband,\n"
              << "  --flip, --no-flip\n"
              << "  stitch    -o OUT.jpg [-w WIDTH] [--fov DEG] [--flip|--no-flip]\n"
              << "            [--no-multiband] [--max-frames N] INPUT...\n"
              << "  inspect   INPUT...\n"
              << "  extract   -i VIDEO.insv -o DIR [--max N] [--step N]\n"
              << "  auto      -o DIR INPUT... [-w WIDTH] [--fov DEG]\n"
              << "  video     -i VIDEO.insv -o OUT.mp4 [--every SEC] [-w WIDTH]\n"
              << "  batch     -i VIDEO.insv -o PREFIX [--every SEC] [--count N]\n"
              << "            [-w WIDTH] [--fov DEG] [--flip|--no-flip]\n"
              << "  littleplanet -i PANO.jpg -o OUT.png [--size N] [--fov DEG]\n"
              << "                [--tilt DEG] [--pan DEG] [--spin DEG]\n"
              << "  gpano     -i PANO.jpg -o TAGGED.jpg\n\n"
              << "Inputs may be DNG, JPG side by side pairs, INSV video, or a\n"
              << "regular MP4/MOV with two video tracks or one side by side\n"
              << "dual fisheye track.\n";
}

std::vector<std::string> positional(int argc, char** argv, int start) {
    std::vector<std::string> out;
    for (int i = start; i < argc; ++i) out.emplace_back(argv[i]);
    return out;
}

std::string optionValue(int argc, char** argv, const std::string& name,
                        const std::string& fallback = {}) {
    for (int i = 1; i + 1 < argc; ++i)
        if (name == argv[i]) return argv[i + 1];
    return fallback;
}

// INSV clips from the tested ONE X / X series are upright, while LRV proxies
// and OneRS stills are mounted upside down. Explicit flags always win.
bool autoFlipFor(const std::string& path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".insv") == 0)
        return false;
    if (lower.find("lrv") != std::string::npos) return true;
    return true;
}

bool autoFlipForAll(const std::vector<std::string>& files) {
    for (const auto& f : files)
        if (!autoFlipFor(f)) return false;
    return true;
}

bool hasFlag(int argc, char** argv, const std::string& name) {
    for (int i = 1; i < argc; ++i)
        if (name == argv[i]) return true;
    return false;
}

double numberOption(int argc, char** argv, const std::string& name, double fallback) {
    const std::string v = optionValue(argc, argv, name);
    if (v.empty()) return fallback;
    try { return std::stod(v); } catch (...) { return fallback; }
}

bool cmdStitch(int argc, char** argv) {
    const std::string outPath = optionValue(argc, argv, "-o");
    if (outPath.empty()) { std::cerr << "stitch: missing -o\n"; return false; }
    const std::string inputs = [&]() {
        std::vector<std::string> files;
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "-o" || a == "-w" || a == "--fov" || a == "--max-frames") { ++i; continue; }
            if (a == "--no-multiband" || a == "--no-flip" || a == "--flip" ||
                a == "--refine" || a == "--seams" || a == "--no-seams" ||
                a == "--keep-all" || a == "--no-local-warp" || a == "stitch") continue;
            files.push_back(a);
        }
        std::string s;
        for (const auto& f : files) s += f + ";";
        return s;
    }();
    std::vector<std::string> files;
    {
        std::string token;
        for (char c : inputs) {
            if (c == ';') { if (!token.empty()) files.push_back(token); token.clear(); }
            else token += c;
        }
    }
    if (files.empty()) { std::cerr << "stitch: no inputs\n"; return false; }

    ps::io::InputSession session;
    std::string error;
    const int maxFrames = static_cast<int>(numberOption(argc, argv, "--max-frames", 0));
    if (!ps::io::loadInputs(files, session, error, maxFrames)) {
        std::cerr << "stitch: " << error << "\n";
        return false;
    }
    std::cout << "Loaded " << session.frames.size() << " lens frames: "
              << session.description << "\n";
    ps::StitchOptions options;
    options.width = static_cast<int>(numberOption(argc, argv, "-w", 4096));
    options.fisheyeFovDegrees = numberOption(argc, argv, "--fov", 195.0);
    options.flipLenses = autoFlipForAll(files);
    if (hasFlag(argc, argv, "--no-flip")) options.flipLenses = false;
    if (hasFlag(argc, argv, "--flip")) options.flipLenses = true;
    options.multiband = !hasFlag(argc, argv, "--no-multiband");
    options.refinePoses = hasFlag(argc, argv, "--refine");
    options.useSeams = hasFlag(argc, argv, "--seams");
    options.dropOutliers = !hasFlag(argc, argv, "--keep-all");
    options.useLocalWarp = !hasFlag(argc, argv, "--no-local-warp");
    std::string diagnostics;
    const ps::Panorama pano = ps::stitchSession(session, options, diagnostics);
    if (pano.equirect.empty()) { std::cerr << "stitch failed: " << diagnostics << "\n"; return false; }
    if (!cv::imwrite(outPath, pano.equirect)) {
        std::cerr << "cannot write " << outPath << "\n";
        return false;
    }
    std::cout << diagnostics << "\nWrote " << outPath << " (" << pano.width << "x"
              << pano.height << ")\n";
    return true;
}

bool cmdBatch(int argc, char** argv) {
    const std::string input = optionValue(argc, argv, "-i");
    const std::string prefix = optionValue(argc, argv, "-o");
    if (input.empty() || prefix.empty()) {
        std::cerr << "batch: need -i VIDEO.insv -o PREFIX\n";
        return false;
    }
    const double everySeconds = numberOption(argc, argv, "--every", 10.0);
    const int count = static_cast<int>(numberOption(argc, argv, "--count", 2));
    const double duration = ps::io::insvDuration(input);
    std::vector<double> times;
    for (double t = 0.0; t < duration && (count <= 0 || times.size() < static_cast<size_t>(count));
         t += everySeconds)
        times.push_back(t);
    if (times.empty()) times.push_back(0.0);
    std::vector<ps::LensFrame> frames;
    std::string error;
    if (!ps::io::readInsvAtTimes(input, times, frames, error)) {
        std::cerr << "batch: " << error << "\n";
        return false;
    }
    ps::StitchOptions options;
    options.width = static_cast<int>(numberOption(argc, argv, "-w", 2048));
    options.fisheyeFovDegrees = numberOption(argc, argv, "--fov", 195.0);
    options.flipLenses = autoFlipFor(input);
    if (hasFlag(argc, argv, "--no-flip")) options.flipLenses = false;
    if (hasFlag(argc, argv, "--flip")) options.flipLenses = true;
    options.multiband = !hasFlag(argc, argv, "--no-multiband");
    options.refinePoses = hasFlag(argc, argv, "--refine");
    options.useSeams = hasFlag(argc, argv, "--seams");
    options.dropOutliers = !hasFlag(argc, argv, "--keep-all");
    options.useLocalWarp = !hasFlag(argc, argv, "--no-local-warp");

    std::cout << "duration=" << duration << " s, seeking to " << times.size()
              << " timestamps, " << frames.size() << " lens frames\n";
    int written = 0;
    for (size_t i = 0; i + 1 < frames.size(); i += 2) {
        if (frames[i].lens == frames[i + 1].lens) continue;
        std::vector<ps::LensFrame> pair{frames[i], frames[i + 1]};
        std::string diagnostics;
        const ps::Panorama pano = ps::stitchDualFisheye(pair, options, diagnostics);
        if (pano.equirect.empty()) continue;
        const std::string path = prefix + "_" +
                                 std::string(3 - std::to_string(written).size(), '0') +
                                 std::to_string(written) + ".jpg";
        cv::imwrite(path, pano.equirect);
        std::cout << "t=" << frames[i].timestamp << " s -> " << path << "\n";
        ++written;
    }
    std::cout << "Wrote " << written << " panoramas\n";
    return written > 0;
}

bool cmdVideo(int argc, char** argv) {
    const std::string input = optionValue(argc, argv, "-i");
    const std::string output = optionValue(argc, argv, "-o");
    if (input.empty() || output.empty()) {
        std::cerr << "video: need -i VIDEO.insv -o OUT.mp4\n";
        return false;
    }
    const double everySeconds = numberOption(argc, argv, "--every", 0.2);
    const double fps = numberOption(argc, argv, "--fps", 25.0);
    const double duration = ps::io::insvDuration(input);
    std::vector<double> times;
    for (double t = 0.0; t < duration; t += everySeconds) times.push_back(t);
    if (times.empty()) times.push_back(0.0);

    int width = static_cast<int>(numberOption(argc, argv, "-w", 2048));
    if (width & 1) --width;
    ps::StitchOptions options;
    options.width = width;
    options.fisheyeFovDegrees = numberOption(argc, argv, "--fov", 195.0);
    options.flipLenses = autoFlipFor(input);
    if (hasFlag(argc, argv, "--no-flip")) options.flipLenses = false;
    if (hasFlag(argc, argv, "--flip")) options.flipLenses = true;
    options.multiband = !hasFlag(argc, argv, "--no-multiband");
    options.refinePoses = hasFlag(argc, argv, "--refine");
    options.useSeams = hasFlag(argc, argv, "--seams");
    options.dropOutliers = !hasFlag(argc, argv, "--keep-all");
    options.useLocalWarp = !hasFlag(argc, argv, "--no-local-warp");

    std::string error;
    ps::io::Mp4Writer writer(output, width, width / 2, fps);
    if (!writer.open(error)) { std::cerr << "video: " << error << "\n"; return false; }
    std::cout << "duration=" << duration << " s, " << times.size()
              << " equirectangular frames, " << width << "x" << width / 2 << " @ " << fps
              << " fps\n";
    int written = 0;
    for (size_t i = 0; i < times.size(); ++i) {
        std::vector<ps::LensFrame> frames;
        if (!ps::io::readInsvAtTimes(input, {times[i]}, frames, error)) continue;
        if (frames.size() < 2 || frames[0].lens == frames[1].lens) continue;
        std::string diagnostics;
        const ps::Panorama pano = ps::stitchDualFisheye({frames[0], frames[1]},
                                                        options, diagnostics);
        if (pano.equirect.empty()) continue;
        if (!writer.write(pano.equirect, error)) {
            std::cerr << "video: " << error << "\n";
            return false;
        }
        ++written;
        if ((written % 5) == 0)
            std::cout << "t=" << times[i] << " s, " << written << " frames encoded\n";
    }
    writer.finish();
    std::cout << "Wrote " << output << " with " << written << " panorama frames\n";
    return written > 0;
}

bool cmdAuto(int argc, char** argv) {
    const std::string dir = optionValue(argc, argv, "-o");
    std::vector<std::string> files;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" || a == "-w" || a == "--fov" || a == "--max-frames") { ++i; continue; }
        if (a == "--refine" || a == "--seams" || a == "--no-seams" ||
            a == "--keep-all" || a == "--no-local-warp" || a == "--no-multiband" ||
            a == "--flip" || a == "--no-flip") continue;
        files.push_back(a);
    }
    if (dir.empty() || files.empty()) {
        std::cerr << "auto: need -o DIR and at least one input\n";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    ps::StitchOptions options;
    options.width = static_cast<int>(numberOption(argc, argv, "-w", 4096));
    options.fisheyeFovDegrees = numberOption(argc, argv, "--fov", 195.0);
    options.flipLenses = autoFlipForAll(files);
    if (hasFlag(argc, argv, "--no-flip")) options.flipLenses = false;
    if (hasFlag(argc, argv, "--flip")) options.flipLenses = true;
    const int maxFrames = static_cast<int>(numberOption(argc, argv, "--max-frames", 1));
    std::vector<ps::io::BatchItem> items;
    std::string error;
    if (!ps::io::stitchAllInputs(files, options, items, error, maxFrames)) {
        std::cerr << "auto: " << error;
        return false;
    }
    if (!error.empty()) std::cerr << "auto warnings:\n" << error;
    int index = 0;
    for (const auto& item : items) {
        const std::string name = "pano_" +
                                 std::string(3 - std::to_string(index).size(), '0') +
                                 std::to_string(index) + ".jpg";
        const std::string path = (std::filesystem::path(dir) / name).string();
        cv::imwrite(path, item.panorama.equirect);
        std::cout << item.source << " -> " << path << " (" << item.diagnostics << ")\n";
        ++index;
    }
    std::cout << "Wrote " << index << " panoramas to " << dir << "\n";
    return true;
}

bool cmdInspect(int argc, char** argv) {
    for (const auto& path : positional(argc, argv, 2)) {
        if (ps::io::isInsvFile(path)) {
            std::vector<ps::LensFrame> frames;
            std::string error;
            if (!ps::io::readInsv(path, frames, 1, 1, error)) {
                std::cout << path << ": INSV error: " << error << "\n";
            } else {
                std::cout << path << ": INSV, " << frames.size() << " lens tracks, "
                          << frames[0].image.cols << "x" << frames[0].image.rows << "\n";
            }
        } else {
            std::vector<cv::Mat> images;
            std::string error;
            ps::CameraModel model;
            if (!ps::io::readStill(path, images, error, &model)) {
                std::cout << path << ": error: " << error << "\n";
            } else {
                std::cout << path << ": " << images.size() << " lens image(s), ";
                for (const auto& im : images)
                    std::cout << im.cols << "x" << im.rows << " ";
                std::cout << "\n";
                ps::io::ExifPose pose;
                if (ps::io::readExifPose(path, pose))
                    std::cout << "  pose index=" << pose.index << " yaw="
                              << pose.yawDegrees << " pitch=" << pose.pitchDegrees
                              << " roll=" << pose.rollDegrees << "\n";
                if (model.calibrated)
                    std::cout << "  calibration: center=" << model.center.x << ","
                              << model.center.y << " focal=" << model.focal << "\n";
            }
        }
    }
    return true;
}

bool cmdExtract(int argc, char** argv) {
    const std::string input = optionValue(argc, argv, "-i");
    const std::string dir = optionValue(argc, argv, "-o");
    if (input.empty() || dir.empty()) {
        std::cerr << "extract: need -i and -o\n";
        return false;
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::vector<ps::LensFrame> frames;
    std::string error;
    const int maxFrames = static_cast<int>(numberOption(argc, argv, "--max", 0));
    const int step = static_cast<int>(numberOption(argc, argv, "--step", 1));
    if (!ps::io::readInsv(input, frames, maxFrames, step, error)) {
        std::cerr << "extract: " << error << "\n";
        return false;
    }
    for (const auto& f : frames) {
        const std::string name = "lens" + std::to_string(f.lens) + "_" +
                                 std::to_string(f.sequence) + ".jpg";
        cv::imwrite((fs::path(dir) / name).string(), f.image);
    }
    std::cout << "Wrote " << frames.size() << " frames to " << dir << "\n";
    return true;
}

bool cmdLittlePlanet(int argc, char** argv) {
    const std::string input = optionValue(argc, argv, "-i");
    const std::string output = optionValue(argc, argv, "-o");
    if (input.empty() || output.empty()) {
        std::cerr << "littleplanet: need -i and -o\n";
        return false;
    }
    const cv::Mat equirect = cv::imread(input, cv::IMREAD_COLOR);
    if (equirect.empty()) { std::cerr << "cannot read " << input << "\n"; return false; }
    ps::LittlePlanetOptions options;
    options.outputSize = static_cast<int>(numberOption(argc, argv, "--size", 2048));
    options.fovDegrees = numberOption(argc, argv, "--fov", 180.0);
    options.tiltDegrees = numberOption(argc, argv, "--tilt", 0.0);
    options.panDegrees = numberOption(argc, argv, "--pan", 0.0);
    options.spinDegrees = numberOption(argc, argv, "--spin", 0.0);
    const cv::Mat planet = ps::convertToLittlePlanet(equirect, options);
    if (!cv::imwrite(output, planet)) {
        std::cerr << "cannot write " << output << "\n";
        return false;
    }
    std::cout << "Wrote " << output << "\n";
    return true;
}

bool cmdGpano(int argc, char** argv) {
    const std::string input = optionValue(argc, argv, "-i");
    const std::string output = optionValue(argc, argv, "-o");
    if (input.empty() || output.empty()) {
        std::cerr << "gpano: need -i and -o\n";
        return false;
    }
    const cv::Mat img = cv::imread(input, cv::IMREAD_COLOR);
    if (img.empty()) { std::cerr << "cannot read " << input << "\n"; return false; }
    std::string error;
    if (!ps::injectGpano(input, output,
                         ps::GPanoMetadata::forFullPano(img.cols, img.rows), error)) {
        std::cerr << "gpano: " << error << "\n";
        return false;
    }
    std::cout << "Wrote " << output << "\n";
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    const std::string command = argv[1];
    if (command == "gpu") {
        std::cout << ps::describeGpu(ps::detectGpu()) << "\n";
        return 0;
    }
    if (command == "stitch") return cmdStitch(argc, argv) ? 0 : 2;
    if (command == "inspect") return cmdInspect(argc, argv) ? 0 : 2;
    if (command == "extract") return cmdExtract(argc, argv) ? 0 : 2;
    if (command == "batch") return cmdBatch(argc, argv) ? 0 : 2;
    if (command == "auto") return cmdAuto(argc, argv) ? 0 : 2;
    if (command == "video") return cmdVideo(argc, argv) ? 0 : 2;
    if (command == "littleplanet") return cmdLittlePlanet(argc, argv) ? 0 : 2;
    if (command == "gpano") return cmdGpano(argc, argv) ? 0 : 2;
    if (command == "--help" || command == "-h") { usage(); return 0; }
    std::cerr << "unknown command: " << command << "\n";
    usage();
    return 1;
}
