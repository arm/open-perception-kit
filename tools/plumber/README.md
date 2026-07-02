# PekComm / Plumber

## Overview

This system is used to test perception output from an Perception Experience Kit pipeline.

At a high level:

- **PekComm** is a GStreamer element that serializes Perception output as **NDJSON**.
- **Plumber** is a Python-based test tool that:
  - can **save** the incoming NDJSON as Ground Truth
  - can **check** a pipeline run against previously saved Ground Truth

PekComm can publish the stream to a file/FIFO, a WebSocket endpoint, or a raw TCP socket. Plumber currently consumes the file/FIFO mode, while browser applications can consume the WebSocket mode directly.

PekComm writes one JSON object per message. In file/FIFO and TCP modes each object is newline-delimited; in WebSocket mode each object is sent as one text message.

## Architecture

```text
Perception Experience Kit pipeline
   |
   v
PekComm (GStreamer element)
   |
   +--> file/FIFO mode
   |      |  NDJSON over FIFO
   |      v
   |   /tmp/pekcomm
   |      |
   |      v
   |   Plumber
   |      |
   |      +--> save mode  -> writes Ground Truth NDJSON
   |      |
   |      \--> check mode -> compares pipeline output to Ground Truth
   |
   +--> WebSocket mode
          |  JSON text messages
          v
       browser or another WebSocket client
   |
   \--> TCP mode
          |  NDJSON over TCP
          v
       TCP client
```

## Components

### PekComm

PekComm is responsible for publishing Perception JSON objects from the pipeline.

Relevant properties:

- `method`
  - publishing method
  - supported values: `file`, `websocket`, `tcp`
  - default: `file`
- `file-name`
  - output target path used when `method=file`
  - can be a normal file path, an existing FIFO path, or `-` for stdout
  - Plumber uses `/tmp/pekcomm`, which is already created as a FIFO in the devcontainer
- `ws-port`
  - TCP port used when `method=websocket`
  - default: `8002`
- `endpoint`
  - WebSocket path used when `method=websocket`
  - must start with `/`
  - default: `/ws`
- `tcp-host`
  - bind host used when `method=tcp`
  - default: `127.0.0.1`
- `tcp-port`
  - listen port used when `method=tcp`
  - default: `7001`

Example GObject properties for Plumber/FIFO use:

- `method=file`
- `file-name=/tmp/pekcomm`

Example GObject properties for browser/WebSocket use:

- `method=websocket`
- `ws-port=8080`
- `endpoint=/ws`

Example GObject properties for raw TCP use:

- `method=tcp`
- `tcp-host=127.0.0.1`
- `tcp-port=7001`

### Plumber

Plumber is the regression / validation tool.

It starts a pipeline with `pek-menu`, reads the generated NDJSON stream from the FIFO, and either:

- stores it as Ground Truth
- or compares it to an existing Ground Truth file

## Data Format

PekComm writes serialized Perception JSON objects:

- file/FIFO mode writes **NDJSON**: one complete JSON object per line
- WebSocket mode sends one complete JSON object per text message
- TCP mode writes **NDJSON**: one complete JSON object per line
- the object shape is the same in all modes

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

## PekComm Configuration Examples

For Plumber/FIFO workflows, configure PekComm with a FIFO path:

```text
pekcomm method=file file-name=/tmp/pekcomm
```

For browser or other live WebSocket clients, configure PekComm with a port and endpoint:

```text
pekcomm method=websocket ws-port=8080 endpoint=/ws
```

The resulting WebSocket URL is `ws://<host>:<ws-port><endpoint>`, for example `ws://127.0.0.1:8080/ws`.

For raw TCP clients, configure PekComm with a bind host and port:

```text
pekcomm method=tcp tcp-host=127.0.0.1 tcp-port=7001
```

TCP clients receive newline-delimited JSON from the configured host and port.

## Typical Workflow

### 1. Record Ground Truth

Run Plumber in `save` mode on a known-good pipeline output.

### 2. Run Validation

Run Plumber in `check` mode and compare a new pipeline run against the saved Ground Truth.

This allows regression testing of pipeline output across code changes.

## Notes

- FIFO output is used for communication between PekComm and Plumber.
- WebSocket output is useful for browser frontends and live monitoring tools.
- TCP output is useful for clients that consume raw newline-delimited metadata without WebSocket framing.
- Ground Truth is stored as NDJSON.
- NDJSON is used by Plumber because it is simple, stream-friendly, and easy to process line by line.
