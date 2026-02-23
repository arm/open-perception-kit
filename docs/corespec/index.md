```                                               
▄████▄ ██▄  ▄██ █████▄   █ ██    ██  ██ ██ ▄█▀ 
██▄▄██ ██ ▀▀ ██ ██▄▄█▀  █  ██    ██▄▄██ ████   
██  ██ ██    ██ ██     █   ██████ ▀██▀  ██ ▀█▄ 
```

## Arm Media Pipelines – Linux Vision Kit

AMP/LVK defines the internal architecture and execution model of the
GStreamer-based inference processing framework.

The system enables reliable, modular, and extensible AI inference execution
inside GStreamer pipelines, targeting containerized development and edge deployment.

# 📚 Documentation Map

## General Overview

- [Generic Info](generic-info.md)
  High-level introduction and project scope.

- [How-To](how-to.md)
  How to open and start the project on PC or Raspberry Pi 5.

- [Architecture](architectural-overview.md)  
  Arxhitectural overview of the system.

- [Containers](containers.md)  
  Container runtime architecture and WebRTC integration model.

- [Inference Engines](inference-engines.md)  
  About the inference engines important for our project.
  
- [amp::Model](model.md)  
  The amp::Model object.

- [Inference Process](engine-independent.md)  
  The inference engine independent inference process.

- [Types](types.md)  
  Generic types.

## Execution Engine

- [Perception](perception.md)  
  Perception is the persistent metadata container.

- [Op system](op-system.md)  
  Local processing based on micro-pipelines.

- [OpChain Context](op-chain-context.md)  
  Transient runtime data model and ownership rules.

- [OpChain Example](op-chain-example.md)  
  The Op system in a simple example.

- [Tensor Builder](tensor-builder.md)  
  The input tensors are built by the tensor builders.

- [Tensor Parser](tensor-parser.md)  
  The output tensors are parsed by one of the tensor parser.

## Elements

- [ampinfer](ampinfer.md)  
  Details of the inference element.

- [amposd](amposd.md)  
  Details of the drawing element.

- [ampperformance](ampperformance.md)  
  Details about the performance measurement system.

- [ampsink](ampsink.md)  
  Details about the WebRTC presentation system.


