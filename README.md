# AMP Development Forge
![AMP CI Pipeline Nightly](https://github.com/Arm-Debug/amp-dev-forge/actions/workflows/amp-ci.yml/badge.svg?branch=main)
![SonarQube Nightly](https://github.com/Arm-Debug/amp-dev-forge/actions/workflows/sonar.yml/badge.svg?branch=main)

---

# (AMP Development Forge)[https://github.com/Arm-Debug/amp-dev-forge]

(AMP Development Forge)[https://github.com/Arm-Debug/amp-dev-forge] is a framework for building and running AI-enabled media workflows. A development environment for **AI media processing pipelines**, built for rapid testing, debugging, and deployment within containerized platforms.
It is built to help teams move from raw input to meaningful output in a clear and repeatable way. Instead of treating each new use case as a one-off effort, the package provides a stable structure that can be reused and adapted.

At a high level, it combines:
- GStreamer-based media pipeline integration
- an Op-based execution model for preprocessing, inference, and postprocessing
- structured `Perception` results that downstream elements can render, track, or publish

## For first time users
For rendered documentation instead of markdowns check out the [AMP dev forge docusaurus documentation](https://docs.staging.devplatform.arm.com/amp-dev-forge/)

### Quick first run
The quick-guide path is the fastest way to reach a working pipeline and then reconnect through the [Exercise Quick Guide](how-to/quick-guides/exercise.md) into the same engineering path as the full setup flow.

- [Windows/Linux quick guide](how-to/quick-guides/win-lin.md)
- [macOS quick guide](how-to/quick-guides/mac.md)
- [Raspberry Pi quick guide](how-to/quick-guides/rpi.md)
- Additionaly to the quick-guides the following [tutorial video](https://armh.sharepoint.com/:v:/s/StrategyandEcosystems/IQCzFz6fgiUMSoOBeC9le6q-AYEuQd2FaDPcQFjF3_r5Y5g?nav=eyJyZWZlcnJhbEluZm8iOnsicmVmZXJyYWxBcHAiOiJTdHJlYW1XZWJBcHAiLCJyZWZlcnJhbFZpZXciOiJTaGFyZURpYWxvZy1MaW5rIiwicmVmZXJyYWxBcHBQbGF0Zm9ybSI6IldlYiIsInJlZmVycmFsTW9kZSI6InZpZXcifX0%3D) follows through a usual path of a new user

## Advanced paths

### Deep dive how-to pages
The deep-dive path branches by platform and Raspberry Pi accelerator choice, then continues into model integration, custom postprocessing, and the architecture pages.

- [Main how-to guide](how-to/deep-dives/index.md)
- [Engineering starting point](how-to/deep-dives/engineering.md)
- [Bring your model](how-to/deep-dives/bring-your-model.md)
- [Performance measuremetn with performix](how-to/deep-dives/performix-setup.md)

### Architecture and implementation detail
The architectural path should provide a deeper understanding of the project it's current capabilities and future goals.

- [Architecture index](arch/index.md)
- [Architectural overview](arch/architectural-overview.md)
- [Known limitations](arch/known-limitations.md)
