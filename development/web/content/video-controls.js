// video-controls.js

const playBtn = document.getElementById("playPauseBtn");
const playIcon = document.getElementById("playPauseIcon");
const playText = playBtn ? playBtn.querySelector(".video-control-text") : null;
const video = document.getElementById("video");
window.PEK_FEED_PAUSED = Boolean(window.PEK_FEED_PAUSED);
let feedPaused = window.PEK_FEED_PAUSED;
let lastToggleRequestedAt = 0;
const DUPLICATE_EVENT_MS = 350;

const renderPlayPause = () => {
    if (!playBtn || !playIcon || !playText) return;

    feedPaused = Boolean(window.PEK_FEED_PAUSED);
    document.body.classList.toggle("is-feed-paused", feedPaused);
    playIcon.classList.toggle("fa-pause", !feedPaused);
    playIcon.classList.toggle("fa-play", feedPaused);
    playText.textContent = feedPaused ? "Resume" : "Pause";
    playBtn.setAttribute("aria-label", feedPaused ? "Resume feed" : "Pause feed");
    playBtn.disabled = false;
    playBtn.style.opacity = "";
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

export const setPlayPause = () => {
    renderPlayPause();
};

const requestPlayPause = async (event) => {
    event?.preventDefault();

    const now = Date.now();
    if (now - lastToggleRequestedAt < DUPLICATE_EVENT_MS) return;
    lastToggleRequestedAt = now;

    feedPaused = !feedPaused;
    window.PEK_FEED_PAUSED = feedPaused;

    if (feedPaused) {
        freezeFeedFrame();
    } else {
        await resumeFeedFrame();
    }

    renderPlayPause();
};

playBtn?.addEventListener("pointerdown", requestPlayPause);
playBtn?.addEventListener("mousedown", requestPlayPause);
playBtn?.addEventListener("pointerup", requestPlayPause);
playBtn?.addEventListener("click", requestPlayPause);
renderPlayPause();
