#pragma once

#include <string>

namespace ps {

struct GPanoMetadata {
    bool usePanoramaViewer = true;
    std::string projectionType = "equirectangular";
    int fullWidth = 0;
    int fullHeight = 0;
    int croppedWidth = 0;
    int croppedHeight = 0;
    int croppedLeft = 0;
    int croppedTop = 0;

    static GPanoMetadata forFullPano(int width, int height);
};

// Builds the GPano XMP packet text.
std::string buildXmpPacket(const GPanoMetadata& metadata);

// Copies a JPEG byte for byte, replacing any XMP APP1 with the given metadata.
// The entropy-coded scan is never decoded or re-encoded.
bool injectGpano(const std::string& inputPath, const std::string& outputPath,
                 const GPanoMetadata& metadata, std::string& error);

} // namespace ps
