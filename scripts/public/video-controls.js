// video-controls.js
(() => {
  const btn = document.getElementById("playPauseBtn");
  const icon = document.getElementById("playPauseIcon");
  const text = btn ? btn.querySelector(".video-control-text") : null;

  if (!btn || !icon || !text) return;

  const setUi = (state) => {
    const isPlaying = state === "playing";
    icon.textContent = isPlaying ? "⏸" : "▶";
    text.textContent = isPlaying ? "Pause" : "Play";
    btn.setAttribute("aria-label", isPlaying ? "Pause pipeline" : "Play pipeline");
  };

  const setBusy = (busy) => {
    btn.disabled = busy;
    btn.style.opacity = busy ? "0.7" : "";
  };

  btn.addEventListener("click", async () => {
    setBusy(true);

    try {
      const r = await fetch("/play-pause", { method: "POST" });
      if (!r.ok) throw new Error(`HTTP ${r.status}`);

      const data = await r.json();
      if (!data || data.ok !== true) throw new Error("Bad response");

      // expected: { ok: true, state: "playing" | "paused" }
      setUi(data.state);
    } catch (e) {
      console.error("play/pause failed:", e);
      appendLog(`[control] play/pause failed: ${e.message || e}`, "error");
    } finally {
      setBusy(false);
    }
  });

  // Initial UI state (assume playing unless server says otherwise)
  // If you later add GET /state, replace this with a fetch to initialize.
  setUi("playing");
})();
