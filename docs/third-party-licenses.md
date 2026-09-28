<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
SPDX-License-Identifier: Apache-2.0

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# TPIP Report for Open Perception Kit

Source: [OPK SBOM — a096f677d](https://confluence.arm.com/spaces/edgeaiexpkits/pages/3186688252/OPK+SBOM+%E2%80%94+a096f677d),
Confluence page version 5, updated 2026-09-23. Imported on 2026-09-28.

This report preserves all component/version entries, licence labels and origins
from the supplied scan snapshot, including development and system dependencies.
The source supplies package origins rather than verified repository and licence
URLs for every entry, so the table retains its **Origin** column.

For the components collected for the current build and their original notice
locations, see [Third-party notices](../THIRD_PARTY_NOTICE.md). Each architecture
package and runtime image contains its own versioned `THIRD_PARTY_LICENSES.md`
and `components.json` under `share/opk/licenses/`.
[Release reconciliation notes](arch/release-process.md#licence-evidence-before-publication)
describe known differences between this scan and the current source pins.

The Apache notice above covers this report. Third-party components retain their
own licences and copyrights.

## Scan reference and reported review status

| Scan reference | Value |
| --- | --- |
| Git tag | [v0.4.0-fsr-base](https://github.com/Arm-Debug/amp-dev-forge/tree/v0.4.0-fsr-base) |
| Source revision | [8d8d38bf8d1513b197ce6fedf1c615f1e7731e3d](https://github.com/Arm-Debug/amp-dev-forge/commit/8d8d38bf8d1513b197ce6fedf1c615f1e7731e3d) |
| Run started (UTC) | 2026-09-21 10:15:26 |
| Black Duck version | [v0.4.0-fsr-base](https://arm.app.blackduck.com/ui/projects/781fdcd2-93c1-4b23-9ec0-358b0f6ee090/versions/bfd3cf14-a2b3-4db0-90ba-b2a5d2d66163/components) |
| Results snapshot | 2026-09-21 |

**Verdict:** License review required — 77 risk-flagged entries; 4 unknown-license entries.

**1,111 component/version entries recorded in Black Duck.** Repeated origins are combined; versions and licenses are as reported by Black Duck.

The inventory has four rows whose entire licence value is `Unknown`, plus
two rows with `Unknown` as an alternative in a compound expression. These
values are retained from the scan; this report does not resolve or approve them.

## Component inventory

| Component | Version | License | Origin |
| --- | --- | --- | --- |
| aalib | 1.4p5 | LGPL-2.0-or-later | debian |
| abbrev | 1.1.1+~1.1.2 | MIT (Expat) | debian |
| Abseil | 20240722.0 | Apache-2.0 | debian |
| acl (access control list) | 2.3.2 | (LGPL-2.1-or-later AND GPL-2.0-or-later) | debian |
| Acorn | 8.8.1+ds+~cs25.17.7 | MIT | debian |
| adduser | 3.152 | GPL-2.0-or-later | debian |
| Advanced Linux Sound Architecture (ALSA) | 1.2.14 | LGPL-2.1-only | debian |
| adwaita-icon-theme | 48.1 | (LGPL-3.0-only OR Creative Commons Attribution Share Alike 3.0) | debian |
| agent-base | 7.1.102024040606 | MIT (Expat) | debian |
| agronholm/typeguard | 4.3.0 | MIT | fedora |
| ajv | 8.12.0~ds+~2.1.1 | MIT (Expat) | debian, ubuntu |
| ajv-keywords | 5.1.0 | MIT | debian |
| ann | 1.1.2+doc | LGPL-2.1-or-later | debian |
| annotated-doc | 0.0.5 | MIT | pypi |
| ansi-escapes | 5.0.0+really.4.3.1 | MIT (Expat) | debian |
| ansi-regex | 5.0.1 | MIT | debian |
| ansi-styles | 6.2.1 | MIT | debian |
| anyio | 4.15.1 | MIT | pypi |
| anymatch | 3.1.3+~cs4.6.1 | ISC | debian |
| apache/xerces-c | 3.2.4+debian | Apache-2.0 | debian |
| AppArmor: Application Armor | 4.1.0 | GPL-2.0-or-later | debian |
| apt - Advanced Package Tool | 3.0.3 | GPL-2.0-or-later | debian |
| architecture-properties | 0.2.6 | GPL-2.0-or-later | debian |
| are-we-there-yet | 3.0.1+~1.1.0 | ISC | debian |
| argcomplete | 3.6.2 | Apache-2.0 | pypi, github |
| armadillo | 14.2.3 | Apache-2.0 | debian |
| arpack | 3.9.1 | BSD-3-Clause | debian |
| arrify | 2.0.1 | MIT | debian |
| Asio C++ Library | 1.30.2 | BSL-1.0 | conan |
| Async | 3.2.6 | MIT | debian |
| async-each | 1.0.3 | MIT | debian |
| audit-userspace | 4.0.2 | LGPL-2.0-or-later | debian |
| autocommand | 2.2.2 | LGPL-3.0-only | pypi |
| autopep8 | 2.3.1 | MIT (Expat) | pypi |
| avahi | 0.8 | LGPL-2.1-only | debian |
| backports.tarfile | 1.2.0 | MIT | pypi |
| balanced-match | 2.0.0 | MIT | debian |
| base64-js | 1.5.1 | MIT | debian |
| base-files | 13.8+deb13u7 | GPL-2.0-or-later | debian |
| base-passwd | 3.6.7 | GPL-2.0-only | debian |
| Bash | 5.2.37 | GPL-3.0-or-later | debian |
| Bauer stereophonic-to-binaural DSP | 3.1.0 | (MIT AND GPL-2.0-or-later) | debian |
| Berkeley DB | 5.3.28 | (Sleepycat License OR Oracle Berkeley DB License) | debian |
| binary-extensions | v2.2.0 | MIT | debian |
| binutils-aarch64-linux-gnu | 2.44 | GPL-3.0-or-later | debian |
| bitflags | 2.13.1 | (MIT OR Apache-2.0) | crates |
| bitflags | 2.13.2 | (MIT OR Apache-2.0) | crates |
| Bouncy Castle | 1.83.0 | MIT | maven |
| brace-expansion | 2.0.1+~1.1.0 | MIT (Expat) | debian |
| brotli | 1.1.0 | MIT | debian |
| browserify/resolve | 1.22.8+~cs5.34.15 | MIT | debian |
| browserslist | 4.25.0+~cs6.3.22 | MIT (Expat) | debian |
| bsdutils | 2.41.5 | GPL-2.0-or-later | debian |
| build-essential | 12.12 | (GPL-1.0-or-later OR GPL-2.0-or-later) | debian |
| Bzip2 | 1.0.8 | Bzip2 License | debian |
| Bzip2 | 4.0.0 | Bzip2 License | maven |
| cacache | 17.0.3+~cs10.3.7 | ISC | debian |
| ca-certificates | 20250419 | MPL-2.0 | debian |
| ca-certificates | 20250419deb12u1deb11u1 | MPL-2.0 | debian |
| ca-certificates-java | 20240118 | GPL-2.0-or-later | debian |
| Cairo Graphics | 1.18.4 | GPL-3.0-or-later | debian |
| Cairo-Pixman | 0.44.0 | MIT | debian |
| camelcase | 7.0.0 | MIT | debian |
| caniuse-lite | 1.0.30001723 | Creative Commons Attribution 4.0 | debian |
| c-ares | 1.34.5 | MIT (Expat) | debian |
| cargo | 1.85.1 | (MIT OR Apache-2.0) | debian |
| @castor/socks | 2.7.1 | MIT | debian |
| ccache | 4.11.2 | GPL-3.0-or-later | debian |
| cfgv | 3.5.0 | MIT | pypi |
| cfitsio | 4.6.2 | CFITSIO License | debian |
| Chalk | 5.3.0 | MIT | debian |
| CharLS, a JPEG-LS library | 2.4.2 | BSD-3-Clause | debian |
| cheshirekow/cmake\_format | 0.6.13 | GPL-3.0-or-later | pypi |
| chokidar | 3.6.0 | MIT | debian |
| chownr | 2.0.0 | ISC | debian |
| cJSON | 1.7.18 | MIT | debian |
| clang: a C language family frontend for LLVM | 20.1.0 | Apache-2.0 | pypi |
| clang-format | 20.1.8 | Apache-2.0 | pypi |
| Click - Python Command Line Utility | 8.5.0 | BSD-3-Clause | pypi |
| cli-table | 0.3.11+~cs0.13.4 | MIT (Expat) | debian |
| cliui | 7.0.4+repack+1+~cs1.4.2 | ISC | debian |
| Clone | 2.1.2+~2.1.2 | MIT (Expat) | debian |
| CMake | 3.31.7 | BSD-3-Clause | opensuse |
| CMake | v3.31.6 | BSD-3-Clause | debian |
| cmakelang | 0.6.13 | GPL-3.0-or-later | pypi |
| colord | 1.4.7 | GPL-2.0-only | debian |
| colors.js | 1.4.0 | MIT | debian |
| columnify | 1.6.0+~1.5.1 | MIT (Expat) | debian |
| Commander.js | 9.4.1 | MIT | debian |
| Common Unix Printing System (CUPS) | 2.4.10 | Apache-2.0 | debian |
| com.tagtraum:libz | 4.0.0 | zlib License | maven |
| concat-stream | 2.0.0+~2.0.0 | MIT (Expat) | debian |
| console-control-strings | 1.1.0 | ISC | debian |
| convert-source-map | 1.9.0+~1.5.2 | MIT (Expat) | debian |
| convert-source-map | 2.0.0 | MIT | npmjs |
| core-js | 3.33.2 | MIT | debian |
| core-util-is | 1.0.3 | MIT | debian |
| Coverage | 7.10.7 | Apache-2.0 | pypi |
| Coverage | 7.15.4 | Apache-2.0 | pypi |
| cpp | 14.2.0 | (GPL-2.0-or-later AND BSD-4-Clause) | debian |
| cpp-for-host | 14.2.0 | GPL-2.0-or-later | debian |
| cpp-x86-64-linux-gnu | 14.2.0 | GPL-2.0-or-later | debian |
| css-loader | 6.8.1+~cs14.0.17 | MIT (Expat) | debian |
| css-selector-tokenizer | 0.8.0 | MIT | maven |
| css-selector-tokenizer | 0.8.0+~cs4.8.3 | MIT (Expat) | debian |
| curl | 8.14.1 | MIT | debian |
| Cyan4973/xxHash | v0.8.3 | BSD-2-Clause | debian |
| Cyrus SASL | 2.1.28 | BSD-4-Clause | debian |
| DASH | 0.5.12 | MIT | debian |
| data-uri-to-buffer | 6.0.202024040606 | MIT (Expat) | debian |
| datrie | 0.2.13 | (LGPL-2.1-or-later AND GPL-2.0-or-later) | debian |
| dav1d | 1.5.1 | BSD-2-Clause | debian |
| D-Bus | 1.16.2 | (GPL-2.0-or-later AND Academic Free License v2.1 AND MIT (Expat)) | debian |
| dconf | 0.40.0 | (LGPL-2.0-or-later AND GPL-2.0-or-later AND GPL-3.0-or-later) | debian |
| debconf | 1.5.91 | BSD-2-Clause | debian |
| Debian | 0.280 | BSD-2-Clause | debian |
| debian-archive-keyring | 2025.1 | GPL-3.0-or-later | alpine, debian |
| Debian Games | 1.24.2 | (LGPL-2.0-or-later AND MIT AND Apache-2.0 AND BSD-3-Clause AND GPL-2.0-or-later AND NCL Source Code License) | debian |
| debianutils | 5.23.2 | GPL-2.0-or-later | debian |
| debug-js/debug | 4.3.4+~cs4.1.7 | MIT (Expat) | debian |
| Decamelize | 4.0.0 | MIT | debian |
| decompress-response | 6.0.0 | MIT | debian |
| deep-is | 0.1.4 | MIT | debian |
| defaults | 1.0.4+~1.0.3 | MIT (Expat) | debian |
| defined | 1.0.1+~1.0.0 | MIT (Expat) | debian |
| define-properties | 1.2.1+~cs2.2.3 | MIT (Expat) | debian |
| define-property | 2.0.2+really+2.0.2 | MIT (Expat) | debian |
| DejaVu fonts | 2.37 | (Public Domain AND Bitstream Vera Fonts Copyright) | debian |
| delegates | 1.0.0 | MIT | debian |
| Del using Globs | 7.1.0 | MIT | debian |
| detect-secrets | 1.5.0 | Apache-2.0 | pypi |
| DirMngr | 2.4.7 | GPL-3.0-or-later | debian |
| distro | 1.9.0 | Apache-2.0 | pypi |
| doctrine | v3.0.0 | Apache-2.0 | ubuntu, debian |
| Doxygen | 1.9.8 | GPL-2.0-only | debian |
| dpkg | 1.22.22 | GPL-2.0-or-later | opensuse, debian |
| duktape | 2.7.0 | MIT | debian |
| e2fsprogs | 1.47.2 | GPL-2.0-only | debian |
| Editline Library - libedit | 3.1-20250104 | BSD-3-Clause | debian |
| egressai-detect-secrets | 1.5.1 | Apache-2.0 | pypi |
| elfutils | 0.192 | (LGPL-2.1-or-later OR LGPL-3.0-or-later OR GPL-3.0-or-later) | debian |
| encoding | v0.1.13 | MIT | debian |
| err-code-2 | v2.0.3 | MIT | debian |
| errno | 1.0.0 | MIT | ubuntu, debian |
| es-abstract | 1.20.4 | MIT | maven |
| esbuild | 0.28.2 | MIT | npmjs |
| esbuild-wasm | 0.28.2 | MIT | maven |
| escape-string-regexp | v4.0.0 | MIT | debian |
| escodegen | 2.1.0 | BSD-2-Clause | debian |
| eslint | 6.4.0~dfsg+~6.1.9 | MIT (Expat) | debian |
| espree | 9.4.1 | BSD-2-Clause | debian |
| Esprima | 4.0.1+ds+~4.0.3 | BSD-2-Clause | debian |
| esrecurse | v4.3.0 | BSD-2-Clause | debian |
| estraverse | 5.3.0+ds+~5.1.1 | BSD-2-Clause | debian |
| esutils | 2.0.3+~2.0.0 | BSD-2-Clause | debian |
| execa | 8.0.1 | MIT | debian |
| expected | 1.3.1 | CC0-1.0 | fedora |
| facebook/regenerator | 0.15.2+~0.10.8 | MIT (Expat) | debian |
| fastapi\_typer | 0.25.1 | MIT | github |
| fast-deep-equal | v3.1.3 | MIT | ubuntu, debian |
| fast-levenshtein | 2.0.6 | MIT | debian |
| felixge/node-retry | 0.13.1.+~cs2.19.16 | MIT (Expat) | debian |
| FFmpeg | 0.1.6 | GPL-2.0-or-later | debian |
| FFmpeg | 0.164.3108+git31e19f9 | GPL-2.0-or-later | debian |
| FFmpeg | 0.7.4 | (GPL-3.0-only OR GPL-2.0-or-later) | debian |
| FFmpeg | 1.3.7 | (X11 License AND AGPLv3 AND GPL-3.0-or-later) | debian |
| FFmpeg | 2.4.9 | (LGPL-2.1-or-later OR GPL-2.0-or-later) | debian |
| FFmpeg | 7.1.5 | LGPL-2.1-or-later | debian |
| fftw | 3.3.10 | GPL-2.0-or-later | debian |
| file | 5.46 | BSD-3-Clause | debian |
| filesystem\_spec | 2026.7.0 | BSD-3-Clause | pypi |
| filesystem\_spec | 2026.9.0 | BSD-3-Clause | pypi |
| fill-range | 7.1.1+~7.0.3 | MIT (Expat) | debian |
| find-cache-dir | 3.3.2+~3.2.1 | MIT (Expat) | debian |
| find-up | 6.3.0 | MIT | debian |
| findutils | 4.10.0 | GPL-3.0-or-later | debian |
| firejune/repl | 0.1.3 | MIT | centos |
| FLAC - Free Lossless Audio Codec | 1.5.0 | GNU Free Documentation License v1.3 only | debian |
| FlatBuffers | 24.3.25 | Apache-2.0 | pypi |
| FlatBuffers | 25.9.23 | Apache-2.0 | npmjs, crates, pypi, fedora, alpine |
| Flite | 2.2 | (University of Cambridge Software License OR BSD-2-Clause OR Sun Freely Redistributable License OR GPL-2.0-or-later OR Christian Michelsen Research License OR GPL-3.0-or-later) | debian |
| fmtlib/fmt | 10.1.1 | MIT | debian |
| fmtlib/fmt | 12.0.0 | MIT | opensuse |
| fontconfig | 2.15.0 | MIT | debian |
| for-in | 1.0.2 | MIT | debian |
| for-own | 1.0.0 | MIT | debian |
| FRB pipenv | v2026.8.0 | MIT | github |
| fsevents | 2.3.2 | MIT | npmjs |
| fs.realpath | 1.0.0 | ISC | debian |
| fs-write-stream-atomic | 1.0.10 | ISC | debian |
| function-bind | 1.1.2+~cs2.1.14 | MIT | debian |
| FUSE | 3.17.2 | GPL-2.0-or-later | debian |
| g | 14.2.0 | GPL-2.0-or-later | debian |
| g++-14 | 14.2.0 | GPL-3.0-only | debian |
| game-music-emu | 0.6.3 | LGPL-2.0-or-later | debian |
| gcc-14 | 14.2.0 | (LGPL-2.1-or-later OR GPL-1.0-or-later OR GPL-2.0-or-later OR Artistic License 1.0 (Perl) OR GNU Free Documentation License v1.2 only OR GPL-3.0-or-later) | debian |
| gcc-14-base | 14.2.0 | (LGPL-2.1-or-later OR GPL-1.0-or-later OR GPL-2.0-or-later OR Artistic License 1.0 (Perl) OR GNU Free Documentation License v1.2 only OR GPL-3.0-or-later) | debian |
| gcc-x86-64-linux-gnu | 14.2.0 | MIT | debian |
| gcovr | 7.2 | BSD-3-Clause | fedora |
| gcovr | 7.2+really | BSD-3-Clause | debian |
| GD | 2.3.3 | GD License | debian |
| GDAL | 3.10.3 | MIT | debian |
| gdal-bin | 3.10.3 | MIT (Expat) | debian |
| GDCM | v3.0.24 | (zlib License AND LGPL-2.1-or-later AND MIT AND BSD-2-Clause AND BSD-3-Clause AND BSL-1.0) | debian |
| Generic PCI access library | 0.17 | (X11 License OR MIT OR ISC) | debian |
| GEOS - Geometry Engine, Open Source | 3.13.1 | LGPL-2.1-only | debian |
| get-caller-file | 2.0.5+~cs1.1.1 | ISC | debian |
| get-stream | 8.0.1 | MIT | debian |
| giflib A library for processing GIFs | 5.2.2 | MIT | debian |
| GIMP | 0.6.25 | LGPL-3.0-or-later | debian |
| Git | v2.47.3 | GPL-2.0-or-later | debian |
| gitdb | 4.0.12 | BSD-3-Clause | pypi |
| gitpython-developers\_GitPython | 3.1.59 | BSD-3-Clause | pypi |
| GLEW | 2.2.0 | (MIT AND BSD-3-Clause) | debian |
| GLib | 2.84.4 | LGPL-2.1-or-later | debian |
| glib-networking | 2.80.1 | LGPL-2.0-or-later | debian |
| glob-parent | 6.0.2+~5.1.1 | ISC | debian |
| GMP | 6.3.0 | (LGPL-3.0-or-later OR GPL-2.0-or-later) | debian |
| GNOME gsettings-desktop-schemas | 48.0 | LGPL-2.1-or-later | debian |
| GNU Binutils | 2.44 | GPL-2.0-or-later | debian |
| GNU C Library | 2.41 | LGPL-2.1-or-later | debian |
| GNU Compiler Collection | 14.2.0 | (GNU Library General Public License v2 only AND GPL-2.0-only AND GPL-3.0-or-later) | debian |
| GNU Compiler Collection | 14.3.0+git11799 | GPL-3.0-or-later | suse |
| GNU Core Utilities | 9.7 | GPL-3.0-or-later | debian |
| GNU Diff Utilities | v3.10 | GPL-3.0-or-later | debian |
| GNU FriBidi | 1.0.16 | LGPL-2.1-only | debian |
| GNU grep | v3.11 | GPL-3.0-or-later | debian |
| GNU Libtool | 2.5.4 | GPL-2.0-or-later | debian |
| GNU/Linux 1394 AV/C Library | 0.5.4 | LGPL-2.1-or-later | debian |
| GNU MPC | 1.3.1 | LGPL-2.1-only | debian |
| GNU MPFR | 4.2.2 | LGPL-3.0-or-later | debian |
| GNU Patch | 2.8 | GPL-3.0-or-later | debian |
| GnuPG | 2.4.7 | GPL-3.0-only | debian |
| GnuPG Made Easy (GPGME) | 1.24.2 | (LGPL-2.1-or-later AND MIT) | debian |
| GNU sed | 4.9 | GPL-3.0-or-later | debian |
| GNU tar | 1.35 | GPL-3.0-or-later | debian |
| GnuTLS | 3.8.9 | (LGPL-2.1-or-later AND GPL-3.0-or-later) | debian |
| GObject Bindings for Python | 3.50.0 | (LGPL-2.1-or-later OR LGPL-2.1-only) | debian |
| GObject-introspection | 1.84.0 | GPL-2.0-or-later | debian |
| GObject-introspection | 2.84.4 | LGPL-2.1-or-later | debian |
| Google Code Prettify | 2015.12.04 | Apache-2.0 | debian, ubuntu |
| Google C++ Testing Framework | 1.17.0 | BSD-3-Clause | debian, ubuntu |
| googlegson | 2.13.2 | Apache-2.0 | maven |
| google/highway | 1.2.0 | Apache-2.0 | debian |
| google/liblc3 | v1.1.3 | Apache-2.0 | debian |
| Google Mock | 1.17.0 | BSD-3-Clause | fedora |
| google-re2 | 2024-07-02 | BSD-3-Clause | debian |
| google-snappy | 1.2.2 | BSD-3-Clause | debian |
| google/XNNPACK | 0.0~git20241108.4ea82e5 | BSD-3-Clause | debian |
| Gosu | 1.17 | Apache-2.0 | debian |
| Gozala/events | 3.3.0+~3.0.0 | MIT (Expat) | debian |
| gpm | 1.20.7 | (GPL-2.0-or-later AND GPL-3.0-or-later) | debian |
| graphene (graphic types library) | 1.10.8 | MIT (Expat) | debian |
| GraphViz | 2.42.4 | Eclipse Public License 1.0 | debian |
| GSM 06.10 Lossy Speech Compression | 1.0.22 | MIT | debian |
| gst-python1.0 | 1.26.2 | LGPL-2.1-or-later | debian |
| GStreamer | 1.26.2 | (LGPL-2.1-only AND Creative Commons Attribution Share Alike 4.0 International) | debian |
| gstreamer1.0-plugins-bad-doc | 1.26.2 | (X11 License AND LGPL-2.0-or-later AND GPL-2.0-or-later) | debian |
| gstreamer1.0-plugins-ugly-dbg | 1.26.3 | LGPL-2.0-or-later | debian |
| gstreamer1.0-x | 1.26.2 | LGPL-2.0-or-later | debian |
| GSTREAMER PLUGINS-BASE | 1.26.2 | LGPL-2.0-or-later | debian |
| GTK | 2.42.12 | (LGPL-2.1-or-later AND LGPL-2.0-or-later AND CC0-1.0) | debian |
| GTK | 3.24.49 | LGPL-3.0-or-later | debian |
| GTK | 4.18.6 | LGPL-2.1-or-later | debian |
| GUPnP | 1.6.0 | LGPL-2.0-or-later | debian |
| GUPnP | 1.6.4 | LGPL-2.0-or-later | debian |
| GUPnP | 1.6.8 | LGPL-2.1-or-later | debian |
| gyp | 0.16.2 | BSD-3-Clause | debian |
| gzip | 1.13 | GPL-2.0-only | debian |
| h11 | 0.16.0 | MIT | pypi |
| h11 | 0.16.0 | MIT | fedora |
| Handlebars.js | 4.7.6 | MIT | debian |
| Handlebars.js | 4.7.7 | MIT | debian |
| HarfBuzz | 10.2.0 | MIT Modern Variant | debian |
| has-unicode | 2.0.1 | ISC | debian |
| HDF5 | 1.14.5+repack | BSD-3-Clause | debian |
| hf-xet | 1.6.0 | Apache-2.0 | pypi |
| hicolor-icon-theme | 0.18 | GPL-2.0-or-later | debian |
| hiredis | 1.2.0 | BSD-3-Clause | debian |
| hosted-git-info | 6.1.1 | ISC | debian |
| hostname | 3.25 | GPL-2.0-only | debian |
| httpcore | 1.0.9 | BSD-3-Clause | pypi |
| https-proxy-agent | 7.0.402024040606 | MIT (Expat) | debian |
| httpx | 0.28.1 | BSD-3-Clause | pypi, anaconda |
| huggingface\_hub | v1.18.0 | Apache-2.0 | fedora |
| huggingface-hub | 1.18.0 | Apache-2.0 | pypi |
| i965-va-driver | 2.22.0 | MIT (Expat) | debian |
| iarna/gauge | 4.0.4 | ISC | debian |
| ibverbs-providers | 56.1 | (GPL-2.0-only OR Unknown) | debian |
| iconv-lite | 0.6.3 | MIT | debian |
| icss-utils | 5.1.0+~5.1.0 | ISC | debian |
| ICU for C/C++ (ICU4C) | 76.1 | ICU License | debian |
| identify | 2.6.19 | MIT | pypi |
| idna | 3.10 | BSD-3-Clause | pypi |
| idna | 3.20 | BSD-3-Clause | pypi |
| ieee754 | v1.2.1 | BSD-3-Clause | debian |
| iferr | 1.0.2+~1.0.2 | MIT (Expat) | debian |
| importlib\_metadata | 8.7.1 | Apache-2.0 | pypi |
| imurmurhash | 0.1.4 | MIT | debian |
| indent-string | v4.0.0 | MIT | debian |
| inflect.py | 7.3.1 | MIT | pypi, debian |
| inflight | 1.0.6 | ISC | debian |
| Info-Zip | 3.0 | BSD-3-Clause | debian |
| Info-Zip | 6.0 | BSD-3-Clause | debian |
| inherits | v2.0.4 | ISC | debian |
| init-system-helpers | 1.69~deb13u1 | BSD-3-Clause | debian |
| Intel Threading Building Blocks | 2022.1.0 | Basic Proprietary Commercial License | debian |
| interpret | 2.2.0 | MIT | debian |
| IO | 1.55 | (Artistic License 1.0 (Perl) AND GPL-3.0-or-later) | alpine |
| ip-regex | 4.3.0+~4.1.1 | MIT (Expat) | debian |
| isaacs\_node-tar | 6.2.1+~cs7.0.8 | ISC | debian |
| isaacs/once | 1.4.1 | ISC | debian |
| isarray | 2.0.5 | MIT | debian |
| is-arrayish | 0.3.2 | MIT | debian |
| is-binary-path | v2.1.0 | MIT | debian |
| is-buffer | v2.0.5 | MIT | debian |
| isexe | 2.0.0 | ISC | ubuntu |
| isexe | 2.0.0+~2.0.1 | ISC | debian |
| is-extendable | 1.0.1 | MIT | debian |
| isl | 0.27 | MIT | debian |
| is-number | 7.0.0 | MIT | debian |
| isobject | v4.0.0 | MIT | ubuntu, debian |
| iso-codes | 4.18.0 | LGPL-2.1-or-later | debian |
| iso-codes-dev | 4.18.0 | LGPL-2.0-or-later | alpine |
| is-path-cwd | v2.2.0 | MIT | debian |
| is-path-inside | 3.0.3 | MIT | debian |
| is-plain-object | v5.0.0 | MIT | debian |
| is-stream | 3.0.0 | MIT | debian |
| istanbul | 0.4.5+repack10+~cs98.25.59 | BSD-3-Clause | debian |
| is-typed-array | 1.0.0 | MIT | debian |
| is-windows | 1.0.2+~cs1.0.0 | MIT (Expat) | debian |
| jansson | 2.14 | MIT | debian |
| jaraco.context | 5.3.0 | MIT | pypi |
| jaraco.context | 6.0.1 | MIT (Expat) | debian |
| jaraco.functools | 4.0.1 | MIT | pypi |
| jaraco.text | 3.12.1 | MIT | pypi |
| java-common | 0.76 | GPL-2.0-or-later | debian |
| JBIG-KIT lossless image compression library | 2.1 | GPL-2.0-or-later | debian |
| Jest from Facebook | 29.6.2~ds1+~cs73.45.28 | MIT (Expat) | debian |
| jinjapython | 3.1.6 | BSD-3-Clause | debian |
| jmespath.py | 1.1.0 | MIT | pypi |
| jonschlinkert/time-stamp | 2.2.0 | MIT | debian |
| JPEG XL | 0.11.2 | BSD-3-Clause | debian |
| jQuery | 3.6.1 | MIT | debian |
| @jridgewell/resolve-uri | 3.1.2 | MIT | npmjs |
| @jridgewell/sourcemap-codec | 1.6.0 | MIT | npmjs |
| @jridgewell/trace-mapping | 0.3.31 | MIT | npmjs |
| jsdiff | v5.0.0 | BSD-3-Clause | debian |
| jshttp/mime-types | 2.1.35 | MIT | debian |
| json5 | 2.2.3 | MIT | debian |
| json-c | 0.18 | MIT | debian |
| jsoncons | v1.5.0 | BSL-1.0 | ubuntu |
| jsoncons | v1.7.0 | BSL-1.0 | conan |
| JsonCpp | 1.9.6 | MIT | debian |
| JSON for Modern Cpp | 3.12.0 | MIT | github |
| json-glib-devel | 1.10.6 | LGPL-2.1-or-later | suse |
| JSON-GLib - Serialize and Deserialize JSON | 1.10.6 | LGPL-2.1-or-later | debian |
| jsonify | 0.0.1 | Public Domain | debian |
| jsonparse | 1.3.1 | MIT | debian |
| json-schema | 0.4.0+~7.0.11 | Academic Free License v2.1 | debian |
| jsonschema-specifications | 2025.9.1 | MIT | pypi |
| json-schema-traverse | 1.0.0 | MIT | ubuntu, debian |
| json-stable-stringify | 1.0.2+repack1+~cs1.0.34 | MIT (Expat) | debian |
| js-tokens | 8.0.0 | MIT | debian |
| js-yaml | 4.1.0 | MIT | debian |
| keyutils | 1.6.3 | (LGPL-2.0-or-later OR GPL-2.0-or-later) | debian |
| Kimundi/rustc-version-rs | 0.4.1 | (MIT OR Apache-2.0) | crates |
| kind-of | 6.0.3 | MIT | debian |
| krb5/krb5 | 1.21.3 | Krb5-MIT | debian |
| LAME (Lame Ain't an MP3 Encoder) | 3.100 | LGPL-2.0-or-later | debian |
| LAPACK | 3.12.1 | BSD-3-Clause | debian |
| leptonica | 1.84.1 | Leptonica License | debian |
| lerc | 4.0.0 | Apache-2.0 | debian |
| level-zero | 1.20.6 | MIT | debian |
| levn | 0.4.1 | MIT | debian |
| libaec0 | 1.1.3 | BSD-2-Clause | debian |
| libaom | v3.12.1 | BSD-2-Clause | debian |
| libarchive | 3.7.4 | BSD-2-Clause | debian |
| libasan8 | 14.2.0 | BSD-3-Clause | debian |
| libasio-doc | 1.30.2 | BSL-1.0 | ubuntu |
| libass | 0.17.3 | ISC | debian |
| libassuan | 3.0.2 | LGPL-2.1-or-later | debian |
| libasyncns0 | 0.8 | LGPL-2.1-or-later | debian |
| libatomic1 | 14.2.0 | GNU General Public License v3.0 w/GCC Runtime Library exception | debian |
| libavcodec-dev | 7.1.5 | LGPL-2.1-or-later | debian |
| libavformat-dev | 7.1.5 | LGPL-2.1-or-later | debian |
| libavif | 1.2.1 | BSD-2-Clause | debian |
| libavtp | v0.2.0 | BSD-3-Clause | debian |
| libavutil-dev | 7.1.5 | LGPL-2.1-or-later | debian |
| libblosc1 | 1.21.5 | (MIT AND BSD-2-Clause AND BSD-3-Clause) | debian |
| libbluray | 1.3.4 | LGPL-2.0-or-later | debian |
| libbsd | 0.12.2 | BSD-3-Clause | debian |
| libcaca | 0.99\_beta20 | Do What The F\*ck You Want To Public License | debian |
| libcap | 2.75 | (BSD-3-Clause OR GPL-2.0-only) | debian |
| libcap-ng | 0.8.5 | LGPL-2.0-or-later | debian |
| libcdio - Direct CD Reads and Control | 2.2.0 | GPL-3.0-or-later | debian |
| libcdparanoia0 | 3.10.2+debian | GPL-2.0-or-later | debian |
| libchromaprint0 | 1.5.1 | GPL-2.0-or-later | debian |
| libcloudproviders | 0.3.6 | LGPL-3.0-or-later | debian |
| libcodec2-dev | 1.2.0 | LGPL-2.1-only | debian |
| libcomerr2 | 2.1-1.47.2 | MIT | debian |
| libconfig | v1.7.3 | LGPL-2.1-only | debian |
| libcpuinfo0 | 0.0~git20250327.39ea79a | BSD-2-Clause | debian |
| libcrypt1 | 4.4.38 | LGPL-2.1-or-later | debian |
| libcrypt-dev | 4.4.38 | LGPL-2.1-or-later | debian |
| libctf0 | 2.44 | GPL-3.0-or-later | debian |
| libctf-nobfd0 | 2.44 | GPL-3.0-or-later | debian |
| libdc1394 | 2.2.6 | LGPL-3.0-or-later | debian |
| libdc1394-25 | 2.2.6 | (LGPL-2.1-or-later OR GPL-2.0-or-later) | debian |
| libde265 | 1.0.15 | LGPL-3.0-or-later | debian |
| libdecor-0-0 | 0.2.2 | MIT (Expat) | debian |
| libdeflate | 1.23 | MIT | debian |
| libdouble-conversion3 | 3.3.1 | BSD-3-Clause | debian |
| libdrm2 | 2.4.124 | MIT | debian |
| libdrm-dev | 2.4.124 | MIT | debian |
| libdts-dev | 0.0.7 | GPL-2.0-or-later | debian |
| libdv | 1.0.0 | LGPL-2.0-or-later | debian |
| libdvdnav4 | 6.1.1 | GPL-2.0-or-later | debian |
| libdvdread3 | 6.1.3 | GPL-2.0-or-later | debian |
| libebur128-1 | 1.2.6 | MIT (Expat) | debian |
| libegl-dev | 1.7.0 | MIT | debian |
| libepoxy | 1.5.10 | MIT | debian |
| liberror-perl | 0.17030 | (Artistic License 1.0 OR GPL-1.0-or-later) | debian |
| libev | 1.13.4 | MIT | debian |
| libevent | 2.1.13-stable | BSD-3-Clause | debian |
| libexpat | 2.8.3 | MIT | debian |
| libfabric | v2.1.0 | (BSD-2-Clause OR GPL-2.0-only) | debian |
| libffi | 3.4.8 | MIT | debian |
| libffi | 3.8.0 | MIT | alpine |
| libfreeaptx | 0.2.2 | LGPL-2.1-or-later | debian |
| libfreexl1 | 2.0.0 | (Mozilla Public License 1.1 OR LGPL-2.1-only OR GPL-2.0-only OR Unknown) | debian |
| libfyba-dev | 4.1.1 | MIT | debian |
| libgav1-0 | 0.19.0 | Apache-2.0 | debian |
| libgcc-s1 | 14.2.0 | GNU General Public License v2.0 w/GCC Runtime Library exception | debian |
| libgcrypt | 1.11.0 | (LGPL-2.1-or-later OR Public Domain OR LGPL-3.0-or-later OR GPL-2.0-or-later OR GPL-3.0-or-later) | debian |
| libgdbm | 1.24 | GPL-3.0-or-later | debian |
| libgdcm-dev | 3.0.24 | BSD-3-Clause | debian |
| libgeotiff | 1.7.4 | Attribution Assurance License | debian |
| libgit2 | 1.9.0 | GNU General Public License v2.0 w/GCC Runtime Library exception | debian |
| libgl-dev | 1.7.0 | MIT | debian |
| libgles-dev | 1.7.0 | MIT | debian |
| libglibmm-2.4-1 | 2.66.8 | LGPL-2.1-or-later | debian |
| libglx-dev | 1.7.0 | MIT | debian |
| libgpg-error | 1.51 | (LGPL-2.1-or-later AND GPL-2.0-or-later) | debian |
| libgphoto2 | 2.5.31 | LGPL-2.1-only | suse, debian |
| libgstreamer-plugins-base1.0-dev | 1.26.2 | LGPL-2.0-or-later | debian |
| libgudev | 238 | LGPL-2.1-or-later | suse |
| libhdf4 | 4.3.0 | HDF5 License | debian |
| libheif1 | 1.19.8 | LGPL-3.0-or-later | debian |
| libhsa-runtime64-1 | 6.1.2 | University of Illinois/NCSA Open Source License | debian |
| libhttp-parser2.9 | 2.9.4 | (MIT AND libxml2 License) | debian |
| libhwasan0 | 14.2.0 | (LGPL-2.1-or-later OR GPL-1.0-or-later OR GPL-2.0-or-later OR Artistic License 1.0 (Perl) OR GNU Free Documentation License v1.2 only OR GPL-3.0-or-later) | debian |
| libibverbs1 | 56.1 | (BSD-2-Clause OR GPL-2.0-only) | debian |
| libice6 | 1.1.1 | (X11 License OR Open Group License) | debian |
| Libidn2 | 2.3.8 | (LGPL-3.0-only OR Unicode License Agreement - Data Files and Software (2016) OR GPL-2.0-or-later OR GPL-3.0-or-later) | debian |
| libiec61883 | 1.2.0 | (LGPL-2.1-or-later AND X11 License AND FSF Unlimited License AND GPL-2.0-or-later) | debian |
| libinput | 1.28.1 | MIT | debian, suse |
| libinstpatch-1.0-2 | 1.1.6 | LGPL-2.1-only | debian |
| libitm1 | 14.2.0 | GPL-3.0-or-later | debian |
| libjack-jackd2-0 | 1.9.22~dfsg | LGPL-2.1-or-later | debian |
| libjpeg | 2.1.5 | NTP License | debian |
| libjpeg-turbo | 2.1.5 | (zlib License OR Independent JPEG Group License OR BSD-3-Clause) | debian |
| libjson-glib-dev | 1.10.6+ds | LGPL-2.1-or-later | debian |
| libkml2 | 1.3.0 | BSD-3-Clause | debian |
| libksba | 1.6.7 | (LGPL-3.0-only AND GPL-3.0-only AND GPL-2.0-only) | debian |
| liblcms2-utils | 2.16 | MIT | debian |
| libldac | 2.0.2.3+git20200429+ed310a0 | Apache-2.0 | debian |
| liblilv-0-0 | 0.24.26 | ISC | debian |
| libltc | v1.3.2 | LGPL-3.0-only | debian |
| liblua5.4-0 | 5.4.7 | MIT (Expat) | debian |
| libmariadb3 | 11.8.6 | GPL-2.0-only | debian |
| libmd0 | 1.1.0 | BSD-3-Clause | debian |
| libmd4c0 | 0.5.2 | MIT | debian |
| libmodplug | 0.8.9.0 | Alternative Commercial License Available | debian |
| libmpcdec-dev | 0.1~r495 | (zlib License OR LGPL-2.1-or-later OR BSD-3-Clause OR GPL-2.0-or-later) | debian |
| libmpeg2 | 0.5.1 | GPL-2.0-only | debian |
| libmysofa1 | 1.3.3 | BSD-3-Clause | debian |
| libnice | 0.1.22 | (Mozilla Public License 1.1 OR LGPL-2.1-only) | debian |
| libnl-route-3-200 | 3.7.0 | (LGPL-2.1-or-later OR BSD-3-Clause OR GPL-2.0-or-later OR GPL-2.0-only) | debian |
| libnuma1 | 2.0.19 | (LGPL-2.1-or-later OR GPL-2.0-or-later) | debian |
| libogg | 1.3.5 | BSD-3-Clause | debian |
| libopenh264-cisco6 | 2.6.0 | BSD-2-Clause | debian |
| libopenjp2-7 | 2.5.3 | BSD-2-Clause | debian |
| libopenmpt-dev | 0.7.13 | BSD-3-Clause | debian |
| libopenni2-0 | 2.2.0.33 | Apache-2.0 | debian |
| liborc-0.4-0 | 0.4.41 | (BSD-2-Clause OR BSD-3-Clause) | suse, debian |
| libp11-kit0 | 0.25.5 | BSD-3-Clause | debian |
| libpam-systemd | 257.13 | LGPL-2.1-or-later | debian |
| libpciaccess0 | 0.17 | (X11 License OR MIT OR ISC) | debian |
| libpkgconf3 | 1.8.1 | ISC | debian |
| libpmix2 | 5.0.7 | (X11 License OR LGPL-2.0-or-later OR BSD-3-Clause) | debian |
| libpng | 1.6.48 | libpng License | debian |
| libpod-simple-perl | 3.45 | (Artistic License 1.0 OR GPL-1.0-or-later) | debian |
| libproxy | 0.5.9 | LGPL-2.0-or-later | debian |
| libpsl | 0.21.2 | MIT | debian |
| libpsl | 0.21.5 | MIT | oracle\_linux |
| libpsm2 | 11.2.185 | (BSD-3-Clause OR GPL-2.0-only OR Intel Open Source License) | debian |
| libpthreadpool0 | 0.0~git20240616.560c60d | BSD-2-Clause | debian |
| libraw1394 | 2.1.2 | LGPL-3.0-or-later | debian |
| librist4 | 0.2.11 | BSD-2-Clause | debian |
| librsvg | 2.60.0 | LGPL-2.1-or-later | debian |
| librtmp | 2.4+20151223.gitfa8646d.1 | LGPL-2.1-or-later | debian |
| librttopo1 | 1.1.0 | GPL-2.0-or-later | debian |
| libselinux1-dev | 3.8.1 | Public Domain | debian |
| libsemanage1 | 3.8.1 | LGPL-2.1-or-later | debian |
| libsensors-dev | 3.6.2 | LGPL-2.1-or-later | debian |
| libsepol | 3.8.1 | LGPL-2.1-or-later | debian |
| libshine-dev | 3.1.1 | LGPL-2.0-or-later | debian |
| libshout | 2.4.6 | LGPL-3.0-or-later | debian |
| libsidplay | 1.36.60 | GPL-2.0-or-later | debian |
| libsigc++ | 2.12.1 | LGPL-3.0-only | debian |
| libsm6 | 1.2.6 | Open Group License | debian |
| libsndfile | 1.2.2 | LGPL-2.1-only | debian |
| libsodium | 1.0.18 | ISC | debian |
| libsord-dev | 0.16.18 | ISC | debian |
| libsoup3 | 3.6.5 | LGPL-2.0-or-later | debian |
| libsoxr0 | 0.1.3 | LGPL-2.1-or-later | debian |
| libspandsp-dev | 0.0.6 | (LGPL-2.1-or-later OR LGPL-2.1-only OR GPL-2.0-or-later OR GPL-2.0-only) | debian |
| libsrtp | 2.7.0 | BSD-3-Clause | debian |
| libssh | 0.11.5 | (LGPL-2.1-or-later AND GPL-2.0-or-later) | debian |
| libssh2 | 1.11.1 | BSD-3-Clause | debian |
| libstdc++ | 14.2.0 | (LGPL-2.1-or-later OR GPL-1.0-or-later OR GPL-2.0-or-later OR Artistic License 1.0 (Perl) OR GNU Free Documentation License v1.2 only OR GPL-3.0-or-later) | debian |
| libswscale-dev | 7.1.5 | LGPL-2.1-or-later | debian |
| libsystemd0 | 257.13 | LGPL-2.1-or-later | debian |
| libsz2 | 1.1.3 | BSD-2-Clause | debian |
| Libtasn1 | 4.20.0 | LGPL-2.1-or-later | debian |
| libtbb12 | 2022.1.0 | Apache-2.0 | debian |
| libtest2-suite-perl | 0.000162 | (Artistic License 1.0 OR GPL-1.0-or-later) | debian |
| libtest-simple-perl | 1.302199 | (Artistic License 1.0 OR GPL-1.0-or-later) | debian |
| libthai | 0.1.29 | LGPL-2.1-or-later | debian |
| libtheora | 1.2.0~alpha1 | BSD-3-Clause | debian |
| libTIFF | 4.7.0 | libtiff License | debian |
| libtirpc | 1.3.6 | (BSD-3-Clause AND Sun Industry Standards Source License v1.1) | debian |
| libtk8.6 | 8.6.16 | TCL/TK License | debian |
| libudfread0 | 1.1.2 | LGPL-2.1-or-later | debian |
| libunibreak | 6.1 | zlib License | debian |
| libunistring | 1.3 | (LGPL-3.0-or-later OR GPL-3.0-or-later) | debian |
| libunwind | 1.8.1 | MIT | debian |
| libusb | 1.0.28 | LGPL-2.1-only | debian |
| libuv | 1.50.0 | MIT | debian |
| libvdpau-doc | 1.5 | MIT | debian |
| libvisual | 0.4.2 | LGPL-2.1-or-later | debian |
| libvorbis | 1.3.7 | BSD-3-Clause | debian |
| libvpl2 | 2.14.0 | MIT | debian |
| libvpx | 1.15.0 | BSD-3-Clause | debian |
| libvtk9 | 9.3.0 | BSD-3-Clause | debian |
| libvulkan1 | 1.4.309.0 | Apache-2.0 | debian |
| libwebp | 1.5.0 | BSD-3-Clause | debian |
| libwebrtc-audio-processing-dev | 1.3 | BSD-3-Clause | debian |
| libx11-data | 1.8.12 | (X11 License AND MIT) | debian |
| libx11-dev | 1.8.12 | (X11 License OR Open Group License OR MIT OR Silicon Graphics New License OR Diffstat License OR MIT Historical Permission License 3 OR Stichting Mathematisch License OR PythonPlot License OR [base] Historical Permission Notice and Disclaimer (base license) OR libxml2 License) | debian |
| libxau-dev | 1.0.11 | Open Group License | debian |
| libxcomposite1 | 0.4.6 | (MIT OR MIT Historical Permission License 3) | debian |
| libXcursor | 1.2.3 | MIT | debian |
| libxdamage1 | 1.1.6 | MIT Historical Permission License 3 | debian |
| libXdmcp | 1.1.5 | MIT | debian |
| libxext6 | 1.3.4 | (X11 License OR Open Group License OR MIT OR Silicon Graphics New License OR Diffstat License OR MIT Historical Permission License 3 OR PythonPlot License) | debian |
| libxft2 | 2.3.6 | MIT Historical Permission License 3 | debian |
| libxi6 | 1.8.2 | (X11 License OR Open Group License OR MIT OR Stichting Mathematisch License OR MIT v2 with Ad Clause License) | debian |
| libxinerama | 1.1.4 | MIT | debian |
| libxkbcommon | 1.7.0 | MIT | debian |
| libxkbcommon-x11-0 | 1.7.0 | (MIT OR Silicon Graphics New License OR Stichting Mathematisch License) | debian |
| Lib XML++ | 2.42.3 | LGPL-2.1-or-later | debian |
| libxml2 | 2.12.7 | MIT | debian |
| libxml2 | 2.56.2 | LGPL-2.0-or-later | debian |
| libxmu6 | 1.1.3 | (X11 License OR Open Group License OR Stichting Mathematisch License) | debian |
| libxpm4 | 3.5.17 | (X11 License OR MIT) | debian |
| libxrender1 | 0.9.12 | (MIT Historical Permission License 3 OR [base] Historical Permission Notice and Disclaimer (base license)) | debian |
| libxshmfence | 1.3.3 | MIT Historical Permission License 3 | debian |
| libxslt | v1.1.35 | MIT | debian |
| libxss1 | 1.2.3 | X11 License | debian |
| libxt6 | 1.2.1 | (X11 License OR Open Group License OR MIT OR MIT Historical Permission License 3 OR curl License OR Stichting Mathematisch License) | debian |
| libxtst6 | 1.2.5 | (X11 License OR Open Group License OR MIT Historical Permission License 3 OR Stichting Mathematisch License) | debian |
| libxv1 | 1.0.11 | (Stichting Mathematisch License OR Christian Michelsen Research License) | debian |
| libXxf86vm | 1.1.4 | MIT | debian |
| LibYAML | 0.2.5 | MIT | debian |
| libyuv | 0.0.1904.20250204 | BSD-3-Clause | debian |
| libzstd-dev | 1.5.7 | (BSD-3-Clause AND GPL-2.0-only) | debian |
| libzvbi-0.1 | 0.2.44 | GPL-2.0-or-later | debian |
| Lightweight RDF | 0.6.1 | GPL-2.0-or-later | debian |
| Linux Extended Attributes (attr) | 2.5.2 | (LGPL-2.1-or-later AND GPL-2.0-or-later) | debian |
| Linux Kernel | v6.12.107 | GPL-2.0 with Linux Syscall Note | centos, debian |
| Linux-Pam | 1.7.0 | (BSD-3-Clause AND GPL-2.0-or-later) | debian |
| LLVM - Low Level Virtual Machine | 17.0.6 | Apache-2.0 with Exceptions | debian |
| LLVM - Low Level Virtual Machine | 19.1.7 | (University of Illinois/NCSA Open Source License OR Apache-2.0 with Exceptions) | debian |
| locate-path | 7.1.1 | MIT | debian |
| Lodash | 4.17.21 | MIT | debian |
| lodash/babel-plugin-lodash | 3.3.4+~cs2.0.1 | (CC0-1.0 OR MIT (Expat)) | debian |
| Logback | 1.5.21 | (LGPL-2.1-or-later OR Eclipse Public License 1.0) | maven |
| lxml | 5.4.0 | BSD-3-Clause | debian |
| lz4 | 1.10.0 | (BSD-2-Clause AND GPL-2.0-or-later) | debian |
| make | 4.4.1 | GPL-2.0-or-later | ubuntu |
| make-dir | 3.1.0 | MIT | debian |
| MariaDB | 11.8.6 | GPL-2.0-only | debian |
| markdown-it-py | 4.2.0 | MIT | pypi |
| MarkupSafe | 2.1.5 | BSD-3-Clause | debian |
| mathiasbynens/regenerate | 1.4.2 | MIT | debian |
| mawk | 1.3.4.20250131 | GPL-2.0-only | debian |
| mbed TLS | 3.6.6 | (Apache-2.0 OR GPL-2.0-or-later) | debian |
| mdurl | 0.1.2 | MIT | pypi |
| media-types | 13.0.0 | Public Domain | debian |
| memory-fs | 0.5.0+~0.3.3 | MIT (Expat) | debian |
| Merge-Stream | 2.0.0+~1.1.2 | MIT (Expat) | debian |
| Mesa | 25.0.7 | (MIT AND SGI Free Software License B v2.0 AND BSD-3-Clause) | debian |
| meson | 1.7.0 | Apache-2.0 | pypi |
| Meson build system | 1.7.0 | Apache-2.0 | debian |
| micromatch | 4.0.7+~4.0.9 | MIT (Expat) | debian |
| micromatch/braces | 3.0.3+~3.0.5 | MIT (Expat) | debian |
| mime | 3.0.0 | MIT | debian |
| minimatch | 9.0.3 | ISC | debian |
| minimist | 1.2.8+~cs5.3.5 | MIT (Expat) | debian |
| minipass | 5.0.0+~cs10.3.21 | ISC | debian |
| more-itertools | 10.3.0 | MIT | pypi |
| more-itertools | 10.7.0 | MIT | pypi, debian |
| more-itertools | 10.8.0 | MIT | pypi |
| Mozilla Rust - a safe, concurrent, practical language | 1.85.1 | (MIT AND ISC AND Apache-2.0 AND BSD-3-Clause) | debian |
| mpg123 | 1.32.10 | LGPL-2.1-only | debian |
| mpv | 2.11.2 | GPL-2.0-or-later | debian |
| mpv | 2.4.4 | (LGPL-2.1-or-later OR GPL-2.0-or-later) | debian |
| mpv | 7.1.5 | LGPL-2.1-or-later | debian |
| ms.js | 2.1.3+~cs0.7.31 | MIT | debian |
| mtdev | 1.1.7 | MIT | debian |
| munge | 0.5.16 | LGPL-3.0-only | debian |
| mute-stream | 0.0.8+~0.0.1 | MIT (Expat) | debian |
| mvdan-sh | 3.8.0 | BSD-3-Clause | debian |
| mypy | 1.16.1 | MIT | pypi |
| mypy-extensions | 1.1.0 | MIT | pypi |
| MySQL | 5.8+1.1.1 | GPL-2.0-or-later | debian |
| ncurses | 6.5+20250216 | MIT | debian |
| ncurses-bin | 6.5+20250216 | (X11 License OR MIT) | debian |
| Neargye/magic\_enum | v0.9.7 | MIT | debian |
| neo-async | 2.6.2+~cs3.0.0 | MIT (Expat) | debian |
| Neon | 0.34.2 | GPL-2.0-or-later | debian |
| netbase | 6.5 | GPL-2.0-only | debian |
| NetCDF | v4.9.3 | BSD-3-Clause | debian |
| Netlink Protocol Library Suite (libnl) | 3.7.0 | LGPL-2.0-or-later | debian |
| Nettle | 3.10.1 | (LGPL-3.0-or-later OR GPL-2.0-or-later) | debian |
| nghttp2 | v1.64.0 | MIT | debian |
| nghttp3 | v1.8.0 | MIT | debian |
| ngtcp2 | v1.11.0 | MIT | debian |
| Ninja (build system) | 1.12.1 | Apache-2.0 | debian |
| node-ampproject-remapping | 2.2.0+~cs5.15.37 | Apache-2.0 | debian |
| node-aproba | 2.0.0 | ISC | debian |
| node-archy | 1.0.0 | MIT | debian |
| node-babel7 | 7.20.15+ds1+~cs214.269.168 | MIT (Expat) | debian |
| node-babel-plugin-add-module-exports | 1.0.4 | MIT (Expat) | debian |
| node-babel-polyfills | 0.3.3020220913+ds1 | MIT (Expat) | debian |
| node-babel-polyfills | 0.4.1020220913+ds1 | MIT (Expat) | debian |
| node-babel-polyfills | 0.6.0020220913+ds1 | MIT (Expat) | debian |
| node-builtins | 5.0.1 | MIT (Expat) | debian |
| nodeca-argparse | 2.0.1 | Python Software Foundation License 2.0 | ubuntu, debian |
| node-ci-info | 4.0.0+~cs1.1.0 | MIT (Expat) | debian |
| node-cjs-module-lexer | 1.2.3 | MIT (Expat) | debian |
| node-clone-deep | 4.0.1+~cs7.0.2 | MIT (Expat) | debian |
| node-color-convert | 2.0.1+~cs2.0.0 | MIT (Expat) | debian |
| node-color-name | 1.1.4+~1.1.1 | MIT | debian |
| node-commondir | 1.0.1+~1.0.0 | MIT (Expat) | debian |
| node-copy-concurrently | 1.0.5 | ISC | debian |
| node-corepack | 0.24.0 | MIT (Expat) | debian |
| node-debbundle-es-to-primitive | 1.2.1+~cs9.7.25 | MIT (Expat) | debian |
| node-deep-equal | 2.2.3+~cs43.15.94 | MIT (Expat) | debian |
| node-electron-to-chromium | 1.5.166 | ISC | debian |
| node-enhanced-resolve | 5.15.0 | MIT (Expat) | debian |
| nodeenv | 1.10.0 | BSD-3-Clause | pypi |
| nodeenv | 1.9.1 | BSD-3-Clause | debian |
| node-error-ex | 1.3.2 | MIT | debian |
| node-es6-error | 4.1.1 | MIT (Expat) | debian |
| node-es-abstract | 1.20.4+~cs26.27.47 | MIT (Expat) | debian |
| node-eslint-scope | 7.1.1+~3.7.3 | BSD-2-Clause | debian |
| node-eslint-scope | 7.1.1+~3.7.4 | BSD-2-Clause | debian |
| node-eslint-utils | 3.0.0 | MIT (Expat) | debian |
| node-eslint-visitor-keys | 3.3.0+~1.0.0 | Apache-2.0 | debian |
| node-es-module-lexer | 1.1.0 | MIT (Expat) | debian |
| node-esquery | 1.4.2~ds | BSD-3-Clause | debian |
| node-fancy-log | 1.3.3+~cs1.3.1 | MIT (Expat) | debian |
| node-fetch | 3.3.2+~cs11.4.11 | MIT | debian |
| node-file-entry-cache | 6.0.1+~3.0.4+~2.0.0+~1.0.0+~2.0.1 | MIT (Expat) | debian |
| node-flat-cache | 3.0.4~6.0.0+~3.0.4+~2.0.0+~1.0.0+~2.0.1 | MIT (Expat) | debian |
| node-flat-cache | 3.0.4~6.0.1+~3.0.4+~2.0.0+~1.0.0+~2.0.1 | MIT (Expat) | debian |
| node-flatted | 3.2.7~ds | ISC | debian |
| node-foreground-child | 3.1.1 | ISC | debian |
| node-fs-readdir-recursive | 1.1.0+~1.1.0 | MIT (Expat) | debian |
| node-functional-red-black-tree | 1.0.1+20181105 | MIT (Expat) | debian |
| node-glob | 8.1.0+~cs8.5.15 | ISC | debian |
| node-graceful-fs | 4.2.10 | ISC | debian |
| node-gyp | 11.1.0+~5.0.0 | MIT (Expat) | debian |
| node-has-flag | 5.0.1 | MIT (Expat) | debian |
| node-http-proxy-agent | 7.0.202024040606 | MIT (Expat) | debian |
| node-ignore | 5.2.1 | MIT | debian |
| node-ip | 2.0.1+~1.1.3 | MIT (Expat) | debian |
| node-is-descriptor | 3.0.0 | MIT (Expat) | debian |
| node-is-extglob | 2.1.1 | MIT | debian |
| node-is-glob | 4.0.3 | MIT (Expat) | debian |
| node-is-plain-obj | 3.0.0 | MIT (Expat) | debian |
| Node.js | 20.19.2 | MIT | debian |
| nodejs Deprecate | 2.0.0 | MIT | ubuntu, debian |
| node-jsesc | 3.0.2 | MIT (Expat) | ubuntu |
| node-jsesc | 3.0.2+~3.0.1 | MIT (Expat) | debian |
| node-json-buffer | 3.0.1+~3.0.0 | MIT (Expat) | debian |
| node-json-parse-better-errors | 1.0.2+~cs3.3.1 | MIT (Expat) | debian |
| nodejs/string\_decoder | 1.3.0 | MIT | debian |
| node-llhttp | 7.3.0 | MIT (Expat) | debian |
| node-loader-runner | 4.3.0 | MIT | debian |
| node-lodash-packages | 4.17.21 | MIT (Expat) | debian |
| node-lowercase-keys | 2.0.0 | MIT (Expat) | debian |
| node-lru-cache | 10.0.1 | ISC | debian |
| node-mimic-fn | 4.0.0 | MIT (Expat) | debian |
| node-mimic-response | 3.1.0 | MIT (Expat) | debian |
| node-mkdirp | 2.1.6+~cs5.2.1 | MIT (Expat) | debian |
| node-move-concurrently | 1.0.1 | ISC | debian |
| node-npm-bundled | 2.0.1 | ISC | debian |
| node-optimist | 0.6.1+~0.0.30 | MIT | debian |
| node-path-dirname | 1.0.2 | MIT (Expat) | debian |
| node-path-exists | 5.0.0 | MIT (Expat) | debian |
| node-path-is-absolute | 2.0.0 | MIT (Expat) | debian |
| node-p-cancelable | 2.1.1 | MIT (Expat) | debian |
| node-picocolors | 1.0.0 | ISC | debian |
| node-p-limit | 4.0.0+~cs4.0.0 | MIT (Expat) | debian |
| node-p-map | 4.0.0+~3.1.0+~3.0.1 | MIT (Expat) | debian |
| node-postcss | 8.4.49+~cs9.2.32 | MIT (Expat) | debian |
| node-postcss-modules-values | 4.0.0+~4.0.0 | ISC | debian |
| node-prelude-ls | 1.2.1 | MIT (Expat) | ubuntu, debian |
| node-progress | 2.0.3 | MIT | ubuntu, debian |
| node-promise-retry | 2.0.1 | MIT | debian |
| node-punycode | 2.3.1+~2.1.4 | MIT (Expat) | debian |
| node-quick-lru | 6.1.1 | MIT (Expat) | debian |
| node-randombytes | 2.1.0+~2.0.0 | MIT | debian |
| node-read-pkg | 5.2.0 | MIT (Expat) | ubuntu, debian |
| node-regexpu-core | 5.2.1 | MIT (Expat) | debian |
| node-regexpu-core | 5.2.2 | MIT (Expat) | debian |
| node-regjsgen | 0.7.1+ds | MIT (Expat) | debian |
| node-resolve-cwd | 3.0.0 | MIT (Expat) | debian |
| node-resolve-from | 5.0.0+~3.1.0+~3.3.0+~2.0.0 | MIT (Expat) | debian |
| node-resumer | 0.0.0 | MIT | debian |
| node-run-queue | 2.0.0 | ISC | debian |
| node-semver | 7.6.1+~7.5.8 | ISC | debian |
| node-source-map-support | 0.5.21+ds+~0.5.10 | MIT (Expat) | debian |
| node-spdx-exceptions | 2.3.0 | Creative Commons Attribution 3.0 | debian |
| node-strip-eof | 3.0.0 | MIT (Expat) | debian |
| node-tapable | 2.2.1 | MIT (Expat) | debian |
| node-tape | 5.6.1+~cs8.20.19 | MIT (Expat) | debian |
| node-text-table | 0.2.0 | MIT | debian |
| node-to-regex-range | 5.0.1 | MIT (Expat) | debian |
| node-tslib | 2.4.1 | Apache-2.0 | debian |
| node-unicode-canonical-property-names-ecmascript | 2.0.0 | MIT (Expat) | debian |
| node-unicode-match-property-ecmascript | 2.0.0 | MIT (Expat) | debian |
| node-unicode-match-property-value-ecmascript | 2.1.0+ds | MIT (Expat) | debian |
| node-unicode-property-aliases-ecmascript | 2.1.0+ds | MIT (Expat) | debian |
| node-wcwidth.js | 1.0.2 | MIT (Expat) | debian |
| node-webassemblyjs | 1.11.4 | MIT | debian |
| node-webpack-sources | 3.2.3+~3.2.0 | MIT | debian |
| node-write | 2.0.0~6.0.1+~3.0.4+~2.0.0+~1.0.0+~2.0.1 | MIT (Expat) | debian |
| nopt | v5.0.0 | ISC | ubuntu, debian |
| norm | 1.5.9 | (Lawrence Berkeley Lab License AND BSD-2-Clause AND BSD-3-Clause AND BSD-4-Clause) | debian |
| normalize-package-data | 4.0.1+~2.4.1 | BSD-2-Clause | debian |
| normalize-path | 3.0.0+~3.0.0 | MIT (Expat) | debian |
| npm-cli | 9.2.0 | Artistic License 2.0 | debian |
| npm ini | 3.0.1 | ISC | debian |
| npmlog | 7.0.1+~4.1.4 | BSD-2-Clause | debian |
| npm-package-arg | 10.0.0+~3.0.0 | ISC | debian |
| npm-run-path | 5.1.0+~4.0.0 | MIT | debian |
| nPth | 1.8 | LGPL-2.1-or-later | debian |
| NSPR | 4.36 | MPL-2.0 | debian |
| NSS | 3.110 | MPL-2.0 | debian |
| NumPy | 2.4.2 | BSD-3-Clause | pypi, alpine |
| NVIDIA/libglvnd | 1.7.0 | MIT | debian |
| nvidia-settings | 535.171.04 | GPL-2.0-only | debian |
| object-assign | 4.1.1 | MIT | debian |
| object-inspect | 1.12.2+~cs1.8.1 | MIT (Expat) | debian |
| ocl-icd-libopencl1 | 2.3.3 | BSD-2-Clause | debian |
| Ohloh | 1.4.2 | (Mozilla Public License 1.1 AND GNU Lesser General Public License v2.0 with Exceptions AND GPL-2.0-or-later) | debian |
| oneapi-src/oneDNN | 3.7.2 | Apache-2.0 | debian |
| onnx | 1.17.0 | Apache-2.0 | debian |
| onnxruntime | 1.21.0 | MIT | debian |
| onnxruntime-dev | 1.24.3 | MIT | alpine |
| openai | 2.44.0 | Apache-2.0 | pypi |
| openai-agents | 0.17.7 | MIT | pypi |
| Open Computer Vision Library (OpenCV) | 4.10.0 | BSD-3-Clause | debian |
| OpenEXR | 3.1.11 | BSD-3-Clause | opensuse |
| OpenEXR | 3.1.13 | BSD-3-Clause | debian |
| OpenFabrics Enterprise Distribution - OFED | 56.1 | (BSD-2-Clause OR GPL-2.0-only) | debian |
| openfec | 1.4.2.11 | CeCILL-C Free Software License Agreement | debian |
| Open Geospatial Data Interface | 4.1.1 | BSD-3-Clause | debian |
| openjdk-21 | 21.0.12.1+1 | GNU General Public License v2.0 w/Classpath exception | debian |
| openjdk-25 | 25.0.4.1+1 | GNU General Public License v2.0 w/Classpath exception | debian |
| openjdk25-demos | 25.0.4\_p7 | Unknown | alpine |
| OpenLDAP | 2.6.10 | Open LDAP Public License v2.8 | debian |
| OpenSSL | 3.5.7 | Apache-2.0 | debian |
| optionator | 0.9.1 | MIT | debian, ubuntu |
| opus codec | 1.5.2 | BSD-3-Clause | debian |
| osenv | 0.1.5+~0.1.1 | BSD-2-Clause | debian |
| Packaging | 24.2 | (BSD-2-Clause OR Apache-2.0) | pypi |
| Packaging | 25.0 | (Apache-2.0 AND BSD-3-Clause) | pypi, debian |
| Packaging | 26.0 | (BSD-2-Clause OR Apache-2.0) | pypi |
| Packaging | 26.3 | (BSD-2-Clause AND Apache-2.0) | pypi |
| pandoc | 3.1.11.1 | GPL-2.0-or-later | debian |
| Pango | 1.56.3 | GNU Library General Public License v2 only | debian |
| parse-json | 5.2.0+~cs5.1.7 | MIT (Expat) | debian |
| path-is-inside | 1.0.2+~1.0.0 | (Do What The F\*ck You Want To Public License OR MIT (Expat)) | debian |
| pathspec | 1.1.1 | MPL-2.0 | pypi |
| path-type | 4.0.0 | MIT | debian |
| PCRE2 | 10.46 | BSD-3-Clause | debian |
| pcscd | 2.3.3 | BSD-3-Clause | debian |
| Perl | 5.40.1 | (Artistic License 1.0 OR GPL-2.0-only) | debian, alpine |
| Perl 5 Encode | 3.20 | (Artistic License 2.0 AND Unicode Character Database Terms Of Use) | cpan |
| pify | 5.0.0+~cs5.0.1 | MIT (Expat) | debian |
| PillowPython | 12.3.0 | CMU License | pypi |
| pinentry-curses | 1.3.1 | GPL-2.0-or-later | debian |
| pip | 25.1.1 | MIT | pypi, debian |
| pip | 26.0.1 | MIT | pypi |
| pip | 26.2.1 | MIT | pypi, fedora |
| pipewire | 1.4.2 | MIT (Expat) | debian |
| pkgconf | 1.8.1 | ISC | debian |
| pkg-config | 1.8.1 | ISC | debian |
| pkg-dir | 5.0.0 | MIT | debian |
| PlantUML | 1.2026.2 | MIT | maven |
| platformdirs | 4.11.11 | MIT | pypi |
| platformdirs | 4.2.2 | BSD-3-Clause | pypi |
| platformdirs | 4.3.7 | MIT (Expat) | debian |
| platformdirs | 4.3.8 | MIT | pypi |
| platformdirs | 4.4.0 | MIT | pypi |
| playwright | 1.61.0 | Apache-2.0 | npmjs |
| @playwright/test | 1.61.0 | Apache-2.0 | npmjs |
| p-locate | 6.0.0 | MIT | debian |
| pngquant | 2.18.0 | GPL-3.0-or-later | debian |
| Poppler | 25.03.0 | (GPL-3.0-only OR GPL-2.0-only) | debian |
| Portable Hardware Locality (hwloc) | 2.12.0 | (BSD-3-Clause OR GPL-3.0-or-later) | debian |
| Portable Hardware Locality (hwloc) | 5.0.7 | (LGPL-2.1-or-later OR LGPL-2.0-or-later OR Apache-2.0 OR BSD-3-Clause) | debian |
| postcss-modules-extract-imports-2 | v3.0.0 | ISC | ubuntu, debian |
| postcss-value-parser | 4.2.0 | MIT | debian |
| PostgreSQL Database Server | 17.11 | PostgreSQL License | debian |
| pre-commit | 4.2.0 | MIT | debian |
| pre-commit/pre-commit | 3.5.0 | MIT | pypi |
| pre-commit/pre-commit | 4.2.0 | MIT | pypi |
| process-nextick-args | v2.0.1 | MIT | debian |
| Procps | 4.0.4 | (LGPL-2.0-or-later AND GPL-2.0-or-later) | debian |
| PROJ | 9.6.0 | MIT | debian |
| promise-inflight | 1.0.1+~1.0.0 | ISC | debian |
| promzard | 0.3.0 | ISC | debian |
| Protobuf | 3.21.12 | BSD-3-Clause | debian |
| prr | 1.0.1 | MIT | debian |
| psf-requests | 2.33.0 | Apache-2.0 | pypi, opensuse |
| PulseAudio | 17.0 | LGPL-3.0-or-later | debian |
| pycapio | 0.0.2 | MIT | pypi |
| pycodestyle | 2.12.1 | MIT | pypi |
| pydantic | 2.13.4 | MIT | pypi |
| py-filelock | 3.18.0 | The Unlicense | debian |
| py-filelock | 3.32.7 | MIT | pypi |
| Pyflakes | 3.3.2 | MIT | pypi |
| Pygments - Python syntax highlighter | 2.18.0 | BSD-2-Clause | pypi |
| Pygments - Python syntax highlighter | 2.21.0 | BSD-2-Clause | pypi |
| pypi/setuptools | 78.1.1 | MIT | ubuntu, debian, fedora |
| pypi/setuptools | 80.9.0 | MIT | pypi, alpine |
| pypi/setuptools | 82.0.1 | MIT | pypi, fedora, anaconda |
| pypi/setuptools | 84.0.0 | MIT | pypi, fedora |
| pyright | 1.1.408 | MIT | pypi |
| python3-cfgv | 3.4.0 | MIT | debian |
| python3-charset-normalizer | 3.5.1 | MIT | pypi |
| python3-httpcore | 1.0.8 | BSD-3-Clause | oracle\_linux |
| python3-identify | 2.6.10 | MIT | debian |
| python3-identify | 2.6.19 | MIT | debian |
| python3-imath | 3.1.12 | BSD-3-Clause | debian |
| python3-importlib-metadata | 8.0.0 | Apache-2.0 | debian |
| python3-jaraco.functools | 4.1.0 | MIT (Expat) | debian |
| python3-jaraco.text | 4.0.0 | MIT (Expat) | ubuntu, debian |
| python3-mdurl | 0.1.2 | MIT | oracle\_linux |
| python3-pyproject-hooks | 1.2.0 | MIT | fedora |
| python3-referencing | 0.37.0+~2025.1.1 | MIT | debian |
| python3-resolvelib | 1.1.0 | ISC | debian |
| python3-zipp | 3.19.2 | MIT (Expat) | debian |
| python3-zipp | 3.21.0 | MIT (Expat) | oracle\_linux, debian |
| python-attrs | 26.1.0 | MIT | pypi |
| python-autocommand | 2.2.2 | LGPL-3.0-or-later | debian, oracle\_linux |
| python-certifi | 2026.7.22 | MPL-2.0 | pypi |
| python-colorlog | 6.9.0 | MIT | pypi, debian |
| python-discovery | 1.5.2 | MIT | pypi |
| python-discovery | 1.6.1 | MIT | pypi |
| python-distlib | 0.3.9 | Python Software Foundation License 2.0 | pypi, debian, fedora |
| python-distlib | 0.4.3 | Python Software Foundation License 2.0 | pypi |
| pythonjedi | 0.20.0 | MIT | fedora |
| python-jsonschema | 4.20.0 | MIT | pypi |
| python-jsonschema | 4.26.0 | MIT | pypi |
| Python programming language | 3.13.5 | Python Software Foundation License 2.0 | debian |
| Python programming language | 3.14.7 | Python Software Foundation License 2.0 | photon, debian, anaconda |
| python-pygments | 2.18.0 | BSD-2-Clause | debian |
| pythonrs | 0.1.2 | MIT | crates |
| Python six | 1.17.0 | MIT | pypi, alpine |
| python-typeguard | 4.4.2 | MIT (Expat) | debian |
| python-typing-extensions | 4.12.2 | Python Software Foundation License 2.0 | pypi |
| python-typing-extensions | 4.13.2 | Python Software Foundation License 2.0 | debian |
| python-typing-extensions | 4.16.0 | Python Software Foundation License 2.0 | pypi |
| python-virtualenv | 20.31.2 | MIT (Expat) | debian |
| python-wheel | 0.45.1 | MIT | pypi, anaconda |
| python-wheel | 0.46.1 | MIT (Expat) | pypi, debian |
| python-wheel | 0.46.3 | MIT | pypi |
| PyYAML | 6.0.2 | MIT | debian |
| PyYAML | 6.0.3 | MIT | pypi |
| qhull | 2020.2 | Qhull License | debian |
| qrencode | 4.1.1 | LGPL-2.1-or-later | debian |
| Qt | 5.15.15 | (LGPL-3.0-only OR GPL-2.0-only) | debian |
| RabbitMQ | 0.15.0 | MIT | debian |
| Raptor RDF Parser Library | 2.0.16 | GPL-3.0-or-later | debian |
| rav1e | 0.7.1 | BSD-2-Clause | debian |
| rbenv/rbenv | v1.2.0 | MIT | github |
| rbenv/ruby-build | v20230330 | MIT | github |
| rdfjs/N3.js | 1.16.3+~1.2.3+~1.10.4 | MIT (Expat) | debian |
| read | 1.0.7 | BSD-2-Clause | debian |
| readable-stream | 3.6.0+~cs3.0.0 | MIT (Expat) | debian |
| readdirp | 3.6.0 | MIT | debian |
| Readline | 8.2 | GPL-3.0-or-later | debian |
| read-package-json | 5.0.2+~2.0.0 | ISC | debian |
| rechoir | 0.8.0+~0.6.1 | MIT (Expat) | debian |
| redding/assert | 2.0.0+~cs3.9.8 | MIT (Expat) | debian |
| referencing | 0.37.0 | MIT | pypi |
| regenerate-unicode-properties | 10.1.0 | MIT | debian |
| regexpp | 3.2.0 | MIT (Expat) | debian |
| regjsparser | 0.9.1 | BSD-2-Clause | debian |
| repeat-string | 1.6.1+repack | MIT (Expat) | debian |
| require-directory | 2.1.1+~2.1.2 | MIT (Expat) | debian |
| RHash | v1.4.5 | BSD Zero Clause License | debian |
| rimraf | 3.0.2 | ISC | debian |
| rocm-compilersupport | 6.0+git20231212.4510c28 | University of Illinois/NCSA Open Source License | debian |
| rocm-hipamd | 5.7.1 | MIT | debian |
| roc-toolkit | 0.4.0 | MPL-2.0 | debian |
| roct-thunk-interface | 6.2.4+ds | MIT (Expat) | debian |
| rpds | 2026.6.3 | MIT | pypi |
| ruby-build | 20240917 | MIT (Expat) | github |
| ruby-build | v20230330 | MIT | github |
| RustPython | 0.5.0 | MIT | crates |
| rust-rustc-version | 0.4.1 | (MIT OR Apache-2.0) | debian |
| rusty\_link | 0.4.5 | GPL-2.0-or-later | crates |
| safe-buffer | 5.2.1+~cs2.1.2 | MIT (Expat) | debian |
| Sage: Open Source Mathematics Software | 10.10.beta10 | Unknown | github |
| Sage: Open Source Mathematics Software | 10.8.rc0 | GPL-3.0-only | github |
| Sage: Open Source Mathematics Software | 10.9.rc2 | Unknown | github |
| samccone/chrome-trace-event | 1.0.3 | MIT | debian |
| sbc | 2.1 | GPL-2.0-or-later | debian |
| schema-utils | 4.2.0 | MIT | debian |
| Seccomp Library | 2.6.0 | LGPL-2.1-only | debian |
| Secret Rabbit Code | 0.2.2 | BSD-2-Clause | debian |
| Secure Reliable Transport (SRT) | 1.5.4 | MPL-2.0 | debian |
| Security-enhanced Linux | 3.8.1 | Public Domain | debian, suse |
| semver | 1.0.28 | (MIT OR Apache-2.0) | crates |
| serd | 0.32.4 | ISC | debian |
| serialize-javascript | 6.0.2 | BSD-3-Clause | debian |
| set-blocking | 2.0.0 | ISC | debian |
| set-immediate-shim | 2.0.0 | MIT | debian |
| Shadow Tool Suite | 4.16.0-2+really2.41.5 | GPL-2.0-or-later | debian |
| Shadow Tool Suite | 4.17.4 | (BSD-3-Clause AND GPL-2.0-or-later) | debian |
| shared-mime-info | 2.4 | GPL-2.0-or-later | debian |
| shebang-command | 2.0.0 | MIT | debian |
| shebang-regex | 3.0.0 | MIT | debian |
| shellcheck | v0.10.0 | GPL-3.0-only | debian |
| shellingham | 1.5.4 | ISC | pypi |
| shellingham | 1.5.4 | ISC | suse |
| SILGraphite: rendering non-roman scripts | 1.3.14 | (LGPL-2.1-only AND GPL-2.0-only AND MPL-2.0) | debian |
| Simple DirectMedia Layer | 2.32.4 | zlib License | debian |
| sindresorhus/globals | 13.23.0 | MIT | debian |
| sindresorhus/globby | 13.1.3+~cs16.25.40 | MIT (Expat) | debian |
| sindresorhus/got | 11.8.5+~cs58.13.36 | MIT (Expat) | debian |
| sindresorhus/slash | 4.0.0 | MIT | debian |
| sindresorhus/supports-color | 8.1.1+~8.1.1 | MIT (Expat) | debian |
| S-Lang | 2.3.3 | GPL-2.0-or-later | debian |
| slice-ansi | 5.0.0+~cs9.0.0 | MIT (Expat) | debian |
| smmap | 5.0.3 | BSD-3-Clause | pypi |
| socket++ | 1.12.13+git20131030.5d039ba | Public Domain | debian |
| SonarQube Scanner | 8.0.1.6346 | LGPL-3.0-or-later | maven |
| SonarScanner Java Library | 4.0.0.1577 | LGPL-3.0-or-later | maven |
| SonarScanner Java Library - Batch Interface | 4.1.2.1663 | LGPL-3.0-or-later | maven |
| soundtouch | 2.4.0 | LGPL-2.1-or-later | debian |
| source-list-map | 2.0.1 | MIT | debian, ubuntu |
| source-map | 0.7.0 | BSD-3-Clause | debian |
| spatialite-bin | 5.1.0 | GPL-3.0-or-later | debian |
| spdx-correct.js | v3.1.1 | Apache-2.0 | debian |
| spdx-expression-parse.js | 3.0.1+~3.0.1 | (Creative Commons Attribution 3.0 AND MIT (Expat)) | debian |
| spdx-license-ids | 3.0.12 | CC0-1.0 | debian |
| Speex | 1.2.1 | BSD-3-Clause | debian |
| Sphinx-Python Documentation Generator | 8.1.3 | BSD-3-Clause | debian |
| sprintf.js | 1.1.2+ds1+~1.1.2 | BSD-3-Clause | debian |
| SQLite | 3.46.1 | Public Domain | debian |
| sqv | 1.3.0 | LGPL-2.0-or-later | debian |
| sratom | 0.6.18 | MIT | debian |
| ssri | 9.0.1 | ISC | debian |
| standard-pkg-resources | 1.0.0 | MIT | pypi |
| stb | 20260802-snapshot-2c980bb5 | Unknown | github |
| steve-o/openpgm | 5.3.128 | LGPL-2.1-only | debian |
| streamich/memfs | 3.4.12+~cs1.0.3 | Public Domain | debian |
| string-width | 4.2.3+~cs13.2.3 | MIT (Expat) | debian |
| Strip ANSI | 6.0.1 | MIT | debian |
| strip-bom | 4.0.0 | MIT | debian |
| strip-json-comments | 4.0.0 | MIT | debian |
| sudo | 1.9.16p2 | (zlib License AND BSD-2-Clause AND ISC AND BSD-3-Clause) | debian |
| SVT-AV1 | 2.3.0 | BSD 3-clause Clear License | debian |
| sysprof | 48.0 | (LGPL-2.0-or-later AND BSD-3-Clause AND Public Domain AND LGPL-3.0-or-later AND GPL-2.0-or-later AND BSD-2-Clause Plus Patent License AND GPL-3.0-or-later) | debian |
| sysprof-capture-devel | 48.1 | BSD-2-Clause Plus Patent License | fedora |
| systemd | 238 | GPL-2.0-only | debian |
| systemd | 257.13 | GPL-2.0-only | debian |
| sysvinit | 3.14 | GPL-2.0-or-later | debian |
| tabrindle/envinfo | 7.11.0+~cs14.3.0 | MIT (Expat) | debian |
| TagLib | v2.0.2 | LGPL-2.1-or-later | debian |
| tapjs/signal-exit | 4.1.0 | ISC | debian |
| TartanLlama/expected | v1.3.1 | CC0-1.0 | github |
| tcl8.6 | 8.6.16 | (Ayam License OR BSD-4-Clause OR TCL/TK License) | debian |
| terser-js/terser | v5.38.0 | BSD-2-Clause | debian |
| Tesseract OCR | 5.5.0 | Apache-2.0 | debian |
| Test::Harness | 3.48 | (GPL-1.0-or-later OR Artistic License 1.0 (Perl)) | oracle\_linux |
| Textualize\_rich | 15.0.0 | MIT | pypi |
| The FreeType Project | 2.13.3 | (Freetype Project License OR GPL-2.0-only) | debian |
| The GNU Triangulated Surface Library | 0.7.6+darcs121130 | LGPL-2.0-or-later | debian |
| The MJPEG/Linux square | 2.1.0+debian | GPL-2.0-or-later | debian |
| thkukuk/rpcsvc-proto | 1.4 | LGPL-2.0-or-later | photon |
| thkukuk/rpcsvc-proto | 1.4.3 | BSD-3-Clause | debian |
| through | 2.3.8+~cs0.0.30 | (MIT OR Apache-2.0) | debian |
| Time Zone Database | 2026c | Public Domain | debian |
| timgm6mb-soundfont | 1.3 | GPL-2.0-only | debian |
| to-fast-properties | 3.0.1 | MIT | debian |
| tomli | 2.0.1 | MIT | pypi |
| tomli | 2.2.1 | MIT | pypi |
| tomli | 2.4.0 | MIT | pypi |
| tqdm | 4.70.1 | (MIT AND MPL-2.0) | pypi |
| truststore | 0.10.4 | MIT | pypi |
| TwoLAME | 0.4.0 | LGPL-2.0-or-later | debian |
| type-check | 0.4.0 | MIT | ubuntu, debian |
| TypedArray | 0.0.7 | MIT | debian |
| typedarray-to-buffer | 4.0.0 | MIT | debian |
| typeguard | 4.3.0 | MIT | pypi |
| typer | 0.25.1 | MIT | pypi |
| typescript | 5.9.2 | Apache-2.0 | maven |
| TypeScript | 5.9.2 | Apache-2.0 | npmjs |
| typeshed | 0.0~git20260204.516eed0 | Apache-2.0 | ubuntu |
| typeshed | 1.6.6.20251220 | Apache-2.0 | pypi |
| typeshed | 2.18.0.20251008 | Apache-2.0 | pypi |
| @types/istanbul-lib-coverage | 2.0.6 | MIT | npmjs |
| types-protobuf | 6.32.1.20251210 | Apache-2.0 | pypi |
| types-PyYAML | 6.0.12.20250516 | Apache-2.0 | pypi |
| ucx | 1.18.1 | BSD-3-Clause | debian |
| udev | 238 | LGPL-2.1-or-later | debian |
| udev | 257.13 | GPL-2.0-only | debian |
| UML doclet | 2.0.16 | Apache-2.0 | maven |
| Underscore.js | 1.13.4 | MIT | debian |
| unique-filename | 1.1.1 | ISC | ubuntu, debian |
| unixODBC | 2.3.12 | LGPL-2.1-only | debian |
| uri-js | 4.4.0 | BSD-2-Clause | debian |
| uriparser | 0.9.8 | BSD-3-Clause | debian |
| urllib3 | 2.8.0 | MIT | pypi |
| util | 0.12.5+~1.0.10 | MIT (Expat) | debian |
| util-deprecate | 1.0.2 | MIT | debian |
| util-linux | 2.41.5 | GPL-2.0-only | debian |
| uuid | 8.3.2+~8.3.4 | MIT (Expat) | debian |
| v4l-utils | 1.30.1 | GPL-2.0-only | debian |
| v8flags | 3.2.0+~3.1.1 | MIT (Expat) | debian |
| v8-to-istanbul | 9.3.0 | ISC | npmjs |
| Valgrind Instrumentation Framework | 3.24.0 | (bzip2 and libbzip2 License v1.0.6 AND X11 License AND FSF All Permissive License AND FSF Unlimited License (with License Retention) AND FSF Unlimited License AND CMU Mach License AND GPL-2.0-or-later AND FSF Unlimited License (With License Retention and Warranty Disclaimer)) | debian |
| validate-npm-package-license | 3.0.4 | Apache-2.0 | debian |
| validate-npm-package-name | 5.0.0+~4.0.0 | ISC | debian |
| virtualenv | 20.31.2 | MIT | fedora |
| virtualenv | 21.9.0 | MIT | pypi |
| vo-aacenc | 0.1.3 | Apache-2.0 | debian |
| vo-amrwbenc | 0.1.3 | Apache-2.0 | debian |
| vulture | 2.14 | MIT | pypi |
| watchpack | 2.4.0+~cs2.8.1 | MIT (Expat) | debian |
| WavPack | 5.8.1 | BSD-3-Clause | debian |
| Wayland | 1.23.1 | MIT | debian |
| Webpack | 5.97.1 | MIT | debian |
| WebSocket++ | 0.8.2 | BSD-3-Clause | debian |
| Wget | 1.25.0 | GPL-3.0-or-later | debian |
| which | 2.0.2+~cs1.3.2 | ISC | debian |
| wide-align | 1.1.3 | ISC | debian |
| WildMIDI | 0.4.3 | GPL-3.0-or-later | debian |
| wordwrap | 1.0.0 | MIT | debian |
| wrap-ansi | 8.0.1+~8.0.1 | MIT (Expat) | debian |
| wrappy | 1.0.2 | ISC | debian |
| write-file-atomic | 4.0.2+~4.0.0 | ISC | debian |
| x11-common | 7.7+24+deb13u1 | (X11 License OR MIT OR GPL-2.0-or-later OR libxml2 License) | debian |
| x11proto-dev | 2024.1 | MIT | debian |
| x265 | 4.1 | GPL-2.0-or-later | debian |
| Xapian | 1.4.29 | MIT | debian |
| XCB | 0.3.10 | MIT | debian |
| XCB | 0.4.0 | MIT | debian |
| XCB | 0.4.1 | MIT | debian |
| XCB | 0.4.2 | MIT | debian |
| XCB | 1.17.0 | X11 License | debian |
| xdm | 1.0.16 | (X11 License OR Open Group License OR MIT OR Xmlproc License OR MIT Historical Permission License 3 OR Historic Permission Notice and Disclaimer OR Stichting Mathematisch License) | debian |
| xf86-input-wacom | 2.14.0 | Historic Permission Notice and Disclaimer | almalinux, debian |
| xkeyboard-config | 2.42 | MIT | debian |
| xolox/python-negotiator | 0.6.3+~0.6.1 | MIT (Expat) | debian |
| x.org\_lib | 1.4.0 | MIT | photon |
| xorg-sgml-doctools | 1.11 | (MIT OR Christian Michelsen Research License) | almalinux, debian |
| xorg-x11 | 1.0.11 | Open Group License | debian |
| xorg-x11 | 1.5.4 | MIT Historical Permission License 3 | debian |
| xorg-x11 | 1.8.12 | (X11 License OR Open Group License OR MIT OR Silicon Graphics New License OR Diffstat License OR MIT Historical Permission License 3 OR Stichting Mathematisch License OR PythonPlot License OR [base] Historical Permission Notice and Disclaimer (base license) OR libxml2 License) | debian |
| xorg-x11 | 6.0.0 | (MIT OR MIT Historical Permission License 3) | debian |
| xtend | 4.0.2 | MIT | debian |
| xtrans | 1.4.0 | (X11 License AND Open Group License AND MIT AND Christian Michelsen Research License) | debian |
| XZ Utils | 5.8.1 | (BSD Zero Clause License AND LGPL-2.1-only AND GPL-2.0-or-later AND GPL-3.0-or-later) | debian |
| y18n | 5.0.8+~5.0.0 | ISC | debian |
| yajl-c | 2.1.0 | BSD-3-Clause | debian |
| yallist | 4.0.0+~4.0.1 | ISC | debian |
| yargs | 16.2.0+~16.0.4 | MIT (Expat) | debian |
| yargs-parser | 21.1.1+~21.0.0 | ISC | debian |
| Yelp/detect-secrets | v1.5.0 | Apache-2.0 | oracle\_linux |
| yhirose/cpp-httplib | 0.54.1 | MIT | github |
| Z3 | 4.13.3 | MIT | debian |
| ZBar bar code reader | 0.23.93 | LGPL-2.1-only | debian |
| ZeroMQ | 4.3.5 | MPL-2.0 | debian |
| zertosh/v8-compile-cache | v2.4.0 | MIT | debian |
| zipp | 3.23.0 | MIT | pypi |
| zix | 0.6.2 | ISC | debian |
| zlib | 1.3 | zlib License | debian |
| zstd | 1.5.7 | BSD-3-Clause | debian |
| zxing-cpp | 2.3.0 | Apache-2.0 | debian |
