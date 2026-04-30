#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

SRC_DIR="/work/docs/public"
OUT_DIR="/work/docs/html"
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
DOXY_OUT_DIR="/work/development/build/doc/doxygen"
mkdir -p "$DOXY_OUT_DIR"
(   
    cd /work/development
    doxygen Doxyfile
)

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
python3 /work/scripts/private/prepare_plain_docs.py "$SRC_DIR" "$PREPARED_SRC_DIR"

# --- regenerate png figures (if any .puml exist) ---
PLANTUML_SRC_DIR="/work/docs/static/plantuml"
PLANTUML_OUT_DIR="/work/docs/static/img"

if [ -d "$PLANTUML_SRC_DIR" ]; then
    echo "Regenerating PlantUML figures from $PLANTUML_SRC_DIR..."
    mkdir -p "$PLANTUML_OUT_DIR"
    if compgen -G "$PLANTUML_SRC_DIR"/*.puml > /dev/null; then
        java -Djava.awt.headless=true -jar /work/deps/plantuml-mit-1.2026.2.jar -tpng "$PLANTUML_SRC_DIR"/*.puml -o "$PLANTUML_OUT_DIR"
    else
        echo "No .puml files found in $PLANTUML_SRC_DIR, skipping PlantUML generation."
    fi
else
    echo "PlantUML source directory $PLANTUML_SRC_DIR not found, skipping PlantUML generation."
fi

# --- convert markdown to html recursively ---
echo "Converting Markdown files to HTML..."
find "$PREPARED_SRC_DIR" -type f -name "*.md" -print0 | while IFS= read -r -d '' file; do
    rel_path="${file#$PREPARED_SRC_DIR/}"
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
    rel_path="${file#$SRC_DIR/}"
    out_path="$HTML_CONTENT_DIR/$rel_path"
    mkdir -p "$(dirname "$out_path")"
    cp -f "$file" "$out_path"
done

if [ -d "/work/docs/static" ]; then
    echo "Copying static assets..."
    mkdir -p "$OUT_DIR/static"
    cp -a /work/docs/static/. "$OUT_DIR/static/"
fi

# --- simple link rewrite: .md -> .html ---
echo "Rewriting internal links..."
find "$OUT_DIR" -type f -name "*.html" -exec sed -i 's/\.md"/.html"/g' {} +

cat > "$OUT_DIR/index.html" <<'EOF'
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta http-equiv="refresh" content="0; url=public/index.html">
  <title>AMP Development Forge Documentation</title>
</head>
<body>
  <p><a href="public/index.html">Open the documentation</a>.</p>
</body>
</html>
EOF

echo "Done. Open: $OUT_DIR/index.html"

BUILD_DOC_HTML_DIR="/work/development/build/doc/html"
echo "Copying documentation to $BUILD_DOC_HTML_DIR..."
mkdir -p "$BUILD_DOC_HTML_DIR"
rm -rf "${BUILD_DOC_HTML_DIR:?}/"*
cp -a "$OUT_DIR/." "$BUILD_DOC_HTML_DIR/"
echo "Documentation copied to $BUILD_DOC_HTML_DIR"

BUILD_DOC_MD_DIR="/work/development/build/doc/md"
echo "Copying documentation to $BUILD_DOC_MD_DIR..."
mkdir -p "$BUILD_DOC_MD_DIR"
cp -a "$SRC_DIR/." "$BUILD_DOC_MD_DIR/"
echo "Documentation copied to $BUILD_DOC_MD_DIR"
