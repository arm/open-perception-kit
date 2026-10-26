# SmartTurn PCM

Uses the pinned `soniqo/Smart-Turn-v3.2-ONNX` INT8 model in `model.json`.
The input is Float32 mono 16 kHz PCM, shape `[1,128000]` (eight seconds).
Normalization and log-mel extraction are embedded in the ONNX model. OPK supplies
raw PCM; short histories are padded with zeros at the front. The output is one
turn-completion probability, thresholded at 0.5 for the `turn_complete` model hint.

`opchain-vad.json` runs Silero VAD every 512 fresh samples, then `TurnDetector`
retains an eight-second history and prepares SmartTurn's input after speech has
been followed by 700 ms of VAD-inactive audio. This delay follows Silero's own
release delay. There is no evaluation during initial silence or active speech.
While silence continues, it evaluates up to three times, at least 500 ms apart.
These are model assessments, including retries after a positive result;
resumed speech starts a new set of assessments. Audio discontinuities or gaps
reset the history and gate. Timings advance by media samples, not wall-clock time.
If all three assessments are negative, the gate still stops until speech resumes;
continued silence alone does not force a positive result or further retries.

The gate must directly follow the VAD postprocessor and directly precede the
SmartTurn inference in an unlooped chain. It matches the current VAD sample window,
model and infer-element identity. Skipping SmartTurn ends the chain successfully;
VAD perceptions already emitted for that window remain available downstream.

`SmartTurnParser` appends a `TurnDetections` payload with one `TurnDetection` for
every assessment, positive or negative, using content type `turnDetection`.
It includes `turn_probability`, `turn_complete`, sample bounds, optional media
timestamps, discontinuity and standard model/producer metadata. It does not print.
Window bounds describe real audio; synthetic front padding has no timestamp.
The first assessment after initialization or a history reset is marked as a
discontinuity, even when the gate skipped the buffer where the reset happened.
These are model hints, not final utterance boundaries. The
[`voice-pipeline` application](../../../development/examples/voice-pipeline/README.md)
confirms two consecutive positive assessments or falls back after 2.5 seconds of
VAD silence, printing one turn per utterance. Submission to STT is not implemented.
See [Turn detection results](../../../docs/arch/perception.md#turn-detection-results)
for the payload contract and SDK compatibility.
Inference is synchronous, so each SmartTurn evaluation can delay audio delivery.

Run the turn demo in the container with `./tools/voice-pipeline`.
`opchain.json` is a standalone chain, evaluating complete eight-second windows
without the VAD gate; use the combined chain for the microphone experiment.

Model configuration and license information:
[pinned model repository](https://huggingface.co/soniqo/Smart-Turn-v3.2-ONNX/tree/b48fdbe20772bcec1fef02f4a1a355236ef6359e).
The standard OPK model downloader owns artifact retrieval; no separate download
script is needed.
