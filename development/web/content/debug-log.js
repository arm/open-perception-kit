import { copyTextWithFeedback, setCopyButtonAvailable } from './copy-utils.js?v=icon-copy-buttons-20260608';

const section = document.querySelector('[data-debug-log-section]');
const toggleButton = document.getElementById('debugLogToggle');
const copyButton = document.getElementById('copyDebugLogBtn');
const logEl = document.getElementById('log');
const copyBuffer = document.getElementById('debugLogCopyBuffer');

function setCollapsed(collapsed) {
    if (!section || !toggleButton) return;

    section.classList.toggle('is-collapsed', collapsed);
    toggleButton.setAttribute('aria-expanded', String(!collapsed));
}

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
    copyBuffer?.classList.remove('is-visible');
    await copyTextWithFeedback(copyButton, logText, 'Copy', copyBuffer);
}

function updateCopyButtonState() {
    setCopyButtonAvailable(copyButton, !!getLogText());
}

toggleButton?.addEventListener('click', () => {
    setCollapsed(!section.classList.contains('is-collapsed'));
});

copyButton?.addEventListener('click', copyLog);

if (logEl) {
    new MutationObserver(updateCopyButtonState).observe(logEl, {
        childList: true,
        subtree: true,
        characterData: true,
    });
}

updateCopyButtonState();
