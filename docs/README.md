# Perception XPK Docs

The Arm docs site source lives in `docs/public`. The docs config is `docs/public/docs-config.json`, and shared static assets live under `docs/public/static`.

## Run Locally

First, make sure you can authenticate with your container engine by following [this guide](https://docs.staging.devplatform.arm.com/arm-docs-github-action/getting-started/docker-auth/).

Make sure you are logged in and authenticated with Docker or Podman:

    echo "YOUR_GITHUB_TOKEN" | docker login ghcr.io -u "YOUR_GITHUB_USERNAME" --password-stdin
    # or
    echo "YOUR_GITHUB_TOKEN" | podman login ghcr.io -u "YOUR_GITHUB_USERNAME" --password-stdin

From the repository root, run:

```bash
./scripts/serve-docs.sh
```

The script serves `docs/public` with the Arm docs preview image and keeps `docs/public/static` available for `/img/...` paths. It preserves the existing local script URL, `http://localhost:3003`.

If you need to run the container manually, the equivalent command is:

    docker run --rm -it --pull always -p 3003:3000 \
      -v "$(pwd)/docs/public":/workspace/docs-site:ro \
      ghcr.io/arm-debug/arm-docs-github-action/local:latest

Then open:

    http://localhost:3003

Stop the preview server with `Ctrl+C`.

## Asset Paths

Because `docs/public` is mounted as the docs root, static assets must be inside `docs/public/static`.

For shared images, put files in `docs/public/static/img` and reference them from Markdown with site-root paths:

```md
![Example image](/img/example.png)
```

See [the docs system docs](https://docs.staging.devplatform.arm.com/arm-docs-github-action/) for more information.
