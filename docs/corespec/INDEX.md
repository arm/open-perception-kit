# AMP/LVK - Arm Media Pipelines/Linux Vision Kit

This file defines the internal architecture and execution model of our
GStreamer-based inference processing system.

The primary goal of the system is to enable reliable, modular, and easily
extensible AI inference execution inside GStreamer pipelines.

## 📚 Documentation Map

[Generic Info](generic-info.md)  
An overviw of the project.

[Op Interface](op-system.md)  
Operation lifecycle, inference contracts, and execution semantics.

[OpChain Context](op-chain-context.md)  
Transient execution data model and runtime ownership rules.
