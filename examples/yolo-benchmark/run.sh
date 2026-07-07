#!/usr/bin/env bash
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"

ARTIFACT_ROOT="${ARTIFACT_ROOT:-$REPO_ROOT/artifacts/yolo-benchmark}"
IMAGE_LIST="${IMAGE_LIST:-$ARTIFACT_ROOT/images.tsv}"
MODEL="$REPO_ROOT/config/models/yolov11/yolo11n-fp32-320.onnx"
OPCHAIN="$REPO_ROOT/config/models/yolov11/opchain.json"
COCO_DIR="$REPO_ROOT/datasets/coco"

usage() {
    cat << EOF
Usage: $0 <bare|pek|compare|both>

Environment:
  ARTIFACT_ROOT  Output directory. Default: artifacts/yolo-benchmark
  IMAGE_LIST     TSV image list. Default: \$ARTIFACT_ROOT/images.tsv

Examples:
  $0 both
EOF
}

prepare_dataset_if_needed() {
    if [[ ! -f "$IMAGE_LIST" ]]; then
        python3 "$SCRIPT_DIR/prepare_dataset.py" --coco-dir "$COCO_DIR" --output "$IMAGE_LIST"
    fi
}

command="${1:-}"
if [[ -z "$command" || "$command" == "-h" || "$command" == "--help" ]]; then
    usage
    exit 0
fi
shift

case "$command" in
    bare)
        prepare_dataset_if_needed
        python3 "$SCRIPT_DIR/bare/benchmark.py" \
            --model "$MODEL" \
            --images "$IMAGE_LIST" \
            --output "$ARTIFACT_ROOT/bare/predictions.jsonl" \
            --summary "$ARTIFACT_ROOT/bare/benchmark_summary.json"
        ;;
    pek)
        prepare_dataset_if_needed
        "$REPO_ROOT/examples/bin/yolo-benchmark" \
            --opchain "$OPCHAIN" \
            --images "$IMAGE_LIST" \
            --output "$ARTIFACT_ROOT/pek/predictions.jsonl" \
            --summary "$ARTIFACT_ROOT/pek/benchmark_summary.json"
        ;;
    compare)
        python3 "$SCRIPT_DIR/compare_benchmark_summaries.py" \
            --bare-summary "$ARTIFACT_ROOT/bare/benchmark_summary.json" \
            --pek-summary "$ARTIFACT_ROOT/pek/benchmark_summary.json" \
            --output-json "$ARTIFACT_ROOT/comparison.json" \
            --output-md "$ARTIFACT_ROOT/comparison.md"
        ;;
    both)
        "$0" bare
        "$0" pek
        "$0" compare
        ;;
    *)
        echo "Unknown command: $command" >&2
        usage >&2
        exit 2
        ;;
esac
