---
sidebar_label: Colima on macOS
---

# Colima Setup on macOS (Before Building the Container)

Use this short guide to prepare Docker on macOS with Colima before opening or building the dev container.

## 1. Install Homebrew (if needed)

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

## 2. Install required tools

```bash
brew install docker colima docker-compose docker-buildx
```

- `docker`: Docker CLI client
- `colima`: container runtime VM backend
- `docker-compose`, `docker-buildx`: required for devcontainers and image builds

## 3. Start Colima

Minimal start:

```bash
colima start
```

Recommended for better devcontainer performance:

```bash
colima start --cpu 4 --memory 8 --mount-type=virtiofs
```

- `--cpu` and `--memory` allocate VM resources
- `--mount-type=virtiofs` improves file sharing speed for source mounts and builds

## 4. Select Colima as Docker backend

```bash
docker context use colima
```

Why this matters: Docker CLI is only a client and must target the correct daemon context.
If the wrong context is active, commands can fail with `Cannot connect to the Docker daemon`.

Check contexts:

```bash
docker context ls
```

Verify that `colima` is the active context.

## 5. Verify setup

```bash
docker ps
```

If this runs without connection errors, your setup is ready for container build and devcontainer workflows.

## Quick command summary

```bash
brew install docker colima docker-compose docker-buildx
colima start --cpu 4 --memory 8 --mount-type=virtiofs
docker context use colima
docker ps
```
