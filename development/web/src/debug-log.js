import { copyTextWithFeedback, setCopyButtonAvailable } from './copy-utils.js?v=icon-copy-buttons-20260608';

const copyButton = document.getElementById('copyDebugLogBtn');
const logEl = document.getElementById('log');

function getLogText() {
    if (!logEl) return '';

    return Array.from(logEl.querySelectorAll('.log-line'))
        .map((line) => line.textContent.trim())
        .filter(Boolean)
        .join('\n');
}

async function copyLog() {
    if (!copyButton) return;

    const logText = getLogText();
    await copyTextWithFeedback(copyButton, logText, 'Copy');
}

function updateCopyButtonState() {
    setCopyButtonAvailable(copyButton, !!getLogText());
}

copyButton?.addEventListener('click', copyLog);

if (logEl) {
    new MutationObserver(updateCopyButtonState).observe(logEl, {
        childList: true,
        subtree: true,
        characterData: true,
    });
}

updateCopyButtonState();
