<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

# Third-party components

List of software components used by OPK, including development tools and
components in distributed artifacts. A row does not by itself mean that its
package ships in every artifact; see the [release notice locations](public/licensing.md#notices-in-release-artifacts)
and check the built artifacts before publication.

| Component | Version or source pin | License | Origin |
| --- | --- | --- | --- |
| [annotated-doc](https://github.com/fastapi/annotated-doc) | [0.0.5](https://pypi.org/project/annotated-doc/0.0.5/) | MIT | PyPI |
| [anyio](https://github.com/agronholm/anyio) | [4.15.1](https://pypi.org/project/anyio/4.15.1/) | MIT | PyPI |
| [argcomplete](https://github.com/kislyuk/argcomplete) | [3.6.2](https://pypi.org/project/argcomplete/3.6.2/) | Apache-2.0 | PyPI |
| [Asio](https://github.com/chriskohlhoff/asio) | [1.30.2](https://github.com/chriskohlhoff/asio/tree/asio-1-30-2) | BSL-1.0 | Conan |
| [autocommand](https://github.com/Lucretiel/autocommand) | [2.2.2](https://pypi.org/project/autocommand/2.2.2/) | LGPL-3.0-only | PyPI |
| [autopep8](https://github.com/hhatto/autopep8) | [2.3.2](https://pypi.org/project/autopep8/2.3.2/) | MIT (Expat) | PyPI |
| [bitflags](https://github.com/bitflags/bitflags) | [2.13.1](https://docs.rs/crate/bitflags/2.13.1) | MIT OR Apache-2.0 | crates.io |
| [cfgv](https://github.com/asottile/cfgv) | [3.5.0](https://pypi.org/project/cfgv/3.5.0/) | MIT | PyPI |
| [cmake-format](https://github.com/cheshirekow/cmake_format) | [0.6.13](https://pypi.org/project/cmake-format/0.6.13/) | GPL-3.0-or-later | PyPI |
| [clang-tidy](https://github.com/ssciwr/clang-tidy-wheel) | [22.1.8](https://pypi.org/project/clang-tidy/22.1.8/) | Apache-2.0 | PyPI |
| [clang-format](https://github.com/ssciwr/clang-format-wheel) | [23.1.1](https://pypi.org/project/clang-format/23.1.1/) | Apache-2.0 | PyPI |
| [Click](https://github.com/pallets/click) | [8.5.0](https://pypi.org/project/click/8.5.0/) | BSD-3-Clause | PyPI |
| [cmakelang](https://github.com/cheshirekow/cmake_format) | [0.6.13](https://pypi.org/project/cmakelang/0.6.13/) | GPL-3.0-or-later | PyPI |
| [convert-source-map](https://github.com/thlorenz/convert-source-map) | [2.0.0](https://www.npmjs.com/package/convert-source-map/v/2.0.0) | MIT | npm |
| [detect-secrets](https://github.com/Yelp/detect-secrets) | [1.5.0](https://pypi.org/project/detect-secrets/1.5.0/) | Apache-2.0 | PyPI |
| [esbuild-wasm](https://github.com/evanw/esbuild) | [0.28.2](https://www.npmjs.com/package/esbuild-wasm/v/0.28.2) | MIT | npm |
| [ExecuTorch](https://github.com/pytorch/executorch) | 1.3.1-2 | BSD-3-Clause and bundled terms | Debian package |
| [expected](https://github.com/TartanLlama/expected) | [1.3.1](https://github.com/TartanLlama/expected/tree/v1.3.1) | CC0-1.0 | Fedora |
| [fsspec](https://github.com/fsspec/filesystem_spec) | [2026.7.0](https://pypi.org/project/fsspec/2026.7.0/) | BSD-3-Clause | PyPI |
| [FlatBuffers](https://github.com/google/flatbuffers) | [25.9.23](https://github.com/google/flatbuffers/tree/v25.9.23) (SDK/build), [25.12.19](https://pypi.org/project/flatbuffers/25.12.19/) (host Python) | Apache-2.0 | npm, crates.io, PyPI |
| [fmt](https://github.com/fmtlib/fmt) | [12.0.0](https://github.com/fmtlib/fmt/tree/12.0.0) | MIT | openSUSE |
| [Font Awesome Free](https://github.com/FortAwesome/Font-Awesome) | [6.5.2](https://www.npmjs.com/package/@fortawesome/fontawesome-free/v/6.5.2) | MIT (CSS), OFL-1.1 (fonts) | vendored assets |
| [fsevents](https://github.com/fsevents/fsevents) | [2.3.2](https://www.npmjs.com/package/fsevents/v/2.3.2) | MIT | npm |
| [gcovr](https://github.com/gcovr/gcovr) | [7.2](https://pypi.org/project/gcovr/7.2/) | BSD-3-Clause | PyPI |
| [gitdb](https://github.com/gitpython-developers/gitdb) | [4.0.12](https://pypi.org/project/gitdb/4.0.12/) | BSD-3-Clause | PyPI |
| [GitPython](https://github.com/gitpython-developers/GitPython) | [3.2.0](https://pypi.org/project/GitPython/3.2.0/) | BSD-3-Clause | PyPI |
| [PyGObject](https://gitlab.gnome.org/GNOME/pygobject) | [3.50.0-4+b1](https://packages.debian.org/trixie/python3-gi) | LGPL-2.1-or-later AND MIT (selected files; [Debian copyright](https://metadata.ftp-master.debian.org/changelogs/main/p/pygobject/pygobject_3.50.0-4_copyright)) | Debian (Cairn image) |
| [PyGObject](https://gitlab.gnome.org/GNOME/pygobject) | [3.58.0](https://pypi.org/project/PyGObject/3.58.0/) | LGPL-2.1-or-later AND [MIT (selected files)](https://github.com/GNOME/pygobject/blob/3.58.0/gi/pygi-property.c) | PyPI (build requirements) |
| [GoogleTest](https://github.com/google/googletest) | [1.17.0](https://github.com/google/googletest/tree/v1.17.0) | BSD-3-Clause | Debian, Ubuntu |
| [Google Mock](https://github.com/google/googletest) | [1.17.0](https://github.com/google/googletest/tree/v1.17.0) | BSD-3-Clause | Fedora |
| [h11](https://github.com/python-hyper/h11) | [0.16.0](https://pypi.org/project/h11/0.16.0/) | MIT | PyPI |
| [hf-xet](https://github.com/huggingface/xet-core) | [1.6.0](https://pypi.org/project/hf-xet/1.6.0/) | Apache-2.0 | PyPI |
| [httpcore](https://github.com/encode/httpcore) | [1.0.9](https://pypi.org/project/httpcore/1.0.9/) | BSD-3-Clause | PyPI |
| [httpx](https://github.com/encode/httpx) | [0.28.1](https://pypi.org/project/httpx/0.28.1/) | BSD-3-Clause | PyPI |
| [huggingface-hub](https://github.com/huggingface/huggingface_hub) | [2.0.0](https://pypi.org/project/huggingface-hub/2.0.0/) | Apache-2.0 | PyPI |
| [identify](https://github.com/pre-commit/identify) | [2.6.19](https://pypi.org/project/identify/2.6.19/) | MIT | PyPI |
| [idna](https://github.com/kjd/idna) | [3.19](https://pypi.org/project/idna/3.19/) | BSD-3-Clause | PyPI |
| [inflect](https://github.com/jaraco/inflect) | [7.3.1](https://pypi.org/project/inflect/7.3.1/) | MIT | PyPI |
| [jaraco.context](https://github.com/jaraco/jaraco.context) | [6.0.1](https://pypi.org/project/jaraco.context/6.0.1/) | MIT | PyPI |
| [jaraco.functools](https://github.com/jaraco/jaraco.functools) | [4.1.0](https://pypi.org/project/jaraco.functools/4.1.0/) | MIT | PyPI |
| [jaraco.text](https://github.com/jaraco/jaraco.text) | [4.0.0](https://pypi.org/project/jaraco.text/4.0.0/) | MIT | PyPI |
| [Jinja2](https://github.com/pallets/jinja) | [3.1.6](https://pypi.org/project/Jinja2/3.1.6/) | BSD-3-Clause | PyPI |
| [jmespath](https://github.com/jmespath/jmespath.py) | [1.1.0](https://pypi.org/project/jmespath/1.1.0/) | MIT | PyPI |
| [@jridgewell/resolve-uri](https://github.com/jridgewell/resolve-uri) | [3.1.2](https://www.npmjs.com/package/@jridgewell/resolve-uri/v/3.1.2) | MIT | npm |
| [@jridgewell/sourcemap-codec](https://github.com/jridgewell/sourcemaps/tree/main/packages/sourcemap-codec) | [1.6.0](https://www.npmjs.com/package/@jridgewell/sourcemap-codec/v/1.6.0) | MIT | npm |
| [@jridgewell/trace-mapping](https://github.com/jridgewell/sourcemaps/tree/main/packages/trace-mapping) | [0.3.31](https://www.npmjs.com/package/@jridgewell/trace-mapping/v/0.3.31) | MIT | npm |
| [jsoncons](https://github.com/danielaparker/jsoncons) | [cb54cdc3134a62634466bf7bcd24f1a906f4ef25](https://github.com/danielaparker/jsoncons/tree/cb54cdc3134a62634466bf7bcd24f1a906f4ef25) | BSL-1.0 | GitHub |
| [nlohmann/json](https://github.com/nlohmann/json) | [3.12.0](https://github.com/nlohmann/json/tree/v3.12.0) | MIT | GitHub |
| [jsonschema-specifications](https://github.com/python-jsonschema/jsonschema-specifications) | [2025.9.1](https://pypi.org/project/jsonschema-specifications/2025.9.1/) | MIT | PyPI |
| [rustc\_version](https://github.com/djc/rustc-version-rs) | [0.4.1](https://docs.rs/crate/rustc_version/0.4.1) | MIT OR Apache-2.0 | crates.io |
| [libasio-doc](https://github.com/chriskohlhoff/asio) | [1.30.2](https://github.com/chriskohlhoff/asio/tree/asio-1-30-2) | BSL-1.0 | Ubuntu |
| [lxml](https://github.com/lxml/lxml) | [5.4.0](https://pypi.org/project/lxml/5.4.0/) | BSD-3-Clause | PyPI |
| [markdown-it-py](https://github.com/executablebooks/markdown-it-py) | [4.2.0](https://pypi.org/project/markdown-it-py/4.2.0/) | MIT | PyPI |
| [MarkupSafe](https://github.com/pallets/markupsafe) | [2.1.5](https://pypi.org/project/MarkupSafe/2.1.5/) | BSD-3-Clause | PyPI |
| [mdurl](https://github.com/executablebooks/mdurl) | [0.1.2](https://pypi.org/project/mdurl/0.1.2/) | MIT | PyPI |
| [meson](https://github.com/mesonbuild/meson) | [1.12.1](https://pypi.org/project/meson/1.12.1/) | Apache-2.0 | PyPI |
| [more-itertools](https://github.com/more-itertools/more-itertools) | [10.7.0](https://pypi.org/project/more-itertools/10.7.0/) | MIT | PyPI |
| [magic\_enum](https://github.com/Neargye/magic_enum) | [v0.9.7](https://github.com/Neargye/magic_enum/tree/v0.9.7) | MIT | Debian |
| [nodeenv](https://github.com/ekalinin/nodeenv) | [1.10.0](https://pypi.org/project/nodeenv/1.10.0/) | BSD-3-Clause | PyPI |
| [NumPy](https://github.com/numpy/numpy) | [2.4.2](https://pypi.org/project/numpy/2.4.2/) (build), [2.5.3](https://pypi.org/project/numpy/2.5.3/) (host Python) | BSD-3-Clause and [wheel terms](https://github.com/numpy/numpy/blob/v2.4.2/pyproject.toml), including GPL-3.0-or-later WITH GCC-exception-3.1 (libgfortran) and LGPL-2.1-or-later (x86_64 libquadmath) | PyPI |
| [onnx](https://github.com/onnx/onnx) | [1.22.0](https://pypi.org/project/onnx/1.22.0/) | Apache-2.0 | PyPI |
| [onnxruntime](https://github.com/microsoft/onnxruntime) | [1.24.4](https://github.com/microsoft/onnxruntime/releases/tag/v1.24.4) | MIT and bundled terms | GitHub release |
| [openai](https://github.com/openai/openai-python) | [2.44.0](https://pypi.org/project/openai/2.44.0/) | Apache-2.0 | PyPI |
| [openai-agents](https://github.com/openai/openai-agents-python) | [0.17.7](https://pypi.org/project/openai-agents/0.17.7/) | MIT | PyPI |
| [packaging](https://github.com/pypa/packaging) | [26.3](https://pypi.org/project/packaging/26.3/) | BSD-2-Clause AND Apache-2.0 | PyPI |
| [Pillow](https://github.com/python-pillow/Pillow) | [12.3.0](https://pypi.org/project/pillow/12.3.0/) | CMU License | PyPI |
| [pip](https://github.com/pypa/pip) | [25.1.1](https://pypi.org/project/pip/25.1.1/) | MIT | PyPI |
| [platformdirs](https://github.com/tox-dev/platformdirs) | [4.11.9](https://pypi.org/project/platformdirs/4.11.9/) | MIT | PyPI |
| [Playwright / playwright-core](https://github.com/microsoft/playwright) | [1.61.0](https://github.com/microsoft/playwright/tree/v1.61.0) | Apache-2.0 | npm |
| [@playwright/test](https://github.com/microsoft/playwright) | [1.61.0](https://www.npmjs.com/package/@playwright/test/v/1.61.0) | Apache-2.0 | npm |
| [pre-commit](https://github.com/pre-commit/pre-commit) | [4.6.2](https://pypi.org/project/pre-commit/4.6.2/) | MIT | PyPI |
| [Requests](https://github.com/psf/requests) | [2.33.0](https://pypi.org/project/requests/2.33.0/) | Apache-2.0 | PyPI |
| [pycapio](https://github.com/High-Performance-IO/PyCAPIO) | [0.0.2](https://pypi.org/project/pycapio/0.0.2/) | MIT | PyPI |
| [pycairo](https://github.com/pygobject/pycairo) | [1.29.1](https://pypi.org/project/pycairo/1.29.1/) | [LGPL-2.1-only OR MPL-1.1](https://github.com/pygobject/pycairo/blob/v1.29.1/COPYING) | PyPI (build requirements) |
| [pycodestyle](https://github.com/PyCQA/pycodestyle) | [2.15.0](https://pypi.org/project/pycodestyle/2.15.0/) | MIT | PyPI |
| [pydantic](https://github.com/pydantic/pydantic) | [2.13.4](https://pypi.org/project/pydantic/2.13.4/) | MIT | PyPI |
| [filelock](https://github.com/tox-dev/filelock) | [3.32.7](https://pypi.org/project/filelock/3.32.7/) | MIT | PyPI |
| [Pyflakes](https://github.com/PyCQA/pyflakes) | [4.0.0](https://pypi.org/project/pyflakes/4.0.0/) | MIT | PyPI |
| [Pygments](https://github.com/pygments/pygments) | [2.21.0](https://pypi.org/project/Pygments/2.21.0/) | BSD-2-Clause | PyPI |
| [setuptools](https://github.com/pypa/setuptools) | [80.9.0](https://pypi.org/project/setuptools/80.9.0/) | MIT | PyPI |
| [pyright](https://github.com/RobertCraigie/pyright-python) | [1.1.408](https://pypi.org/project/pyright/1.1.408/) | MIT | PyPI |
| [charset-normalizer](https://github.com/jawah/charset_normalizer) | [3.5.1](https://pypi.org/project/charset-normalizer/3.5.1/) | MIT | PyPI |
| [attrs](https://github.com/python-attrs/attrs) | [26.1.0](https://pypi.org/project/attrs/26.1.0/) | MIT | PyPI |
| [certifi](https://github.com/certifi/python-certifi) | [2026.7.22](https://pypi.org/project/certifi/2026.7.22/) | MPL-2.0 | PyPI |
| [colorlog](https://github.com/borntyping/python-colorlog) | [6.9.0](https://pypi.org/project/colorlog/6.9.0/) | MIT | PyPI |
| [python-discovery](https://github.com/tox-dev/python-discovery) | [1.6.0](https://pypi.org/project/python-discovery/1.6.0/) | MIT | PyPI |
| [distlib](https://github.com/pypa/distlib) | [0.4.3](https://pypi.org/project/distlib/0.4.3/) | Python Software Foundation 2.0 | PyPI |
| [jsonschema](https://github.com/python-jsonschema/jsonschema) | [4.20.0](https://pypi.org/project/jsonschema/4.20.0/) | MIT | PyPI |
| [jsonschema](https://github.com/python-jsonschema/jsonschema) | [4.26.0](https://pypi.org/project/jsonschema/4.26.0/) | MIT | PyPI |
| [six](https://github.com/benjaminp/six) | [1.17.0](https://pypi.org/project/six/1.17.0/) | MIT | PyPI |
| [typing-extensions](https://github.com/python/typing_extensions) | [4.16.0](https://pypi.org/project/typing-extensions/4.16.0/) | Python Software Foundation 2.0 | PyPI |
| [wheel](https://github.com/pypa/wheel) | [0.45.1](https://pypi.org/project/wheel/0.45.1/) | MIT (Expat) | PyPI |
| [PyYAML](https://github.com/yaml/pyyaml) | [6.0.3](https://pypi.org/project/PyYAML/6.0.3/) | MIT | PyPI |
| [referencing](https://github.com/python-jsonschema/referencing) | [0.37.0](https://pypi.org/project/referencing/0.37.0/) | MIT | PyPI |
| [rpds-py](https://github.com/crate-py/rpds) | [2026.6.3](https://pypi.org/project/rpds-py/2026.6.3/) | MIT | PyPI |
| [rusty\_link](https://github.com/anzbert/rusty_link) | [0.4.5](https://docs.rs/crate/rusty_link/0.4.5) | GPL-2.0-or-later | crates.io |
| [semver](https://github.com/dtolnay/semver) | [1.0.28](https://docs.rs/crate/semver/1.0.28) | MIT OR Apache-2.0 | crates.io |
| [shellingham](https://github.com/sarugaku/shellingham) | [1.5.4](https://pypi.org/project/shellingham/1.5.4/) | ISC | PyPI |
| [smmap](https://github.com/gitpython-developers/smmap) | [5.0.3](https://pypi.org/project/smmap/5.0.3/) | BSD-3-Clause | PyPI |
| [stb](https://github.com/nothings/stb) | [20260802-snapshot-2c980bb5](https://github.com/nothings/stb/tree/2c980bb59875b0d32144a71867fbdebb2f77cd20) | MIT | GitHub |
| [TartanLlama/expected](https://github.com/TartanLlama/expected) | [v1.3.1](https://github.com/TartanLlama/expected/tree/v1.3.1) | CC0-1.0 | GitHub |
| [rich](https://github.com/Textualize/rich) | [15.0.0](https://pypi.org/project/rich/15.0.0/) | MIT | PyPI |
| [tqdm](https://github.com/tqdm/tqdm) | [4.70.1](https://pypi.org/project/tqdm/4.70.1/) | MIT AND MPL-2.0 | PyPI |
| [truststore](https://github.com/sethmlarson/truststore) | [0.10.4](https://pypi.org/project/truststore/0.10.4/) | MIT | PyPI |
| [typeguard](https://github.com/agronholm/typeguard) | [4.4.2](https://pypi.org/project/typeguard/4.4.2/) | MIT | PyPI |
| [typer](https://github.com/fastapi/typer) | [0.25.1](https://pypi.org/project/typer/0.25.1/) | MIT | PyPI |
| [TypeScript](https://github.com/microsoft/TypeScript) | [5.9.2](https://www.npmjs.com/package/typescript/v/5.9.2) | Apache-2.0 | npm |
| [@types/istanbul-lib-coverage](https://github.com/DefinitelyTyped/DefinitelyTyped/tree/master/types/istanbul-lib-coverage) | [2.0.6](https://www.npmjs.com/package/@types/istanbul-lib-coverage/v/2.0.6) | MIT | npm |
| [ultralytics](https://github.com/ultralytics/ultralytics) | [8.4.90](https://pypi.org/project/ultralytics/8.4.90/) | AGPLv3 | PyPI |
| [urllib3](https://github.com/urllib3/urllib3) | [2.8.0](https://pypi.org/project/urllib3/2.8.0/) | MIT | PyPI |
| [v8-to-istanbul](https://github.com/istanbuljs/v8-to-istanbul) | [9.3.0](https://www.npmjs.com/package/v8-to-istanbul/v/9.3.0) | ISC | npm |
| [virtualenv](https://github.com/pypa/virtualenv) | [21.7.10](https://pypi.org/project/virtualenv/21.7.10/) | MIT | PyPI |
| [WebSocket++](https://github.com/zaphoyd/websocketpp) | [0.8.2](https://github.com/zaphoyd/websocketpp/tree/0.8.2) | BSD-3-Clause | Debian |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | [0.56.0](https://github.com/yhirose/cpp-httplib/tree/v0.56.0) | MIT | GitHub |
| [zipp](https://github.com/jaraco/zipp) | [3.21.0](https://pypi.org/project/zipp/3.21.0/) | MIT | PyPI |

The published architecture archives and SDK packages do not bundle
cmake-format, cmakelang, autocommand, pathspec, certifi, tqdm, ultralytics,
`rusty_link`, PyGObject or pycairo. The PyPI PyGObject and pycairo entries are
build requirements; Cairn instead uses Debian's `python3-gi`, whose copyright
file remains in the image at `/usr/share/doc/python3-gi/copyright` and whose
LGPL text is at `/usr/share/common-licenses/LGPL-2`. The Python SDK permits
`flatbuffers>=24.3.25,<26.0.0`; 24.3.25 is a lower bound, while 25.9.23 is
the checked-in runtime pin.
