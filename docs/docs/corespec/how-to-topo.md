---
sidebar_position: 5
sidebar_label: How-To Topo
---

# AMP Development Forge Topo How-To

This guide shows the shortest path from cloning AMP with Topo to deploying it to a remote target over SSH.

The repository already includes Topo metadata in `compose.yaml`, so after cloning the project you can deploy it directly with the `topo` CLI.

## Prerequisites

- `topo` is installed on your host machine.
- Your target is reachable over SSH.
- Your target is ready to run the deployment.

The `--target` flag accepts either an SSH config host alias or a `user@host` destination.

## Clone the project with Topo

```bash
topo clone test git:https://github.com/Arm-Debug/amp-dev-forge.git
cd test
```

This creates a local `test` directory containing the AMP Development Forge project.

## Deploy to your target

```bash
topo deploy --target {ssh_target}
```

Replace `{ssh_target}` with your SSH target, for example:

```bash
topo deploy --target my-board
topo deploy --target ubuntu@192.168.1.20
```

## Optional check

If you want to verify connectivity before deployment, run:

```bash
topo health --target {ssh_target}
```

## Summary

For a standard Topo-based AMP deployment, the workflow is:

```bash
topo clone test git:https://github.com/Arm-Debug/amp-dev-forge.git
cd test
topo deploy --target {ssh_target}
```
