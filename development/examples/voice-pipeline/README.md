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

# voice-pipeline

A minimal microphone/turn demo. All application code is in `main.cpp`, using
`opk::runtime::Pipeline` and the generated VAD/turn perception types. The app
introduces the demo, shows the pipeline in color, and explains how to use the
OPK web frontend for microphone input. The live status line starts with
`ESC - Quit`, with only `ESC` inverted, followed by audio progress, detected
speech, or silence while waiting for turn confirmation. Each completed turn
prints a permanent, inverted `TURN (silence timeout)` or
`TURN (turn detector decision)` line. Errors go to stderr. ESC stops playback
and restores the terminal.

From an interactive container terminal at the repository root:

```sh
./scripts/build.sh debug true
./tools/voice-pipeline
```

Open `http://localhost:9999`, click **Mic**, and allow microphone access.
Before microphone audio arrives, the status remains `Waiting for audio...`;
placeholder silence does not advance inference or conversation timers.
Stop other pipelines using ports 9999, 8000, 8001 or 8003 first. The local
Silero VAD and SmartTurn ONNX artifacts must already be downloaded through
the normal model downloader. The demo never downloads models.
Use `--pipeline PATH` to load any compatible pipeline JSON through the existing
runtime loader. There is no model list in the application. With no option, the
embedded default uses Silero VAD and SmartTurn.

The default conversation pipeline uses this structure:

```text
browser microphone -> opksrc -> VAD + SmartTurn -> opksink audio
```

The default uses black video for the web viewer. When using a turn
OpChain, VAD and SmartTurn share one `opkinfer name=voice`, so their sample
positions have the same origin.
Conversation handling consumes only perceptions from that element. There is no diarization,
STT submission or speaker identity logic.

For overlapping VAD windows, `TurnDetector` retains each sample only once when
building turn history. Set `inputWindowSampleCount` to the VAD context and
`hopSampleCount` to the inference cadence; see
[the audio preprocessing contract](../../../docs/arch/audio-log-mel.md#conversation-audio-and-metadata).

Custom pipelines can add an independently scheduled microphone inference branch
named `keywords`. The console prints `KEYWORD <label> (<probability>%)` for its
accepted classifications. These are rolling model assessments, so the same word
may appear repeatedly; they do not start or stop the conversation state machine.

The application applies this policy:

- Speech opens an utterance. Initial silence produces nothing.
- Two consecutive positive SmartTurn assessments at distinct window ends close it.
- New speech or a negative assessment clears pending confirmations.
- Continuous VAD silence closes it after 2.5 seconds even without a positive model
  result (2528 ms with the default 512-sample hop).
- Further silence and model retries produce no additional event until new speech.
- VAD discontinuities and missing sample coverage discard pending state.

The timeout uses observed 16 kHz samples, so stalled audio does not advance it.
Multiple assessments in one packet are sorted by sample time, VAD first at equal
endpoints. The app receives metadata rather than GStreamer stream events; resets
are conveyed by VAD's discontinuity flag.

To use another model or cadence, copy a pipeline preset and change its OpChain
path and `audio-hop-samples`, then pass that file with `--pipeline`. Keep the
VAD/turn inference element named `voice`, and an optional keyword element named
`keywords`; the app routes their existing perception payloads by identity.
For checkout paths outside `/work`, set `OPK_PROJECT_ROOT`.
`OPK_PLUGIN_PATH` can override the default `development/build-active/meson-out`
plugin directory.
