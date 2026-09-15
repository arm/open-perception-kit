# SDK Generator Sources

This directory contains ordinary tracked source files for the SDK generator,
including C++, Python, Rust, TypeScript, CMake, Meson, and the embedded Python
bridge. The generator identity is 0.6.0, independent of the generated Perception
SDK's product version.

No separate checkout, credentials, or generator download is required. Source
updates are manual repository changes; there is no fetch or update command.
Generation receipts record the local Python source files and their hashes so
changes require regeneration just like other generation inputs.

From the repository root, use the supported container workflow:

```bash
./scripts/perception-sdk.sh generate
./scripts/perception-sdk.sh check
python3 tools/perception/tests/test_release.py
```

The descriptor at `tools/perception/sdk.json` selects this directory, the
generator entrypoint, the Perception schema set, and output locations. Do not
edit generated SDK files or receipts by hand.

- [Perception SDK workflow](../../docs/public/how-to/use-perception-sdk.md)
- [Generator commands and contracts](tools/flowdata/README.md)
- [Language API guides](docs/README.md)
- [Container model](HETEROGENEOUS_CONTAINER.md)
- [Generator limitations](KNOWN_LIMITATIONS.md)

The language guides retain illustrative `metapoc` examples. Their demo schemas
and applications are not shipped here; PEK uses `schemas/perception/metadata/`.
Repository contribution and source-header rules apply to these sources.
