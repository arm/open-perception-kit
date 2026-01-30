// video-controls.js

import { ctrlSend } from "./ctrlws.js";


const playBtn = document.getElementById("playPauseBtn");
const playIcon = document.getElementById("playPauseIcon");
const playText = playBtn ? playBtn.querySelector(".video-control-text") : null;

const setBusy = (busy) => {
    playBtn.disabled = busy;
    playBtn.style.opacity = busy ? "0.7" : "";
};

export const setPlayPause = (isPlaying) => {
    if (!playBtn || !playIcon || !playText) return;

    playIcon.textContent = isPlaying ? "⏸" : "▶";
    playText.textContent = isPlaying ? "pause" : "play";
    playBtn.setAttribute("aria-label", isPlaying ? "Pause pipeline" : "Play pipeline");

    setBusy(false);
};

playBtn.addEventListener("click", () => {
    setBusy(true);

    ctrlSend({type: "play_pause"});
});
