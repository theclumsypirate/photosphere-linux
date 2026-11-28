#include "core/gpu_info.hpp"

#include <opencv2/core.hpp>
#include <opencv2/core/cuda.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace ps {
namespace {

std::string readFile(const fs::path& path) {
    std::ifstream in(path);
    if (!in) return {};
    std::string line;
    std::getline(in, line);
    return line;
}

bool pathExists(const fs::path& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

bool directoryHasFile(const fs::path& dir, const std::string& suffix) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
    for (const auto& entry : fs::directory_iterator(dir, ec))
        if (entry.path().filename().string().find(suffix) != std::string::npos) return true;
    return false;
}

} // namespace

GpuInfo detectGpu() {
    GpuInfo info;
    if (std::getenv("PS_DISABLE_GPU")) return info;
    std::error_code drmError;
    for (const auto& entry : fs::directory_iterator("/sys/class/drm", drmError)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("card", 0) != 0) continue;
        const fs::path vendorPath = entry.path() / "device" / "vendor";
        if (!pathExists(vendorPath)) continue;
        const std::string id = readFile(vendorPath);
        if (id == "0x10de" || id == "0x10DE") info.vendor = GpuVendor::Nvidia;
        else if (id == "0x1002" || id == "0x1002") info.vendor = GpuVendor::Amd;
        else if (id == "0x8086" || id == "0x8086") info.vendor = GpuVendor::Intel;
        const std::string uevent = [&]() {
            std::ifstream in(entry.path() / "device" / "uevent");
            std::string line;
            while (std::getline(in, line))
                if (line.rfind("DRIVER=", 0) == 0) return line.substr(7);
            return std::string{};
        }();
        if (!uevent.empty()) info.driver = uevent;
        if (info.deviceName.empty()) info.deviceName = name;
        if (info.vendor != GpuVendor::Unknown) break;
    }
    for (const auto& path : {"/dev/nvidia0", "/dev/nvidiactl"})
        if (pathExists(path)) info.hasNvidiaDevice = true;
    std::error_code driError;
    for (const auto& entry : fs::directory_iterator("/dev/dri", driError))
        if (entry.path().filename().string().rfind("renderD", 0) == 0)
            info.hasDrmRenderNode = true;
    info.hasVaapiLibrary = pathExists("/usr/lib/x86_64-linux-gnu/libva.so.2") ||
                           pathExists("/usr/lib/aarch64-linux-gnu/libva.so.2");
    info.hasQsvLibrary = pathExists("/usr/lib/x86_64-linux-gnu/libmfx.so.1") ||
                         directoryHasFile("/usr/lib/x86_64-linux-gnu", "libvpl.so");
    info.hasVulkanIcd = directoryHasFile("/usr/share/vulkan/icd.d", ".json") ||
                        directoryHasFile("/etc/vulkan/icd.d", ".json");
    info.opencvCudaDevices = cv::cuda::getCudaEnabledDeviceCount();
    return info;
}

namespace {

fs::path decodeFailureMarker() {
    const char* home = std::getenv("HOME");
    const fs::path base = home ? fs::path(home) : fs::temp_directory_path();
    return base / ".cache" / "photosphere" / "hw_decode_failed";
}

} // namespace

std::string gpuDecodeSignature(const GpuInfo& info) {
    return std::string(toString(info.vendor)) + "|" + info.deviceName + "|" +
           info.driver + "|" + std::to_string(info.hasNvidiaDevice ? 1 : 0) +
           "|" + std::to_string(info.hasDrmRenderNode ? 1 : 0);
}

bool hardwareDecodeFailed(const std::string& signature) {
    std::ifstream in(decodeFailureMarker());
    std::string stored;
    std::getline(in, stored);
    return stored == signature;
}

void markHardwareDecodeFailed(const std::string& signature) {
    const fs::path marker = decodeFailureMarker();
    std::error_code ec;
    fs::create_directories(marker.parent_path(), ec);
    std::ofstream out(marker);
    out << signature << "\n";
}

DecodeBackend GpuInfo::preferredDecodeBackend() const {
    if (hardwareDecodeFailed(gpuDecodeSignature(*this))) return DecodeBackend::Cpu;
    if (hasNvidiaDevice || vendor == GpuVendor::Nvidia || opencvCudaDevices > 0)
        return DecodeBackend::Cuda;
    if (hasQsvLibrary && hasDrmRenderNode && vendor == GpuVendor::Intel)
        return DecodeBackend::Qsv;
    if (hasVaapiLibrary && hasDrmRenderNode)
        return DecodeBackend::Vaapi;
    return DecodeBackend::Cpu;
}

const char* toString(GpuVendor vendor) {
    switch (vendor) {
        case GpuVendor::Nvidia: return "NVIDIA";
        case GpuVendor::Amd: return "AMD";
        case GpuVendor::Intel: return "Intel";
        default: return "unknown";
    }
}

const char* toString(DecodeBackend backend) {
    switch (backend) {
        case DecodeBackend::Cuda: return "CUDA/NVDEC";
        case DecodeBackend::Vaapi: return "VA-API";
        case DecodeBackend::Qsv: return "Intel QSV";
        default: return "CPU";
    }
}

std::string describeGpu(const GpuInfo& info) {
    std::string out = "GPU: " + std::string(toString(info.vendor));
    if (!info.deviceName.empty()) out += " (" + info.deviceName + ")";
    if (!info.driver.empty()) out += ", driver=" + info.driver;
    out += "\n  VA-API=" + std::string(info.hasVaapiLibrary && info.hasDrmRenderNode ? "yes" : "no") +
           " QSV=" + std::string(info.hasQsvLibrary ? "yes" : "no") +
           " Vulkan=" + std::string(info.hasVulkanIcd ? "yes" : "no") +
           " OpenCV-CUDA=" + std::to_string(info.opencvCudaDevices);
    out += std::string("\n  preferred video decode: ") + toString(info.preferredDecodeBackend());
    return out;
}

} // namespace ps
