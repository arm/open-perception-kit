---
sidebar_position: 5
sidebar_label: Inference Engines
---

# Inference Engines Overview

This page summarizes the current status of supported and planned inference engines within the GStreamer-based inference framework. The goal is to support both industry-standard runtimes and emerging or hardware-accelerated backends, enabling flexibility across platforms from embedded devices to edge AI systems.

---

## Status Summary

| Engine       | Status        | Why |
|--------------|--------------|------|
| ONNX Runtime | Working      | Industry standard, broad model compatibility and ecosystem support |
| HailoRT      | Working      | Hardware acceleration on Raspberry Pi 5 (Hailo AI accelerator support) |
| ExecuTorch   | Experimental | Potential strong future runtime for edge deployments |
| MNN          | Planned      | Versatile backend with OpenCL and Vulkan support |

---

## Detailed Rationale

### ONNX Runtime — **Working**

ONNX Runtime is widely considered an industry-standard inference engine. It provides:

- Strong interoperability via the ONNX model format  
- Support for models exported from PyTorch, TensorFlow, and many other frameworks  
- Mature graph optimization and execution provider architecture  
- Broad CPU and accelerator support 
- Dynamic tensor support

Including ONNX Runtime ensures:

- Immediate compatibility with a large ecosystem of existing models  
- Stability and production readiness  
- A reliable baseline backend for cross-platform validation  

ONNX serves as the reference backend due to its maturity and broad industry adoption.

---

### HailoRT — **Working**

HailoRT enables hardware-accelerated inference using the Hailo AI accelerator, particularly relevant for Raspberry Pi 5 + Hailo configurations.

Key reasons for support:

- Dedicated AI acceleration significantly reduces CPU load  
- Enables real-time, low-latency inference pipelines  
- Improves performance-per-watt for edge deployments  
- Demonstrates hardware offload capability within GStreamer pipelines  

HailoRT integration allows the framework to take advantage of specialized edge AI hardware, making it suitable for production-grade embedded deployments.

---

### ExecuTorch — **Experimental**

ExecuTorch is a lightweight runtime designed for deploying PyTorch models on edge devices. While still evolving, it represents a strategically important direction.

Reasons for experimental integration:

- Strong alignment with the PyTorch ecosystem  
- Designed specifically for edge and embedded deployments  
- Potential to become a major standard for PyTorch-native inference  
- Lightweight architecture suitable for constrained environments  

Although not yet fully mature, ExecuTorch is included experimentally to evaluate its long-term viability and to prepare for potential future adoption.

---

### MNN — **Planned**

MNN (Mobile Neural Network) is a high-performance, lightweight inference engine designed for mobile and embedded devices.

Reasons for planned integration:

- Support for multiple hardware acceleration backends  
- OpenCL and Vulkan support for GPU acceleration  
- Good cross-platform portability  
- Optimized for performance on edge-class devices  
- Dynamic tensor support

MNN’s versatility and GPU acceleration capabilities make it an attractive future backend, particularly for systems where OpenCL or Vulkan acceleration is preferred.
