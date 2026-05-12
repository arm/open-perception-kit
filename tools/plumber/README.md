# PekComm / Plumber

## Overview

This system is used to test perception output from an Perception Experience Kit pipeline.

At a high level:

- **PekComm** is a GStreamer element that serializes Perception output as **NDJSON**.
- **Plumber** is a Python-based test tool that:
  - can **save** the incoming NDJSON as Ground Truth
  - can **check** a pipeline run against previously saved Ground Truth

The data exchange between the pipeline and Plumber is currently done through a **FIFO**.

PekComm writes one JSON object per line, and Plumber reads the stream line by line.

## Architecture

```text
Perception Experience Kit pipeline
   |
   v
PekComm (GStreamer element)
   |
   |  NDJSON over FIFO
   v
/tmp/pekcomm
   |
   v
Plumber
   |
   +--> save mode  -> writes Ground Truth NDJSON
   |
   \--> check mode -> compares pipeline output to Ground Truth
```

## Components

### PekComm

PekComm is responsible for publishing Perception JSON objects from the pipeline.

Relevant properties:

- `method`
  - publishing method
  - currently only `file`
- `file-name`
  - output target path
  - `file-name` can be any file (existing or non-existing) or fifo name (must exists). 
    if the property is set to `-`, the PekComm writes the NDJSON to the standard output
  - Plumber uses `/tmp/pekcomm` FIFO, which is already created in the devcontainer 

Example GObject properties:

- `method=file`
- `file-name=/tmp/pekcomm`

### Plumber

Plumber is the regression / validation tool.

It starts a pipeline with `pek-menu`, reads the generated NDJSON stream from the FIFO, and either:

- stores it as Ground Truth
- or compares it to an existing Ground Truth file

## Data Format

PekComm writes **NDJSON**:

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
  - pipeline name passed to `pek-menu`
  - example: `onnx`

- `mode`
  - `save`
  - `check`

- `file`
  - in `save` mode: output Ground Truth file
  - in `check` mode: input Ground Truth file

### Options

- `--fifo`
  - path to FIFO used by PekComm
  - default: `/tmp/pekcomm`

- `--pek-menu`
  - path to the `pek-menu` executable
  - default: `/work/tools/pek-menu`

- `--pek-menu-args`
  - extra arguments passed to `pek-menu`

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
plumber onnx save gt.ndjson --fifo /tmp/pekcomm --limit 100
```

This will:

- start `pek-menu onnx`
- read NDJSON from `/tmp/pekcomm`
- save 100 messages into `gt.ndjson`

### Check Against Ground Truth

```bash
plumber onnx check gt.ndjson --fifo /tmp/pekcomm --fail-fast
```

This will:

- start `pek-menu onnx`
- read NDJSON from `/tmp/pekcomm`
- compare the incoming data with `gt.ndjson`

## PekComm Configuration Example

Example pipeline element configuration:

```text
pekcomm method=file file-name=/tmp/pekcomm
```

In the current setup, `file-name` should refer to a FIFO path used for communication with Plumber.

## Typical Workflow

### 1. Record Ground Truth

Run Plumber in `save` mode on a known-good pipeline output.

### 2. Run Validation

Run Plumber in `check` mode and compare a new pipeline run against the saved Ground Truth.

This allows regression testing of pipeline output across code changes.

## Notes

- FIFO output is used for live communication between PekComm and Plumber.
- Ground Truth is stored as NDJSON.
- NDJSON is used because it is simple, stream-friendly, and easy to process line by line.

