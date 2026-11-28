#pragma once

#include <string>

namespace ps {

enum class GpuVendor { Unknown, Nvidia, Amd, Intel };

enum class DecodeBackend { Cpu, Vaapi, Cuda, Qsv };

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
const GpuInfo& cachedGpu();

// Remembers a hardware decoder that failed on a real frame, so later runs do
// not retry it and flood the log with driver errors.
std::string gpuDecodeSignature(const GpuInfo& info);
bool hardwareDecodeFailed(const std::string& signature);
void markHardwareDecodeFailed(const std::string& signature);

const char* toString(GpuVendor vendor);
const char* toString(DecodeBackend backend);

// Human readable summary, used by the CLI and the GUI log.
std::string describeGpu(const GpuInfo& info);

} // namespace ps
