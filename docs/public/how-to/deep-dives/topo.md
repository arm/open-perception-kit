---
sidebar_position: 9
sidebar_label: Topo How-To
---

# Perception Experience Kit Topo How-To

As an alternative to using a Dev Container, deployment can be done directly with Topo.
This guide shows the shortest path from cloning Perception Experience Kit with Topo to deploying it to a remote target over SSH.

The repository already includes Topo metadata in `compose.yaml`, so after cloning the project you can deploy it directly with the `topo` CLI.

## What will you learn from this documentation?

If you follow this page successfully, you will learn how to clone Perception Experience Kit with Topo and deploy it to a remote target over SSH.

At the end of this page, you should have a deployed Perception Experience Kit workspace on a reachable target and a simple way to repeat that deployment flow.

## Prerequisites

- [topo](https://github.com/arm/topo) is installed on your host machine.
- Your target is reachable over SSH.
- Your target is ready to run the deployment.

The `--target` flag accepts either an SSH config host alias or a `user@host` destination.

## Clone the project with Topo

```bash
topo clone test git:https://github.com/Arm-Debug/perception-experience-kit.git
cd test
```

This creates a local `test` directory containing the Perception Experience Kit project.
If you already cloned the repository another way, you can also run `topo deploy` from that existing working tree.

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

For a standard Topo-based Perception Experience Kit deployment, the workflow is:

```bash
topo clone test git:https://github.com/Arm-Debug/perception-experience-kit.git
cd test
topo deploy --target {ssh_target}
```

## What should you have at the end of this document?

By the end of this page, you should have:

- a local clone prepared through Topo
- a reachable SSH target
- a successful `topo deploy` run to that target

Success looks like this: `topo` can clone the repository, reach the target, and complete the deployment without requiring the Dev Container workflow.
