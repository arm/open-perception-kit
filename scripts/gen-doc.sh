#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
requested_project_root="${OPK_PROJECT_ROOT:-$SCRIPT_DIR/..}"
if [[ "$requested_project_root" != /* ]]; then
    echo "OPK_PROJECT_ROOT must be an absolute path: $requested_project_root" >&2
    exit 2
fi
if [[ ! -d "$requested_project_root" ]]; then
    echo "OPK project root does not exist: $requested_project_root" >&2
    exit 2
fi
OPK_PROJECT_ROOT="$(cd -- "$requested_project_root" && pwd -P)"
export OPK_PROJECT_ROOT
if [[ ! -f "$OPK_PROJECT_ROOT/development/Doxyfile" ||
      ! -d "$OPK_PROJECT_ROOT/docs/public" ]]; then
    echo "OPK_PROJECT_ROOT is not an OPK checkout: $OPK_PROJECT_ROOT" >&2
    exit 2
fi

DEVELOPMENT_DIR="$OPK_PROJECT_ROOT/development"
SRC_DIR="$OPK_PROJECT_ROOT/docs/public"
OUT_DIR="$OPK_PROJECT_ROOT/docs/html"
HTML_CONTENT_DIR="$OUT_DIR/public"
TMP_DIR="$(mktemp -d)"

cleanup() {
    rm -rf "$TMP_DIR"
}

trap cleanup EXIT

echo "Building HTML docs..."
echo "Source: $SRC_DIR"
echo "Output: $OUT_DIR"

echo "Running Doxygen API docs (using development/Doxyfile)..."
DOXY_OUT_DIR="$DEVELOPMENT_DIR/build/doc/doxygen"
mkdir -p "$DOXY_OUT_DIR"
pushd "$DEVELOPMENT_DIR" > /dev/null
doxygen Doxyfile
popd > /dev/null

# --- prepare output dir ---
if [ -d "$OUT_DIR" ]; then
    echo "Clearing existing output directory..."
    rm -rf "${OUT_DIR:?}/"*
else
    echo "Creating output directory..."
    mkdir -p "$OUT_DIR"
fi

# --- prepare markdown sources for plain HTML generation ---
PREPARED_SRC_DIR="$TMP_DIR/public"
echo "Preparing Markdown sources for plain HTML output..."
python3 "$OPK_PROJECT_ROOT/scripts/private/prepare_plain_docs.py" \
    "$SRC_DIR" "$PREPARED_SRC_DIR"

# --- regenerate png figures (if any .puml exist) ---
PLANTUML_SRC_DIR="$OPK_PROJECT_ROOT/docs/plantuml"
PLANTUML_OUT_DIR="$SRC_DIR/static/img"

if [ -d "$PLANTUML_SRC_DIR" ]; then
    echo "Regenerating PlantUML figures from $PLANTUML_SRC_DIR..."
    mkdir -p "$PLANTUML_OUT_DIR"
    if compgen -G "$PLANTUML_SRC_DIR"/*.puml > /dev/null; then
        PLANTUML_JAR="${PLANTUML_JAR:-/opt/opk-deps/plantuml-mit-1.2026.2.jar}"
        REPOSITORY_PLANTUML_JAR="$OPK_PROJECT_ROOT/deps/plantuml-mit-1.2026.2.jar"
        if [ ! -f "$PLANTUML_JAR" ] && [ -f "$REPOSITORY_PLANTUML_JAR" ]; then
            PLANTUML_JAR="$REPOSITORY_PLANTUML_JAR"
        fi
        if [ -f "$PLANTUML_JAR" ]; then
            java -Djava.awt.headless=true -jar "$PLANTUML_JAR" -tpng "$PLANTUML_SRC_DIR"/*.puml -o "$PLANTUML_OUT_DIR"
        else
            echo "PlantUML JAR not found, skipping PlantUML figure generation."
        fi
    else
        echo "No .puml files found in $PLANTUML_SRC_DIR, skipping PlantUML generation."
    fi
else
    echo "PlantUML source directory $PLANTUML_SRC_DIR not found, skipping PlantUML generation."
fi

# --- convert markdown to html recursively ---
echo "Converting Markdown files to HTML..."
find "$PREPARED_SRC_DIR" -type f -name "*.md" -print0 | while IFS= read -r -d '' file; do
    rel_path="${file#"$PREPARED_SRC_DIR"/}"
    out_path="$HTML_CONTENT_DIR/${rel_path%.md}.html"
    mkdir -p "$(dirname "$out_path")"
    echo "Converting $rel_path -> ${rel_path%.md}.html"
    pandoc --from markdown-yaml_metadata_block "$file" -s -o "$out_path"
done

# --- copy images/assets ---
echo "Copying assets (preserving structure)..."
find "$SRC_DIR" -type f \( \
    -iname "*.png" -o -iname "*.jpg" -o -iname "*.jpeg" -o \
    -iname "*.gif" -o -iname "*.svg" -o -iname "*.webp" \
    \) -print0 | while IFS= read -r -d '' file; do
    rel_path="${file#"$SRC_DIR"/}"
    out_path="$HTML_CONTENT_DIR/$rel_path"
    mkdir -p "$(dirname "$out_path")"
    cp -f "$file" "$out_path"
done

if [ -d "$SRC_DIR/static" ]; then
    echo "Copying static assets..."
    mkdir -p "$OUT_DIR/static"
    cp -a "$SRC_DIR/static/." "$OUT_DIR/static/"
fi

if [ -d "$SRC_DIR/static/img" ]; then
    echo "Creating root-level /img alias for plain HTML output..."
    rm -rf "$OUT_DIR/img"
    mkdir -p "$OUT_DIR/img"
    cp -a "$SRC_DIR/static/img/." "$OUT_DIR/img/"
fi

# --- simple link rewrite: .md -> .html ---
echo "Rewriting internal links..."
find "$OUT_DIR" -type f -name "*.html" -exec sed -i 's/\.md"/.html"/g' {} +

cat > "$OUT_DIR/index.html" << 'EOF'
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta http-equiv="refresh" content="0; url=public/index.html">
  <title>Open Perception Kit Documentation</title>
</head>
<body>
  <p><a href="public/index.html">Open the documentation</a>.</p>
</body>
</html>
EOF

echo "Done. Open: $OUT_DIR/index.html"

BUILD_DOC_HTML_DIR="$DEVELOPMENT_DIR/build/doc/html"
echo "Copying documentation to $BUILD_DOC_HTML_DIR..."
mkdir -p "$BUILD_DOC_HTML_DIR"
rm -rf "${BUILD_DOC_HTML_DIR:?}/"*
cp -a "$OUT_DIR/." "$BUILD_DOC_HTML_DIR/"
echo "Documentation copied to $BUILD_DOC_HTML_DIR"

BUILD_DOC_MD_DIR="$DEVELOPMENT_DIR/build/doc/md"
echo "Copying documentation to $BUILD_DOC_MD_DIR..."
mkdir -p "$BUILD_DOC_MD_DIR"
cp -a "$SRC_DIR/." "$BUILD_DOC_MD_DIR/"
echo "Documentation copied to $BUILD_DOC_MD_DIR"
