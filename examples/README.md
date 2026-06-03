# Examples

This directory contains proof-of-concept applications that use the public PEK runtime API.

Examples here are intentionally not treated as supported installed tools yet.
<<<<<<< HEAD
They are small integration sketches for validating API shape and runtime use cases.
If an example becomes stable and generally useful, it can be
promoted later into `tools/`.

## Available Examples

- `infer-cli`: command-line image input proof of concept for loading an image file
  and wrapping it as a `mediaio::VideoFrame`, then executing an opchain and printing the result.
- `pipeline-run`: C++ application facade proof of concept for loading a PEK
  pipeline JSON through `pek::api::Pipeline` and receiving serialized perception JSON callbacks.
=======
They are small integration sketches for validating runtime API shape and use cases.
If an example becomes stable and generally useful, it can be promoted later into `tools/`.

## Available Examples

- `opchain-exec`: command-line image input proof of concept that loads an image
  through `pek::runtime::Tools`, wraps it as a `pek::runtime::VideoFrame`, runs an
  OpChain through `pek::runtime::OpChain`, and prints the serialized result.
- `pipeline-exec`: C++ application facade proof of concept for loading a PEK
  pipeline JSON through `pek::runtime::Pipeline` and receiving serialized perception JSON callbacks.
>>>>>>> abf198c (runtime api)
