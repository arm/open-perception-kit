
import { ctrlSend } from "./ctrlws.js";

// handle disable Performance overlay button
const enablePerfOverlayBtn = document.getElementById('enablePerfOverlayBtn');

enablePerfOverlayBtn.addEventListener('click', function() {

    console.log('Toggling performance overlay');

    ctrlSend({type: "perf_overlay"});
});

export function setPerfOverlayButton(overlayEnabled) {
    const icon = '<span class="btn-icon">👁</span> ';
    enablePerfOverlayBtn.innerHTML = icon + (overlayEnabled ? 'Disable Performance Overlay' : 'Enable Performance Overlay');
}
