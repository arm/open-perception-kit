# Perception XPK
![PEK CI Pipeline Nightly](https://github.com/Arm-Debug/perception-experience-kit/actions/workflows/pek-ci.yml/badge.svg?branch=main)
![SonarQube Nightly](https://github.com/Arm-Debug/perception-experience-kit/actions/workflows/sonar.yml/badge.svg?branch=main)

The Perception XPK is a framework for building and running AI-enabled media workflows.

For example, you could use it for:
- creating a smart camera doorbell that recognizes when one or more people are present and responds in real time
- creating tools that read labels, signs, or documents and act based on the text found

Use it to prototype, test and deploy AI pipelines, turning raw media into meaningful output. It includes models for object detection, classification, tracking, segmentation, text recognition, voice activity detection and similar perception use cases.

At a high level, the Perception XPK combines:
- GStreamer-based media pipeline integration
- an Op-based execution model for preprocessing, inference, and postprocessing
- structured `Perception` results that downstream elements can render, track, or publish

## Get started
- Run on [Raspberry Pi 5](docs/public/how-to/quick-guides/rpi.md) (recommended)
- Run locally with the [macOS quick guide](docs/public/how-to/quick-guides/mac.md) or [Windows/Linux quick guide](docs/public/how-to/quick-guides/win-lin.md) / [tutorial video](https://armh.sharepoint.com/:v:/s/StrategyandEcosystems/IQCzFz6fgiUMSoOBeC9le6q-AYEuQd2FaDPcQFjF3_r5Y5g?nav=eyJyZWZlcnJhbEluZm8iOnsicmVmZXJyYWxBcHAiOiJTdHJlYW1XZWJBcHAiLCJyZWZlcnJhbFZpZXciOiJTaGFyZURpYWxvZy1MaW5rIiwicmVmZXJyYWxBcHBQbGF0Zm9ybSI6IldlYiIsInJlZmVycmFsTW9kZSI6InZpZXcifX0%3D)
- After running your first pipeline, use the [Pipeline customisation guide](docs/public/how-to/quick-guides/exercise.md) to learn how to make your own and integrate a model

## Advanced paths

### Deep dive how-to pages
The deep-dive path branches by platform and Raspberry Pi accelerator choice, then continues into model integration, custom postprocessing, and the architecture pages.

- [Deep dive how-to guide](docs/public/how-to/deep-dives/index.md)
- [Engineering starting point](docs/public/how-to/deep-dives/engineering.md)
- [Bring your model](docs/public/how-to/deep-dives/bring-your-model.md)
- [Troubleshooting](docs/public/how-to/deep-dives/troubleshooting.md)
- [Performance measurement with performix](docs/public/how-to/deep-dives/performix-setup.md)

### Architecture and implementation detail
The architectural path provides a deeper understanding of the project, its current capabilities, and its future goals.

- [Architecture index](docs/public/arch/index.md)
- [Architectural overview](docs/public/arch/architectural-overview.md)
- [Known limitations](docs/public/arch/known-limitations.md)

For rendered documentation instead of Markdown files, see the [Perception XPK Docusaurus Site](https://docs.staging.devplatform.arm.com/amp-dev-forge/).