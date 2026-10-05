/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */



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
