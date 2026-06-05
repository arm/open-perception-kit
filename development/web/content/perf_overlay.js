
import { ctrlSend } from "./ctrlws.js?v=chrome-freeze-pause-20260605";

// handle Performance overlay toggle (checkbox)
const enablePerfOverlayChk = document.getElementById('enablePerfOverlayChk');

if (enablePerfOverlayChk) {
    enablePerfOverlayChk.addEventListener('change', function () {
        console.log('Toggling performance overlay');
        ctrlSend({ type: "perf_overlay" });
    });
}

export function setPerfOverlayButton(overlayEnabled) {
    // update checkbox state to reflect server value; keep same API name
    if (enablePerfOverlayChk) {
        enablePerfOverlayChk.checked = !!overlayEnabled;
    }
}
