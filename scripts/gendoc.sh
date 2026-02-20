#!/usr/bin/env bash
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
echo "Copying assets..."
find "$SRC_DIR" -maxdepth 1 -type f \( \
    -iname "*.png" -o -iname "*.jpg" -o -iname "*.jpeg" -o \
    -iname "*.gif" -o -iname "*.svg" -o -iname "*.webp" \
    \) -exec cp -f {} "$OUT_DIR" \;

# --- simple link rewrite: .md -> .html ---
echo "Rewriting internal links..."
find "$OUT_DIR" -type f -name "*.html" -exec sed -i 's/\.md"/.html"/g' {} +

echo "Done. Open: $OUT_DIR/index.html"

ZIP_NAME="doc-html.zip"
ZIP_PATH="/work/development/build/doc/$ZIP_NAME"
BUILD_DOC_DIR="/work/development/build/doc"

echo "Zipping documentation..."
mkdir -p "$BUILD_DOC_DIR"
cd "$OUT_DIR"
zip -r "$ZIP_PATH" .
cd -
echo "Documentation zipped and copied to $ZIP_PATH"
