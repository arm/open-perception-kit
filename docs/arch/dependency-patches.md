---
sidebar_position: 7
sidebar_label: Dependency patches
---
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

# Dependency patches

Declare patches beside the dependency's pinned version, URL and archive checksum:

```json
"patches": [
  {
    "file": "patches/example/buffer-lifetime.patch",
    "reason": "Keep buffers alive until processing finishes."
  }
]
```

**The `patches` list defines application order, from top to bottom.** Each patch
sees the preceding patches' changes. Filenames are not sorted.

Paths are relative to `requirements/`. Add an `upstream` URL when there is a PR or
Gerrit change to track. Without a list, the source is unchanged. Export focused
patches with `git format-patch` to preserve authorship.

To integrate a source installer, call `prepare_sources(lock, work_dir)` to download, verify, extract and
patch sources. Missing or conflicting patches stop preparation. After a successful
build, `stage_patches(patches, notices)` saves the diffs and an ordered `patches.json`.
The `notices/patches` directory must be new, so removed patches cannot be carried over.

When packaging, use `validate_patch_files(notices, patch_inventory(lock))` to check the
saved files against the selected patches. Without the expected inventory, validation
only checks the saved files against their own receipt.

Check patch application without compiling (replace `example.json` with your descriptor):

```sh
python3 scripts/private/dependency_patches.py requirements/example.json
```

Recheck patches when updating a source pin; remove fixes already present upstream.
Changing a patch requires rebuilding the dependency. Successful application does
not prove build or runtime correctness. Packaged diffs retain the same visibility
restrictions as their source; internal patches belong in private artifacts.
