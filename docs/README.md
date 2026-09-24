# Open Perception Kit Docs

The public Arm docs site source lives in `docs/public`. The docs config is `docs/public/docs-config.json`, and shared static assets live under `docs/public/static`.

Developer-facing architecture documentation lives in `docs/arch`.

PlantUML sources live in `docs/plantuml`. Generated PNGs are written to `docs/public/static/img` so public pages can use them with `/img/...` paths.

## Run Locally

First, make sure you can authenticate with your container engine by following [this guide](https://qa.developer.arm.com/dev-docs/arm-docs-github-action/getting-started/docker-auth/).

Make sure you are logged in and authenticated with Docker or Podman:

    echo "YOUR_GITHUB_TOKEN" | docker login ghcr.io -u "YOUR_GITHUB_USERNAME" --password-stdin
    # or
    echo "YOUR_GITHUB_TOKEN" | podman login ghcr.io -u "YOUR_GITHUB_USERNAME" --password-stdin

From the repository root, run:

```bash
./scripts/serve-docs.sh
```

The documentation scripts use `OPK_PROJECT_ROOT` when it is set and otherwise
derive the checkout from their own location. Set it to an absolute path when
invoking the scripts for a different checkout.

The script serves `docs/public` with the Arm docs preview image and keeps `docs/public/static` available for `/img/...` paths. It preserves the existing local script URL, `http://localhost:3003`.

If you need to run the container manually, the equivalent command is:

    docker run --rm -it --pull always -p 3003:3000 \
      -v "$(pwd)/docs/public":/workspace/docs-site:ro \
      ghcr.io/arm-debug/arm-docs-github-action/local:latest

Then open:

    http://localhost:3003

Stop the preview server with `Ctrl+C`.

## Asset Paths

Because `docs/public` is mounted as the docs root, published static assets must be inside `docs/public/static`.

For shared images, put files in `docs/public/static/img` and reference them from Markdown with site-root paths:

```md
![Example image](/img/example.png)
```

For generated diagrams, put the `.puml` source in `docs/plantuml` and run `./scripts/gen-doc.sh`.

See [the docs system docs](https://qa.developer.arm.com/dev-docs/arm-docs-github-action/) for more information.
