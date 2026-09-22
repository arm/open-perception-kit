

const video = document.getElementById("video");
const muteBtn = document.getElementById("muteUnmuteBtn");
const muteIcon = document.getElementById("muteUnmuteIcon");
const muteText = muteBtn.querySelector(".video-control-text");

muteBtn.addEventListener("click", () => {
    video.muted = !video.muted;

    if (video.muted) {
        muteIcon.textContent = "🔇";
        muteText.textContent = "Unmute";
        muteBtn.setAttribute("aria-label", "Unmute");
    } else {
        muteIcon.textContent = "🔊";
        muteText.textContent = "Mute";
        muteBtn.setAttribute("aria-label", "Mute");
    }
});

export function enableAudioButton(enable) {
    if(muteBtn) {
        muteBtn.style.display = (enable ? "inline-flex" : "none");
    }
}
