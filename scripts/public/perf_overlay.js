
import { ctrlSend } from "./ctrlws.js";

let overlayEnabled = true;

// handle disable Performance overlay button
const enablePerfOverlayBtn = document.getElementById('enablePerfOverlayBtn');

enablePerfOverlayBtn.addEventListener('click', function() {

    console.log('Toggling performance overlay to:', overlayEnabled);

    ctrlSend({type: "perf_overlay"});
});

export function setPerfOverlayButton(overlayEnabled ) {
    const icon = '<span class="btn-icon">👁</span> ';
    enablePerfOverlayBtn.innerHTML = icon + (overlayEnabled ? 'Disable Performance Overlay' : 'Enable Performance Overlay');
}
