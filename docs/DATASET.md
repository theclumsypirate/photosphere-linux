# Dataset notes

All observations below are read only, taken from `/home/dani/Downloads/dataset/`.

## JPG frames

`IMG_20220716_142858_00_004.jpg` and its `(1)` copy:

- 6528x3264, baseline JPEG, YCbCr 4:2:2.
- Make `Arashi Vision`, model `Insta360 OneRS`, software `v2.0.0_build6`.
- ImageDescription points at `IMG_20220716_142858_00_004.insp`.
- Exposure 1/2667 s, f/2.2, ISO 102, focal 2.4 mm (6 mm equivalent).
- A MakerNote block holds nine floats, for example
  `-1.000977_0.016602_0.063477_0.000000_0.000000_0.001065`.
- ExifTool reports an embedded Insta360 trailer with timed accelerometer and
  gyroscope samples. That data is not needed for a first visual stitcher, but
  it is available if pose seeding is wanted later.

The 6528x3264 image is a side by side pair of 3264x3264 fisheye views.

## DNG frames

`IMG_20220716_142858_00_004.dng` is missing from the dataset. Present are
`00_005`, `00_007` and `00_012`. All three are identical in metadata:

- TIFF little endian, DNG 1.3.0.0.
- 3264x6528, 16 bit, uncompressed, single CFA plane, SamplesPerPixel 1.
- CFA pattern `[Red,Green][Green,Blue]`, black level 799/800/800/799,
  white level 16383.
- Color matrix 1 for D65:
  `1.4462 -0.8273 -0.1252 / -0.1818 1.0996 0.0918 / 0.0193 0.0440 0.7474`.
- As shot neutral `0.482734 1 0.491835`.
- GainMap opcodes (8 of them).
- UserComment, an Insta360 calibration string:

      INSTA3602_1632.300_1605.970_1629.610_-0.045_-0.138_179.385_
      1629.170_1606.500_4891.020_0.150_-0.366_0.672_3264_6528_
      3134 31745_32787_32832_32768_32773_32767

  The three values near 1600 and the one near 4891 look like principal point
  and radius parameters of the two fisheye lenses. The trailing numbers are
  finish/start row coordinates of some kind.

The DNG height 6528 is exactly twice the width 3264, so each raw file holds
two stacked 3264x3264 fisheye images, one per lens.

OpenCV 4.13 `imread` cannot open this DNG: TIFF photometric interpretation
32803 (CFA) is not handled by its TIFF backend. LibRaw is therefore the
preferred reader. A minimal built in reader for uncompressed single strip CFA
DNG is kept as a fallback, with demosaicing done by OpenCV.

## INSV video

`1745586212-vid_20250416_143628_00_005.insv`:

- 607965492 bytes, 24.44 s, MP4 family (`ftyp avc1`, `isom`).
- Box order: `ftyp`, `wide`, `mdat` (593878121 bytes), `moov`, `inst`
  (10840168 bytes).
- `moov/udta` contains `AMBA` (139 bytes) and `nail` (1228816 bytes,
  probably a cover image).
- Three tracks:
  1. HEVC Main, 3840x3840, yuvj420p, 25 fps, 611 frames, ~97 Mbit/s,
     handler `INS.HVC`.
  2. same, second lens, ~96 Mbit/s.
  3. AAC LC stereo, 48 kHz, ~189 kbit/s, handler `INS.AAC`.
- No separate gyro data track is exposed by ffprobe. The `inst` box at the
  end is the proprietary telemetry container and is optional for a visual
  pipeline.

Consequence: `avformat` plus `avcodec` can decode both fisheye streams. No
Insta360 SDK is required to read pixels. The SDK may still be useful for
official stitching parameters or newer camera models, but it is proprietary
and has no documented general Linux C++ distribution, so the port does not
depend on it.
