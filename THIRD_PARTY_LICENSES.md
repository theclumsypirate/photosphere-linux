# Third-party licenses

The application source code in this repository is MIT licensed (see
[LICENSE](LICENSE)). The distributed AppImage also contains unmodified third
party shared libraries. Some of them are GPL licensed, so the AppImage as a
whole is distributed under the GNU General Public License, version 2 or later.
The MIT license of our own code remains unchanged.

Bundled components and their licenses:

| Component | Version | License | Source |
| --- | --- | --- | --- |
| FFmpeg / libav | 8.0.1 (Ubuntu 7:8.0.1-3ubuntu2, built with `--enable-gpl`) | GPL-2.0-or-later | https://ffmpeg.org/download.html |
| x264 | 0.165.3222 (Ubuntu 2:0.165.3222+gitb35605ac) | GPL-2.0-or-later | https://code.videolan.org/videolan/x264 |
| Qt 6 | 6.10.2 | LGPL-3.0 | https://download.qt.io/official_releases/qt/ |
| OpenCV | 4.10.0 | Apache-2.0 | https://opencv.org/releases/ |
| LibRaw | 0.21.5 | LGPL-2.1 or CDDL-1.0 | https://www.libraw.org/download |
| libx265, libaom, libvpx, libdav1d and other codecs | Ubuntu 26.04 packages | See each package copyright | https://packages.ubuntu.com/ |

Full license texts for the main components are included in the
[licenses](licenses) directory and inside the AppImage at
`usr/share/licenses/photosphere-linux/`.

## Written offer for source code

The bundled libraries are unmodified binaries taken from Ubuntu 26.04
packages. Their corresponding source code is available at no extra charge
from the upstream projects listed above and from the Ubuntu source packages
for the exact versions shown in the table. See https://packages.ubuntu.com/
and the `copyright` file of each package for details.

If you need the exact source files used for a build, open an issue in this
repository and we will provide them.
