# ⚡ AMP .plan file

---

### 080925: Initial considerations

📌 No real target hardware for a long time

- Development in container (not so compatible)
- Development on ARM boards (slooow)
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

🔫 lazer

- Latency-Aware Zero-copy Execution Rig
- Tool written in python
- Mostly runs system utilities and parses output
- Gives system diagnostics in the perspective of GStreamer capabilities, hardware acceleration, drivers

💡 Considerations

- Do we need a common-code-framework for our elements? 

📋 TODO

- Sanity check script for system components: GStreamer, EGL, simple zero-copy pipelines, ...
- Maybe sanity checks should be done by Lazer?

✅ COMPLETED

- Git repo
- Basic Dockerfile + devcontainer.json

---



