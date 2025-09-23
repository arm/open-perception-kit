# ⚡ AMP .plan file

---

### 080925: Initial considerations

📌 No real target hardware for a long time

The original idea was to do the development in a container, its disadvantage is that not fully compatible with the final hardware.
We can do the development on already available ARM boards, the problem is that mostly they are to slooow to reflect the final performance.
We also can use a Linux PC with AMD GPU or Intel GPU, that ones follow standards unlike NVidia.

💡 Considerations

- Use standard 'Memory:DMABuf' capsfeature to achieve zero-copy
- We need continous research in all GStreamer-related topics  😓
- Most important Linux tech stack elements here: DMA-BUF, DRM/KMS, GBM, EGL and of course OpenGL + Vulkan

📋 TODO

- Git repo
- Development container with Python for tooling and C++ for building elements
- Setup a project for a Python tool that helps to get information about GStremer on the current host: **Lazer**
- Create a container that is aarch64 by default
- If possible, an option for the container to use x64 Linux (for development on the PC)
- Expose required devices to container to support hardware features 

---

### 090925

🔫 Lazer

Lazer is a companion tool for the while development process of AMP and also could be a generic tool for the future developers using AMP.
Lazer is written in Python, the container helps to host it and also to to improve it with new functions.
Lazer should help the user to discover a given system: what hardver acceleration capabilities are available, GStreamer element list, driver availability.
Lazer also should help to create new elements.
The awkward backronym is 'Latency-Aware Zero-copy Execution Rig'.

💡 Considerations

- Do we need a common-code-framework for our elements? An utility framework?
- We should map all the baseline elements that has Memory:DMABuf support and use them

📋 TODO

- Sanity check script for system components: GStreamer, EGL, simple zero-copy pipelines, ...
- Maybe sanity checks should be done by Lazer?

✅ COMPLETED

- Git repo
- Basic Dockerfile + devcontainer.json

---

### 100925

📌 Paralel development in aarc64 and x64 containers seems to be possible

The containers are working, x64 on Linux PC, aarch64 on MacOS, the project builds and runs on both.
It seems that development on a Linux PC with good drivers and standard-following compontents is possible to solve the lack-of-hardware problem.

💡 Considerations

- Drop Python tooling and use C++ instead?
  - C++ would be simpler, no Python devenv, etc..
  - Maybe later the exp kit will need a Python CLI anyway

📋 TODO

- Should create an easy way for trace debugging via VSCode IDE

✅ COMPLETED

- Removed aarc64 from Dockerfile, container builds on Mac ➡️ aarch64, on Linux PC ➡️ x64
- Dummy element skeleton added
- Basic build system for elements with meson + ninja (development/command.sh)
- Container fully working on Linux PC (Bazzite + KDE + Podman), host Wayland provides visible frames

---

### 110925

📌 Conisdering a Q3 demo, preparing for that is not the best direction for development, but maybe reachable.

💡 Considerations

- Demo requires inference element with inference engine integration
- Also some drawing functionality is required
- YOLO object detection?
- Face detection?

📋 TODO

- An 🧠 **ampinfer** element
- Add ONNX dependencies to the container
- Implement a code that can access the pixels of the video frame traveling down the pipeline

✅ COMPLETED

- Visible frames on MacOS (important to demo the system)

---

### 120925

💡 Considerations

- Pipeline element negotiation seems very complex, additional research is required 🔎

📋 TODO

- Fix the inference
- The inference pipeline is very slow, hopefully display tunneling causes this
- The ximagesink tunneling is so fragile, that some network streaming based display must be tested

✅ COMPLETED

- The yolov8n model is added to the project (fp32 + int8)
- ONNX dependencies added to the container
- The ampinfer element is basically done for a simple usecase
- But does not work ☹️

---

### 130925: First inference working

💡 Considerations

- Now we go on with UDP casting, host runs video client, container runs server (in-pipeline)
- This is a start-and-forget lazy-coupled client-server video display for development  

📋 TODO

- Cleanup ampinfer code

✅ COMPLETED

- Testvideo added to git
- The fragile image tunneling (ximagesing, waylandsink) to host is removed
- Video playback via UDP casting is stable (+portable), ximagesink tunneling removed
- ONNX yolov8n is working in-pipeline

---

### 160925

✅ COMPLETED

- Build and test scripts in **./scripts**
- Some script celanup and formating implemented

---

### 220925

🧠 Inference input data

A possible solution for inference data generation is to create a format for input tensor data. 
The quantized input tensor data could be donwstreamed together with the original frame.
There is also a memory type **GLMemory** in GStreamer that is crucial for our usecases.

📦 Existing OpenGL elements can be handy for inference preprocessing

- Color-space conversion (and scaling): **glcolorconvert** (e.g. NV12↔RGBA) and also resize 
- Transforms/crop-like operations: **gltransformation**
- Compositing/overlay (OSD-style): **glvideomixer** (mix multiple GPU streams/layers with positioning and alpha)
- Custom GPU ops: **glshader** (to inject custom pixel shader)

📋 TODO

- Container generator script for supported platforms (to tunnel different hardware to the container on different hosts)

