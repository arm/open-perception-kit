---
slug: /
sidebar_position: 1
sidebar_label: Overview
---

# Documentation workspace index

This file tracks the current documentation layout inside the repository.

The actively maintained Docusaurus content now lives under:

- [public/arch/index.md](public/arch/index.md)
- [public/how-to/deep-dives/how-to.md](public/how-to/deep-dives/how-to.md)
- [public/how-to/quick-guides/how-to-win-lin-quick-guide.md](public/how-to/quick-guides/how-to-win-lin-quick-guide.md)

The supporting assets live under:

- [static](static)

## Current layout

- [public/arch](public/arch) — architecture and implementation-facing documentation
- [public/arch/elements](public/arch/elements) — element-specific documentation
- [public/how-to/deep-dives](public/how-to/deep-dives) — fuller user and integrator guides
- [public/how-to/quick-guides](public/how-to/quick-guides) — shortest first-run guides
- [static](static) — shared images and PlantUML sources

## Notes for maintainers

- `scripts/serve-docs.sh` should serve `docs/public` together with `docs/static`.
- `scripts/gen-doc.sh` should generate plain HTML from `docs/public`.
- Legacy references to `docs/content`, `docs/docs`, or `docs/docs/corespec` should be treated as outdated.
