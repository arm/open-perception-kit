// video-controls.js

import { ctrlSend } from "./ctrlws.js";

const playBtn = document.getElementById("playPauseBtn");
const playIcon = document.getElementById("playPauseIcon");
const playText = playBtn ? playBtn.querySelector(".video-control-text") : null;
const video = document.getElementById("video");

let isPipelinePlaying = true;
let lastToggleRequestedAt = 0;
const DUPLICATE_EVENT_MS = 350;

const setBusy = (busy) => {
    if (!playBtn) return;

    playBtn.disabled = busy;
    playBtn.style.opacity = busy ? "0.7" : "";
};

const renderPlayPause = () => {
    if (!playBtn || !playIcon || !playText) return;

    const isPaused = !isPipelinePlaying;
    document.body.classList.toggle("is-feed-paused", isPaused);
    playIcon.classList.toggle("fa-pause", isPipelinePlaying);
    playIcon.classList.toggle("fa-play", isPaused);
    playText.textContent = isPaused ? "Resume" : "Pause";
    playBtn.setAttribute("aria-label", isPaused ? "Resume pipeline" : "Pause pipeline");
    setBusy(false);
};

const dispatchPauseState = () => {
    window.dispatchEvent(new CustomEvent("feed-pause-change", {
        detail: { paused: Boolean(window.PEK_FEED_PAUSED) },
    }));
};

const freezeFeedFrame = () => {
    if (!video) return;

    const wrapper = video.closest(".video-wrapper");
    if (!wrapper) return;

    let canvas = document.getElementById("videoFreezeFrame");
    if (!canvas) {
        canvas = document.createElement("canvas");
        canvas.id = "videoFreezeFrame";
        canvas.className = "video-freeze-frame";
        canvas.setAttribute("aria-hidden", "true");
        wrapper.appendChild(canvas);
    }

    const width = video.videoWidth || Math.max(1, Math.round(video.clientWidth || wrapper.clientWidth || 1280));
    const height = video.videoHeight || Math.max(1, Math.round(video.clientHeight || wrapper.clientHeight || 720));
    canvas.width = width;
    canvas.height = height;

    try {
        const context = canvas.getContext("2d");
        context?.drawImage(video, 0, 0, width, height);
    } catch (error) {
        console.warn("Video freeze frame failed", error);
    }

    canvas.classList.add("is-visible");
};

const resumeFeedFrame = async () => {
    const canvas = document.getElementById("videoFreezeFrame");
    canvas?.classList.remove("is-visible");

    try {
        if (video?.paused) {
            await video.play();
        }
    } catch (error) {
        console.warn("Video resume failed", error);
    }
};

export const setPlayPause = (isPlaying) => {
    if (typeof isPlaying === "boolean") {
        isPipelinePlaying = isPlaying;
        window.PEK_FEED_PAUSED = !isPipelinePlaying;

        if (isPipelinePlaying) {
            void resumeFeedFrame();
        } else {
            freezeFeedFrame();
        }

        renderPlayPause();
        dispatchPauseState();
        return;
    }

    renderPlayPause();
};

const requestPlayPause = (event) => {
    event?.preventDefault();

    const now = Date.now();
    if (now - lastToggleRequestedAt < DUPLICATE_EVENT_MS) return;
    lastToggleRequestedAt = now;

    setBusy(true);
    ctrlSend({ type: "play_pause" });
};

playBtn?.addEventListener("click", requestPlayPause);
renderPlayPause();
