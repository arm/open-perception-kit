# AmpComm / Plumber

## Overview

This system is used to test perception output from an AMP pipeline.

At a high level:

- **AmpComm** is a GStreamer element that serializes Perception output as **NDJSON**.
- **Plumber** is a Python-based test tool that:
  - can **save** the incoming NDJSON as Ground Truth
  - can **check** a pipeline run against previously saved Ground Truth

The data exchange between the pipeline and Plumber is currently done through a **FIFO**.

AmpComm writes one JSON object per line, and Plumber reads the stream line by line.

## Architecture

```text
AMP pipeline
   |
   v
AmpComm (GStreamer element)
   |
   |  NDJSON over FIFO
   v
/tmp/ampcomm
   |
   v
Plumber
   |
   +--> save mode  -> writes Ground Truth NDJSON
   |
   \--> check mode -> compares pipeline output to Ground Truth
```

## Components

### AmpComm

AmpComm is responsible for publishing Perception JSON objects from the pipeline.

Relevant properties:

- `method`
  - publishing method
  - currently only `file`
- `file-name`
  - output target path
  - `file-name` can be any file (existing or non-existing) or fifo name (must exists). 
    if the property is set to `-`, the AmpComm writes the NDJSON to the standard output
  - Plumber uses `/tmp/ampcomm` FIFO, which is already created in the devcontainer 

Example GObject properties:

- `method=file`
- `file-name=/tmp/ampcomm`

### Plumber

Plumber is the regression / validation tool.

It starts a pipeline with `amp-menu`, reads the generated NDJSON stream from the FIFO, and either:

- stores it as Ground Truth
- or compares it to an existing Ground Truth file

## Data Format

AmpComm writes **NDJSON**:

- one Perception JSON per line
- each line is a complete JSON object
- suitable for streaming over FIFO

Example:

```json
{"frame_counter":0,"perception":{"layers":[...]}}
```

## Plumber Usage

### Command line

```bash
plumber <pipeline> <mode> <file> [options]
```

### Positional arguments

- `pipeline`
  - pipeline name passed to `amp-menu`
  - example: `onnx`

- `mode`
  - `save`
  - `check`

- `file`
  - in `save` mode: output Ground Truth file
  - in `check` mode: input Ground Truth file

### Options

- `--fifo`
  - path to FIFO used by AmpComm
  - default: `/tmp/ampcomm`

- `--amp-menu`
  - path to the `amp-menu` executable
  - default: `/work/tools/amp-menu`

- `--amp-menu-args`
  - extra arguments passed to `amp-menu`

- `--limit`
  - stop after N messages
  - `0` means unlimited

- `--verbose`
  - print extra debug information

- `--fail-fast`
  - stop at the first mismatch in `check` mode

## Examples

### Save Ground Truth

```bash
plumber onnx save gt.ndjson --fifo /tmp/ampcomm --limit 100
```

This will:

- start `amp-menu onnx`
- read NDJSON from `/tmp/ampcomm`
- save 100 messages into `gt.ndjson`

### Check Against Ground Truth

```bash
plumber onnx check gt.ndjson --fifo /tmp/ampcomm --fail-fast
```

This will:

- start `amp-menu onnx`
- read NDJSON from `/tmp/ampcomm`
- compare the incoming data with `gt.ndjson`

## AmpComm Configuration Example

Example pipeline element configuration:

```text
ampcomm method=file file-name=/tmp/ampcomm
```

In the current setup, `file-name` should refer to a FIFO path used for communication with Plumber.

## Typical Workflow

### 1. Record Ground Truth

Run Plumber in `save` mode on a known-good pipeline output.

### 2. Run Validation

Run Plumber in `check` mode and compare a new pipeline run against the saved Ground Truth.

This allows regression testing of pipeline output across code changes.

## Notes

- FIFO output is used for live communication between AmpComm and Plumber.
- Ground Truth is stored as NDJSON.
- NDJSON is used because it is simple, stream-friendly, and easy to process line by line.

