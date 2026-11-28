#pragma once

#include <string>

namespace ps {

enum class GpuVendor { Unknown, Nvidia, Amd, Intel };

enum class DecodeBackend { Cpu, Vaapi, Cuda, Qsv, Vulkan };

struct GpuInfo {
    GpuVendor vendor = GpuVendor::Unknown;
    std::string deviceName;
    std::string driver;
    bool hasDrmRenderNode = false;
    bool hasNvidiaDevice = false;
    bool hasVaapiLibrary = false;
    bool hasQsvLibrary = false;
    bool hasVulkanIcd = false;
    int opencvCudaDevices = 0;

    // Preferred video decode backend. Actual support is verified at decoder
    // creation and falls back to CPU when the driver cannot decode the codec.
    DecodeBackend preferredDecodeBackend() const;
};

GpuInfo detectGpu();
const char* toString(GpuVendor vendor);
const char* toString(DecodeBackend backend);

// Human readable summary, used by the CLI and the GUI log.
std::string describeGpu(const GpuInfo& info);

} // namespace ps
