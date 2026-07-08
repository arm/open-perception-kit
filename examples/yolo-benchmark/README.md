# yolo-benchmark

`yolo-benchmark` is a manual benchmark example for comparing a bare Ultralytics
YOLO predict loop with the PEK OpChain runtime on the same image list.

This is not a PR gate. It is intended for manual and nightly runs where the
artifact is reviewed as a performance regression signal.

## Measurement Cut

The benchmark cut is defined by `schema/benchmark_summary.schema.json`.

Both runners load the model separately, preload all input images before the
timed loop, run one warmup pass, then measure one in-process image loop. JPEG
decode, image file I/O, and camera/color adapter cost are outside the per-image
timing. JSONL artifact writing is also outside each per-image timer.

The common summary artifact is:

```text
benchmark_summary.json
```

The schema is documented in `schema/benchmark_summary.schema.json`. The compare
script also checks the shared measurement block before it writes a delta report.

## Layout

```text
bare/benchmark.py  bare Ultralytics sample runner
bare/requirements.txt
pek/benchmark.cpp  PEK OpChain sample runner
tests/             unit tests for the Python helpers
```

## Build PEK Runner

Run this inside the PEK development container:

```sh
./examples/yolo-benchmark/pek/build.sh debug true
```

The build installs:

```text
examples/bin/yolo-benchmark
```

## Dataset

The input list is a TSV file:

```text
# image_set_fingerprint=sha256:<dataset-and-limit-fingerprint>
image_id<TAB>/absolute/path/to/image.jpg
```

Generate it with `prepare_dataset.py`; the runners require the fingerprint
header so a comparison cannot accidentally mix different image sets.

The runners use `IMAGE_LIST`. If it does not exist, `run.sh bare`, `run.sh pek`,
and `run.sh both` prepare the default COCO val2017 dataset under
`datasets/coco` and write the image list automatically.

For CI or smoke runs, prepare a limited image list explicitly:

```sh
python3 examples/yolo-benchmark/prepare_dataset.py \
  --coco-dir datasets/coco \
  --output artifacts/yolo-benchmark/images.tsv \
  --limit 500
```

If the dataset is already present, the helper only writes the image list. If it
is missing, it downloads the official COCO val2017 images and annotations first,
verifies their SHA-256 hashes, CRC-checks the ZIPs, and extracts only safe paths.
The helper accepts both common layouts:

```text
datasets/coco/images/val2017
datasets/coco/val2017
```

To download the official COCO val2017 images and annotations without pulling the
train split:

```text
https://s3.amazonaws.com/images.cocodataset.org/zips/val2017.zip
https://s3.amazonaws.com/images.cocodataset.org/annotations/annotations_trainval2017.zip
```

Use `--limit` on `prepare_dataset.py` for quick smoke runs. Headline runs
should use the full official COCO val2017 split.

## Run

Preferred host entry point:

```sh
./examples/yolo-benchmark/docker/setup.sh
./examples/yolo-benchmark/docker/run.sh both
```

This uses Docker Compose to run inside the PEK development image with the repo
mounted at `/work`, matching the checked-in OpChain paths and ONNX Runtime
runtime layout. `setup.sh` builds the Compose runtime image. `run.sh` creates or
reuses `artifacts/yolo-benchmark/.venv`, builds the PEK sample runner, prepares
the dataset if needed, and writes the same artifacts listed below.

For a quick smoke image list:

```sh
./examples/yolo-benchmark/docker/setup.sh
YOLO_BENCHMARK_LIMIT=500 ./examples/yolo-benchmark/docker/run.sh both
```

Inside an already prepared PEK development container, install Python
dependencies for the bare runner:

```sh
python3 -m pip install -r examples/yolo-benchmark/bare/requirements.txt
```

Then run both sides on the same image list:

```sh
./examples/yolo-benchmark/run.sh both
```

Default inputs:

```text
MODEL=config/models/yolov11/yolo11n-fp32-320.onnx
OPCHAIN=config/models/yolov11/opchain.json
IMAGE_LIST=artifacts/yolo-benchmark/images.tsv
ARTIFACT_ROOT=artifacts/yolo-benchmark
```

The sample fixes image size to `320`, device label to `cpu`, and warmup to one
image because those are part of this benchmark definition, not runtime knobs.

Outputs:

```text
artifacts/yolo-benchmark/bare/benchmark_summary.json
artifacts/yolo-benchmark/bare/predictions.jsonl
artifacts/yolo-benchmark/pek/benchmark_summary.json
artifacts/yolo-benchmark/pek/predictions.jsonl
artifacts/yolo-benchmark/comparison.json
artifacts/yolo-benchmark/comparison.md
```

The Markdown comparison contains the main per-image table:

```text
metric | bare ms | PEK ms | delta ms | ratio | delta %
```

Keep `artifacts/` local; it is ignored by git.
