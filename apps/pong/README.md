# Face Pong

Browser Pong prototype controlled by AMP face metadata.

The app is a static site in this directory:
- `index.html`
- `styles.css`
- `app.js`

## Run

Serve the repository with Python from the repo root:

```bash
python3 -m http.server 8088
```

Then open:

```text
http://127.0.0.1:8088/apps/pong/
```

You can also serve just this directory:

```bash
cd apps/pong
python3 -m http.server 8088
```

Then open:

```text
http://127.0.0.1:8088/
```

## Metadata Connection

The page connects to AMP metadata over WebSocket. The current default value in the UI is:

```text
ws://127.0.0.1:7001/ws
```

If your metadata stream is exposed on a different host or port, change the field in the app before pressing `Connect`.

## Notes

- This is a static browser app. Serve it over HTTP rather than opening `index.html` directly from `file://`.
- The game uses the left and right halves of the source image only for the initial paddle lock.
- Once a paddle has a locked face ID, it follows that ID anywhere in the frame until you press `Reset round`.
- `Reset face lock` clears only that player’s paddle-to-face mapping so the next tracked face in that half can claim it.
- Both paddles may lock to the same tracked face ID.
- `Face filter alpha` controls the low-pass filtering on face Y input. Lower values are smoother but lag more; higher values respond faster.
- The app reads `detection.data.attributes.trackId` when present, and falls back to the current tracker text annotation format if needed.
- Use the `Mirror left/right` setting when the camera view is mirrored.
