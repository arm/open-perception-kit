```                                               
▄████▄ ██▄  ▄██ █████▄   █ ██    ██  ██ ██ ▄█▀ 
██▄▄██ ██ ▀▀ ██ ██▄▄█▀  █  ██    ██▄▄██ ████   
██  ██ ██    ██ ██     █   ██████ ▀██▀  ██ ▀█▄ 
```

# AMP / LVK  
## Arm Media Pipelines – Linux Vision Kit

AMP/LVK defines the internal architecture and execution model of the
GStreamer-based inference processing framework.

The system enables reliable, modular, and extensible AI inference execution
inside GStreamer pipelines, targeting containerized development and edge deployment.

# 📚 Documentation Map

## General Overview

- [Generic Info](generic-info.md)  
  High-level introduction and project scope.

- [Architectural Overview](architectural-overview.md)  
  Core system structure and execution flow.

---

## Runtime and Deployment

- [Containers](containers.md)  
  Container runtime architecture and WebRTC integration model.

---

## Execution Engine

- [Op System](op-system.md)  
  Operation lifecycle, inference contracts, dynamic loading, and execution semantics.

- [OpChain Context](op-chain-context.md)  
  Transient runtime data model and ownership rules.
