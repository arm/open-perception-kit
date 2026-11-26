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

Provide model-independent log-mel preprocessing, voice-activity decoding and
keyword classification through the existing OPK audio path. Preserve the supported
Silero PCM path; model-specific settings belong in model-local configuration.

Keep the existing ownership split:

- `opkinfer` owns PCM conversion, sample positions, scheduled windows and resets.
- `GenericAudioPreprocess` binds the next inference input and calls a local
  audio tensor builder under `ops-std/preproc`, like image preprocessing.
- The builder configures one PCM or log-mel conversion and binds its kernel.
  The selected implementation owns its processing state and numerical rules.
- Inference owns tensors and recurrent feedback; parsers emit existing
  `VoiceActivityDetections` and `Classifications` payloads.
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
2. No waveform padding by default. Optional centered reflection pads half an FFT
   on each side without repeating boundary samples. No dither or pre-emphasis.
3. Squared complex magnitude by default, or magnitude when configured, then
   HTK triangular mel filters over a configured frequency range,
   without filter-area normalization.
4. Natural logarithm of `mel_value + logOffset`.
5. The selected normalization below.

The 16 kHz input rate is the existing audio runtime contract. Periodic Hann,
HTK and natural logarithm define the supported algorithm; this is not a promise
to reproduce every possible frontend dialect.
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
- `minFrequencyHz`, `maxFrequencyHz`: finite limits, `0 <= min < max <= 8000`.
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
dimensions are singleton. With `centerPadding: "reflect"`, the available frame count is
`1 + floor(windowSampleCount / frameHopSamples)`. Optional `frameCount` selects
that many leading frames, must be positive, and cannot exceed the available
count. Omission emits all available frames. Reflection is local to each scheduled
PCM window; it does not retain context from previous windows.
Optional `spectrum` accepts `power` (default) or `magnitude`; the latter takes
the square root of FFT power before mel projection. `centerPadding` defaults
to `none`. These options do not change existing configurations. Support 1–512 mel bins and at most 1,048,576 output
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
deviation. Require `windowSampleCount == fftSize` and `centerPadding: "none"`. On a new stream use only
observed frames; reset statistics and ONNX recurrent feedback on discontinuity.
Require the scheduler hop to equal `frameHopSamples` and contiguous frame
positions; invalid cadence is an error. Allocate history only at configuration.
Evaluate candidate moments and normalized features before committing the one
history slot, counters and next position; processing must not copy or allocate
a history vector. Output layout must not change normalization semantics.

## Model-independent output parsers

Voice-activity decoding, hysteresis and Silero configuration use the existing
[voice-activity parser contract](perception.md#voice-activity-results).

`KeywordClassificationParser.{h,cpp}` consumes a Float32 class vector with
optional singleton leading dimensions. Required configuration is
`outputTensorIndex` (0–15), `activation` (`softmax` or `none`),
`confidenceThreshold` (finite, [0,1]), a nonempty `labelSet` identifying the
ordered vocabulary, and `labels` (nonempty array of nonempty strings or nulls,
at least one string). Its length must match the tensor's
class axis. Null suppresses that class at any position; there is no fixed
class count, keyword list or unknown/noise index. Softmax and argmax include
all classes. If the winning label is null, publish nothing; do not select a
runner-up. `none` accepts finite scores in [0,1]. Publish one winner when its
score is >= the configured threshold, compared in the Float32 score domain;
ties select the first index. Publish `Classifications` with `kind=Categorical`,
the configured label-set identity, the input subject and producer/model identity.

Parser IDs name the operation rather than a model. Model names belong in
model-local descriptors, runnable profiles and artifact integration tests.
Shared algorithms and their unit tests use algorithm or payload-based filenames.

### Audio event classifications

`AudioEventClassificationParser` publishes existing `Classifications` payloads
with categorical kind and an explicit `labelSet`. It requires `outputTensorIndex`, `activation`
(`sigmoid` for logits or `none` for probabilities), `confidenceThreshold`,
positive `topK`, and a `labels` array matching the Float32 class vector. Leading
tensor dimensions must be singleton. Labels are nonempty strings or null, with
at least one string. Null suppresses that class independently.

All scores must be finite; `none` additionally requires [0,1]. Sigmoid is computed
stably per class, without normalizing across classes. Candidates at or above the
Float32 threshold are sorted by descending probability, ties by ascending class
index, and limited to `topK`. Every successful assessment publishes one typed
`Classifications` payload. If no classes qualify (including when qualifying classes
have null labels), its classification retains its kind, label set and subject
identity with an empty candidate list. This distinguishes an empty assessment from
disabled, skipped or unavailable output. Each payload preserves model, inference
element, producer and parent-object identity. Invalid input still returns an error
without appending a payload.
Classification results do not carry audio-window bounds. See the
[YAMNet model](../../config/models/yamnet-esc/index.md) for its frontend and artifacts.

## Conversation audio and metadata

The voice-pipeline demo accepts `--pipeline PATH` through the existing JSON
loader. Its default uses Silero VAD and SmartTurn; custom pipelines keep their
VAD/turn inference element named `voice`. See the
[voice demo](../../development/examples/voice-pipeline/README.md).

Custom pipelines can add independently scheduled keyword inference in an
`opkinfer name=keywords` branch, with the postprocessor's `instanceId` set to
`opk-std-ops-GenericPostprocess-0`. Keep it separate from the conversation's
terminal turn-detection branch: `AbortChain` skips the remaining Op stages.
The demo receives its classifications through the runtime callback; keyword
assessments do not control conversation completion.

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

Model reader 1.4.0 and OpChain reader 1.13.0 support these features; see
[configuration compatibility](../public/concepts/configuration-compatibility.md).
No payload or product release version changes are required.

- [Kernel tests](../../development/tests/cpu_log_mel_spectrogram_test.cpp) compare
  independent Torch references, both layouts, rolling eviction/reset, silence,
  impulses and reconfiguration using synthetic signals.
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

## PCM window normalization

OpChain 2.2 adds optional `pcmNormalization` to `GenericAudioPreprocess`.
Omission or `{ "kind": "none" }` retains the raw PCM copy behavior.
`{ "kind": "window", "epsilon": 1e-10 }` subtracts the mean of the scheduled
window and divides by `max(sample_stddev, epsilon)`, using N−1 variance and
double-precision reductions. This is the waveform normalization required by
CREPE; it differs from log-mel normalization's additive epsilon.

Window normalization requires at least two samples, a finite positive epsilon,
and zero PCM context. It cannot be combined with `logMel`. Constant windows
produce zero; nonfinite input is rejected before writing the tensor. Normalization
has no history and preserves scheduler timing. The original PCM view remains
unchanged for downstream signal-level measurements.

For CREPE tiny, use a `[1,1024]` Float32 `AudioPcm` tensor, the window mode above,
and `audio-hop-samples=160`. The scheduler supplies complete, unpadded 64 ms
windows every 10 ms, matching the reference's `pad=False` framing. This step
prepares model inputs only; pitch decoding is a separate integration step.

## Pitch postprocessing

`opk-std-ops/GenericPostprocess` supports `PitchEstimationParser` for monophonic
pitch scores. This parser consumes raw PCM model outputs; it does not require
log-mel preprocessing. See `config/models/crepe-tiny/opchain.json` for a complete
PCM normalization → ONNX inference → pitch result chain.

The decoding and gating attributes below are required:

| Attribute | Meaning |
| --- | --- |
| `outputTensorIndex`, `binCount` | Float32 output vector selection and size; leading dimensions must be singleton. |
| `activation` | `none` for probabilities in [0,1], or stable independent `sigmoid` for logits. |
| `centsOffset`, `centsPerBin`, `referenceFrequencyHz` | Bin `i` maps to `referenceFrequencyHz * 2^((centsOffset + centsPerBin*i)/1200)`. |
| `minFrequencyHz`, `maxFrequencyHz` | Inclusive candidate-bin frequency limits. |
| `decoder` | `argmax`, or `weightedArgmax` for a score-weighted mean in cents around the strongest bin. |
| `weightedRadius` | Neighbor radius for weighted decoding, clipped to the vector and frequency limits; ignored by argmax. |
| `periodicityThreshold` | Minimum strongest eligible score for a voiced result. All-zero scores remain unvoiced. |
| `minimumRms` | Minimum original PCM RMS for a voiced result; zero disables this gate. |

Optional `log: true` prints each voiced estimate to stderr with the model name,
frequency and periodicity, regardless of the global log level. It defaults to
false; the CREPE opchain enables it for debugging.

Equal maxima select the lowest eligible bin. There is no temporal smoothing,
Viterbi decoding or random dithering. Periodicity is the maximum eligible score,
not a calibrated probability that the frequency is correct. Malformed tensors,
nonfinite values and invalid scheduling metadata fail without appending results.

`GenericAudioPreprocess` records RMS over the scheduled PCM window before any
model normalization in the inference context. It resets this value on each
attempt, so failed preprocessing cannot leave a stale level. A positive RMS gate
requires this value; it rejects missing metadata instead of assuming silence.
The parser emits one `PitchEstimate` with window bounds, optional timestamps,
discontinuity and producer identity. Unvoiced results retain periodicity and
omit frequency. Voiced includes periodic instrumental sounds as well as speech.

The CREPE tiny configuration uses 360 bins, 20 cents per bin, a 10 Hz reference,
1997.3794084376191 cents offset and a weighted radius of four bins. Its exported
ONNX output already contains sigmoid scores, so activation is `none`. Initial
voicing settings are periodicity ≥ 0.21 and RMS ≥ 0.001 (−60 dBFS for full-scale
PCM); tune these for the source. They are configuration choices, not model
accuracy guarantees. Use mono 16 kHz audio, 1024-sample full windows and a
160-sample hop for 10 ms updates. Windows are unpadded; timestamps describe the
actual input window rather than an inferred point event.

After downloading `crepe-tiny` and building the native runtime, run the opt-in
`development/tests/crepe_integration_test.py` in the development container. It
checks the actual model and serialized results for silence, a 440 Hz tone and a
quiet tone below the RMS gate. This smoke test does not measure general pitch
accuracy or realtime throughput.
