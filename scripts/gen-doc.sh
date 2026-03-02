#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

SRC_DIR="/work/docs/corespec"
OUT_DIR="$SRC_DIR/html"

echo "Building HTML docs..."
echo "Source: $SRC_DIR"
echo "Output: $OUT_DIR"

# --- prepare output dir ---
if [ -d "$OUT_DIR" ]; then
    echo "Clearing existing output directory..."
    rm -rf "${OUT_DIR:?}/"*
else
    echo "Creating output directory..."
    mkdir -p "$OUT_DIR"
fi

# --- convert markdown to html ---
for file in "$SRC_DIR"/*.md; do
    base="$(basename "$file")"
    name="${base%.md}"
    echo "Converting $base -> $name.html"
    pandoc "$file" -s -o "$OUT_DIR/$name.html"
done

# --- copy images/assets ---
echo "Copying assets (preserving structure)..."
find "$SRC_DIR" -type f \( \
    -iname "*.png" -o -iname "*.jpg" -o -iname "*.jpeg" -o \
    -iname "*.gif" -o -iname "*.svg" -o -iname "*.webp" \
    \) -print0 | while IFS= read -r -d '' file; do
    rel_path="${file#$SRC_DIR/}"
    out_path="$OUT_DIR/$rel_path"
    mkdir -p "$(dirname "$out_path")"
    cp -f "$file" "$out_path"
done

# --- simple link rewrite: .md -> .html ---
echo "Rewriting internal links..."
find "$OUT_DIR" -type f -name "*.html" -exec sed -i 's/\.md"/.html"/g' {} +

echo "Done. Open: $OUT_DIR/index.html"

HTML_ZIP_NAME="doc-html.zip"
HTML_ZIP_PATH="/work/development/build/doc/$HTML_ZIP_NAME"
BUILD_DOC_DIR="/work/development/build/doc"

echo "Zipping documentation..."
mkdir -p "$BUILD_DOC_DIR"
cd "$OUT_DIR"
zip -r "$HTML_ZIP_PATH" .
cd -
echo "Documentation zipped and copied to $HTML_ZIP_PATH"
