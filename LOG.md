# ⚡ AMP .plan file

---

### 080925: Initial considerations

📌 No real target hardware for a long time

- Development in container (not so compatible)
- Development on ARM boards (slooow for everyday dev)
- Linux PC with AMD GPU (AMD drivers follow standards, unlike NVIDIA)

💡 Considerations

- Use standard 'memory:DMABuf' capsfeature to achieve zero-copy
- Continous research in all GStreamer-related topics  😓

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

- Latency-Aware Zero-copy Execution Rig
- Tool written in python
- Mostly runs system utilities and parses output
- Gives system diagnostics in the perspective of GStreamer capabilities, hardware acceleration, drivers

💡 Considerations

- Do we need a common-code-framework for our elements?
- We should map all the baseline elements that has memory:DMABuf support and use them

📋 TODO

- Sanity check script for system components: GStreamer, EGL, simple zero-copy pipelines, ...
- Maybe sanity checks should be done by Lazer?

✅ COMPLETED

- Git repo
- Basic Dockerfile + devcontainer.json

---

### 100925

📌 Paralel development in aarc64 and x64 containers seems to be possible

💡 Considerations

- Drop Python tooling and use C++ instead?
  - C++ would be simpler, no Python devenv, etc
  - Maybe later the exp kit will need a Python CLI anyway

📋 TODO

- Should create an easy way for IDE debugging

✅ COMPLETED

- Removed aarc64 from Dockerfile, container builds to Mac ➡️ aarch64, Linux PC ➡️ x64
- Dummy element skeleton added
- Basic build system for elements with meson + ninja (development/command.sh)
- Container fully working on Linux PC (Bazzite + KDE + Podman), host Wayland provides visible frames

---

### 110925

📌 The Q3 demo is near and doing a demo is not the best direction for development, but maybe reachable.

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

- Build and test scripts in ./scripts
- Some script celanup and formating implemented

---

