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

# Native log-mel audio preprocessing

Status: model-independent implementation validated in the development container on 2026-11-11.

## Goal and existing boundaries

Provide model-independent log-mel preprocessing and voice-activity decoding
through the existing OPK audio path. Preserve the supported Silero PCM path;
model-specific settings belong in model-local configuration.

Keep the existing ownership split:

- `opkinfer` owns PCM conversion, sample positions, scheduled windows and resets.
- `GenericAudioPreprocess` binds the next inference input and calls a local
  audio tensor builder under `ops-std/preproc`, like image preprocessing.
- The builder configures one PCM or log-mel conversion and binds its kernel.
  The selected implementation owns its processing state and numerical rules.
- Inference owns tensors and recurrent feedback; parsers emit existing
  `VoiceActivityDetections` payloads.
- `TurnDetector` owns retained conversation audio and pause-based assessment.
- `opksrc` and the existing web frontend own browser microphone capture.

No new downloader, inference engine, media transport, persistent result schema,
general stage scheduler, or unused accelerator abstraction is needed.

### Preprocessing responsibilities

`GenericAudioPreprocess` selects the PCM source and target tensor, obtains its
writable storage, invokes the bound builder, and supplies inference identity.
It forwards scheduler timing without interpreting the frontend's normalization
or cadence. It owns no PCM context, FFT state or rolling-window position.

The audio tensor builder validates configuration and tensor compatibility, then
binds exactly one supported implementation. PCM copying owns its optional
retained context and contiguous-hop checks. Log-mel processing owns its FFT,
normalization statistics and rolling-cadence checks. Their state resets on
reconfiguration, rebind and stream discontinuity. Invalid configuration or a
failed bind must not leave a previous implementation executable. A failed
processing call must preserve the destination and retained state.

Keep the builder local to audio preprocessing; the existing image builder's
image-specific interface does not need generalization. Preserve the profiles'
numeric behavior. Configuration limits and the feature
shape calculation have one implementation owner, reused during validation and
binding rather than copied into the Op.

Processing is allocation-free after binding.

For an audio inference stage, `OpChain` first uses the immediately preceding
`OpInterfaceAudioPreprocessor`'s scheduled PCM window. Without that interface,
retain the supported fixed-shape raw-PCM fallback and reject log-mel input.
Tensor conversion rules belong to the bound preprocessor. Preserve the chain's
shape checks for every audio input, including inputs not selected by that
preprocessor. The chain still rejects inconsistent scheduled windows and inner
Op loops.

## Feature contract

Input is finite mono Float32 PCM at 16 kHz, obtained from `PcmRingBuffer::View`,
including wrapped views. Output is contiguous Float32, `[T,M]` or `[M,T]` as
selected by configuration, with optional singleton leading dimensions. Tensor shape supplies frame and mel
counts; incompatible kinds, types, dimensions, non-finite input and missing
storage fail explicitly before inference. Check FFT power, log features and
normalized output for finiteness as well; finite PCM can overflow Float32 power.

`CpuLogMelSpectrogram` and `LogMelSpectrogramConfig`, in files named after the
algorithm, implement this supported frontend family:

1. An unnormalized real FFT, with a configurable periodic Hann window centered
   inside it (floor of half the difference on the left). Generate its coefficients
   with Float32 phase and cosine arithmetic, consistent with the Float32 frontend.
2. No waveform centering, reflection, end padding, dither or pre-emphasis.
3. Squared complex magnitude, then HTK triangular mel filters over a configured frequency range,
   without filter-area normalization.
4. Natural logarithm of `mel_power + logOffset`.
5. The selected normalization below.

The 16 kHz input rate is the existing audio runtime contract. Periodic Hann,
HTK, power-squared, natural logarithm and no padding define the supported
algorithm; this is not a promise to reproduce every possible frontend dialect.
Model-specific dimensions, frequency limits and normalization settings belong
in model-local opchains. Use GStreamer's existing
`gstreamer-fft-1.0` float FFT facility. Allocate the FFT plan, window and mel
coefficients once per bound Op, release them with that Op, and allocate rolling
statistics only for rolling normalization. Do not directly link FFTW or add a
new FFT implementation/dependency.

`GenericAudioPreprocess` gains a `logMel` object with required fields:

- `windowSampleCount`: scheduled PCM window length (`fftSize`–960000).
- `fftSize`: even FFT length, 4–4096, with prime factors only 2, 3 and 5.
  These sizes use the FFT library's allocation-free butterflies; length 2
  would enter its generic allocating radix-1 path.
- `windowLengthSamples`: periodic Hann length, 2–`fftSize`.
- `frameHopSamples`: distance between FFT frame starts, 1–`windowSampleCount`.
- `minFrequencyHz`, `maxFrequencyHz`: finite limits, 0 <= min < max <= 8000.
  Mel-filter breakpoints must remain distinct and ordered in Float32 arithmetic.
- `logOffset`: finite, positive and representable as a positive Float32.
- `layout`: `time-major` (`[T,M]`) or `mel-major` (`[M,T]`).
- `normalization`: `{ "kind": "none" }`,
  `{ "kind": "window", "epsilon": E }`, or
  `{ "kind": "rolling", "epsilon": E, "historyFrames": K }`.
  E is finite and positive; K is an integer from 1 to 4096. Reject fields
  irrelevant to the selected normalization, as well as unknown fields.

`AudioLogMel` requires this object. `AudioPcm` rejects it. Existing PCM
`contextSampleCount` remains available only for PCM. Bins come from the
configured layout's mel axis, and frames must equal
`1 + floor((windowSampleCount - fftSize) / frameHopSamples)`; all preceding
dimensions are singleton. Support 1–512 mel bins and at most 1,048,576 output
values. These bounds limit resource use, not model identities. The opchain and model schemas validate these contracts, while bind
checks tensor agreement with the loaded model. `OpChain` obtains the scheduled
PCM window from the adjacent preprocessor for log-mel inference; bare log-mel
inference fails setup. Audio chains remain unlooped, and all stages must agree
on the scheduled PCM window length.

Window normalization computes one mean and sample standard deviation (N−1)
over all log-mel values and returns `(x - mean)/(std + epsilon)`; require at
least two values. Use float64 reductions for numerical stability. With `none`,
return the unnormalized log-mel values. Constant features normalize to zero.
Float64 reductions avoid cancellation artifacts on constant features.

Rolling normalization computes global mean and population variance over all
bins of the latest `historyFrames` frames, including the current frame. Accumulate moments in
float64, clamp roundoff below zero before sqrt, and add `epsilon` to the standard
deviation. Require `windowSampleCount == fftSize`. On a new stream use only
observed frames; reset statistics and ONNX recurrent feedback on discontinuity.
Require the scheduler hop to equal `frameHopSamples` and contiguous frame
positions; invalid cadence is an error. Allocate history only at configuration.
Evaluate candidate moments and normalized features before committing the one
history slot, counters and next position; processing must not copy or allocate
a history vector. Output layout must not change normalization semantics.

## Model-independent output parsers

Voice-activity decoding, hysteresis and Silero configuration use the existing
[voice-activity parser contract](perception.md#voice-activity-results).

Parser IDs name the operation rather than a model. Model names belong in
model-local descriptors, runnable profiles and artifact integration tests.
Shared algorithms and their unit tests use algorithm or payload-based filenames.

## Conversation audio and metadata

The voice-pipeline demo accepts `--pipeline PATH` through the existing JSON
loader. Its default uses Silero VAD and SmartTurn; custom pipelines keep their
VAD/turn inference element named `voice`. See the
[voice demo](../../development/examples/voice-pipeline/README.md).

Before microphone audio arrives, `opksrc` emits placeholder silence marked
`GAP`. The audio scheduler resets and skips those buffers, so they do not advance
normalization state or conversation timers. Inference waits for a complete
window of real audio.

The web frontend displays the existing VAD/turn probabilities, state and
audio-window bounds. Its view model preserves 64-bit sample/timestamp values
and absent timestamps. VAD and turn assessments stay visible between inference
windows and clear when the source resets or stops. Video results describe the
current frame. Turn-model assessments remain distinct from the conversation
controller's final turn decisions.

`TurnDetector` accepts `inputWindowSampleCount`; omission uses `hopSampleCount`
for existing callers. It requires window >= hop, a matching scheduler
window/hop, ordered bounds and exactly one matching VAD assessment. On the
first/reset call it retains the whole window; on successive overlapping calls
it retains only the newest hop. Expected starts and ends advance by one hop.
A gap resets history and speech gating. Duplicate/backward windows, malformed
bounds and wrong hops are errors. An explicit discontinuity may establish a new
sample origin. Matching uses the complete VAD-window bounds before metadata is
replaced with turn-history bounds.

Turn timing follows new sample progress. VAD hysteresis advances by the effective
scheduler hop. The first turn assessment after a reset retains its discontinuity
marker even if intermediate assessments were skipped. Initial silence does not
trigger turn inference.

## Compatibility and validation

Model reader 1.4.0 and OpChain reader 1.12.0 support these features; see
[configuration compatibility](../public/concepts/configuration-compatibility.md).
No payload or product release version changes are required.

- [Kernel tests](../../development/tests/cpu_log_mel_spectrogram_test.cpp) compare
  independent Torch references, both layouts, rolling eviction/reset, silence,
  impulses and reconfiguration without private models.
- [Preprocessor tests](../../development/tests/generic_audio_preprocess_test.cpp)
  cover tensor binding, wrapped PCM, invalid data and lifecycle;
  [descriptor tests](../../development/tests/opchain_descriptor_test.cpp) cover
  configuration validation before startup.
- [Parser tests](../../development/tests/audio_parser_test.cpp),
  [turn detector tests](../../development/tests/turn_detector_test.cpp) and
  [scheduler tests](../../development/tests/audio_window_pipeline_test.py) cover
  score decoding, debounce, timing and discontinuities.

### Reproduce the model-independent reference tests

The checked-in `development/tests/log_mel_reference.h` contains complete Torch
reference tensors with varied FFT/window sizes, mel counts, frequency limits
and normalization settings. The `cpu_log_mel_spectrogram_tests` target consumes
every value in both layouts and after reset; it needs neither Torch nor models.
Regenerate the header in an environment with NumPy, Torch and torchaudio, then
apply the repository C++ formatter:

```sh
python3 development/tests/generate_log_mel_reference.py
clang-format -i development/tests/log_mel_reference.h
meson test -C development/build-active cpu_log_mel_spectrogram_tests --print-errorlogs
```

The generator records its library versions in the header. Repeating generation
and formatting with Torch/torchaudio 2.9.1 produced identical contents.
