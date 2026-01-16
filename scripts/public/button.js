
let overlayEnabled = true;

document.addEventListener('DOMContentLoaded', function() {
    // handle disable Performance overlay button
    const button = document.getElementById('postButton');

    if (!button) {
        console.error('postButton not found in DOM');
        return;
    }

    button.addEventListener('click', function() {
        overlayEnabled = !overlayEnabled;

        console.log('Toggling performance overlay to:', overlayEnabled);

        const icon = '<span class="btn-icon">👁</span> ';
        this.innerHTML = icon + (overlayEnabled ? 'Disable Performance Overlay' : 'Enable Performance Overlay');

        fetch('/ctrl', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ enabled: overlayEnabled })
        })
            .then(response => response.json())
            .then(data => {
                console.log('Server response:', data);
                if (data.status !== 'ok') {
                    console.error('Server returned error:', data);
                }
            })
            .catch(error => {
                console.error('Error:', error);
                overlayEnabled = !overlayEnabled;
                this.innerHTML = icon + (overlayEnabled ? 'Disable Performance Overlay' : 'Enable Performance Overlay');
            });
    });

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
});
