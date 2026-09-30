# Third-party components

Lumen is licensed under the **GNU Affero General Public License v3.0 or later** (see [LICENSE](LICENSE)).
The components below are used under their own licenses. All of them are compatible with the AGPLv3,
and the GPL-licensed ones through section 13 of the GPLv3/AGPLv3.

| Component | Use | License |
|-----------|-----|---------|
| [Qt 6](https://www.qt.io) | UI, networking, XML | LGPL-3.0 |
| [mpv / libmpv](https://mpv.io) | Playback | GPL-2.0-or-later (as built by MSYS2/Homebrew) |
| [FFmpeg](https://ffmpeg.org) | Decoding | GPL-2.0-or-later (GPL builds) |
| [FFmpeg-mvc](https://github.com/tthayer93/FFmpeg-mvc) | Blu-ray 3D (MVC) decoding, optional | GPL-2.0-or-later |
| [libbluray](https://www.videolan.org/developers/libbluray.html) | Blu-ray navigation | LGPL-2.1-or-later |
| [libdvdnav / libdvdread](https://www.videolan.org/developers/libdvdnav.html) | DVD navigation | GPL-2.0-or-later |
| [libcdio / libiso9660](https://www.gnu.org/software/libcdio/) | Video CD, audio CD | GPL-3.0-or-later |
| [OpenSSL](https://www.openssl.org) | DCP KDM/AES, certificates | Apache-2.0 |
| [libxml2](https://gitlab.gnome.org/GNOME/libxml2) | KDM signature canonicalisation (C14N) | MIT |
| [DTS IAB Renderer](https://github.com/DTSProAudio/iab-renderer) | Dolby Atmos / SMPTE IAB rendering | BSD-3-Clause ([resources/iab/LICENSE.DTS](resources/iab/LICENSE.DTS)) |

Not included: **libaacs, libbdplus, libdvdcss** and any other copy-protection circumvention.
Instead, Lumen ships its own libdvdcss stand-in, [`src/dvdcss_shim.c`](src/dvdcss_shim.c), which contains no CSS code.
Users can add such libraries themselves through [plugins](plugins/README.md).

Test material is generated during the build or CI by these tools. They are not part of the program:
- FFmpeg
- OpenSSL
- dvdauthor (GPL-2.0)
- GNU VCDImager (GPL-2.0)
- the DTS IAB packer
