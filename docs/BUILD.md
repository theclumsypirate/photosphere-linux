# Build

## Dependencies

Installed on this machine:

- g++ 15.2, CMake 4.2, Ninja
- OpenCV 4.10 (`libopencv-dev`)
- libav / ffmpeg 62 (`libavcodec-dev`, `libavformat-dev`, `libswscale-dev`)
- LibRaw 0.21.5 (`libraw-dev`)
- Qt 6.10.2 (`qt6-base-dev`)
- GPU: auto-detected at runtime. NVIDIA (CUDA/NVDEC), AMD/Intel (VA-API),
  Intel QSV and OpenCV CUDA are probed; unsupported codecs fall back to CPU.

CMake finds everything through `find_package(OpenCV)` and `pkg-config`.
LibRaw and libav are optional at configure time: without LibRaw the built in
uncompressed CFA reader is used (geometry works, colour is approximate);
without libav the INSV reader is disabled.

## Commands

CLI only:

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j4
    ./build/ps_tests
    ./build/photosphere-cli --help

CLI plus GUI:

    cmake -S . -B build-gui -G Ninja -DCMAKE_BUILD_TYPE=Release -DPS_BUILD_GUI=ON
    cmake --build build-gui -j4
    ./build-gui/photosphere-gui

## CLI examples

    # Stitch one Insta360 DNG (two stacked fisheye lenses)
    ./build/photosphere-cli stitch -w 4096 -o pano.jpg frame.dng

    # Stitch a side by side dual fisheye JPEG
    ./build/photosphere-cli stitch -w 4096 -o pano.jpg frame.jpg

    # Stitch a ring of ordinary photos using EXIF poses and FOV
    ./build/photosphere-cli stitch -o pano.jpg photo_01.jpg photo_02.jpg ...
    # Add --refine to also align poses from image features

    # Stitch the first frame pair of an INSV video
    ./build/photosphere-cli stitch --max-frames 1 -o pano.jpg clip.insv

    # A regular MP4/MOV with two video tracks, or one side by side dual
    # fisheye track (Insta360 LRV is 768x384, split into two 384x384 lenses)
    ./build/photosphere-cli stitch --max-frames 1 -o pano.jpg LRV_....mp4

    # Show detected GPU and preferred decode backend
    ./build/photosphere-cli gpu

    # Process several files at once (DNG + JPG + INSV), one panorama each
    ./build/photosphere-cli auto -o output_dir frame.dng frame.jpg clip.insv

    # One panorama every N seconds from a video. Defaults: --every 10 --count 2.
    # It seeks to timestamps instead of decoding the whole file.
    ./build/photosphere-cli batch -i clip.insv -o sequence

    # Stitch selected video frames into an equirectangular H.264 MP4
    ./build/photosphere-cli video -i clip.insv -o stitched.mp4 --every 1 -w 2048

    # Decimate an INSV video to lens frames
    ./build/photosphere-cli extract -i clip.insv -o frames --max 20 --step 5

    # Little planet and GPano metadata
    ./build/photosphere-cli littleplanet -i pano.jpg -o planet.png --size 2048
    ./build/photosphere-cli gpano -i pano.jpg -o tagged.jpg

`--fov` tunes the fisheye field of view (default 195 degrees).
`--flip` / `--no-flip` override the automatic lens orientation. OneRS stills
need a 180 degree rotation; the tested ONE X / X series INSV video does not.
`--no-multiband` switches to a plain feathered mean.

## Verification done on the supplied dataset

- `IMG_20220716_142858_00_005.dng`: read through LibRaw, two 3264x3264 lenses,
  stitched to a 2048x1024 equirectangular panorama with correct colours.
- `IMG_20220716_142858_00_004.jpg`: side by side 6528x3264, split and stitched.
- `1745586212-vid_20250416_143628_00_005.insv`: two HEVC 3840x3840 tracks
  decoded through libav, first frame pair stitched.
- Unit tests cover rotation algebra, spherical geometry, lens distortion,
  quaternion conversion, feather weights.
