async function writeClipboard(text) {
    if (navigator.clipboard?.writeText) {
        try {
            await navigator.clipboard.writeText(text);
            return;
        } catch {
            // Plain HTTP origins need the document fallback below.
        }
    }

    const buffer = document.createElement('textarea');
    buffer.value = text;
    buffer.readOnly = true;
    buffer.style.position = 'fixed';
    buffer.style.opacity = '0';
    document.body.appendChild(buffer);
    const activeElement = document.activeElement;
    let copied = false;

    try {
        buffer.focus();
        buffer.select();
        copied = document.execCommand('copy');
    } finally {
        buffer.remove();
        activeElement?.focus?.();
    }

    if (!copied) throw new Error('Clipboard write failed');
}

function setButtonIcon(button, iconName) {
    const icon = button?.querySelector('i');
    if (!icon)
        return;

    icon.className = `fa-solid fa-${iconName}`;
}

function setButtonFeedback(button, state, label) {
    button.dataset.copyState = state;
    button.setAttribute('aria-label', label);
    button.title = label;
    setButtonIcon(button, state === 'copied' ? 'check' : 'copy');
}

export async function copyTextWithFeedback(button, text, emptyText = 'Empty') {
    if (!button) return;
    if (!String(text || '').trim()) return;

    const originalLabel = button.getAttribute('aria-label') || button.title || 'Copy';
    if (button.copyFeedbackTimer) {
        clearTimeout(button.copyFeedbackTimer);
        button.copyFeedbackTimer = null;
    }
    button.dataset.copyState = 'copying';

    try {
        await writeClipboard(text);
        setButtonFeedback(button, text ? 'copied' : 'empty', text ? 'Copied' : emptyText);
    } catch (error) {
        console.debug('Clipboard copy failed', error);
        setButtonFeedback(button, 'failed', 'Copy failed');
    }

    button.copyFeedbackTimer = setTimeout(() => {
        setButtonFeedback(button, 'idle', originalLabel);
        button.copyFeedbackTimer = null;
    }, 1500);
}

export function setCopyButtonAvailable(button, available) {
    if (!button) return;

    button.disabled = !available;
    button.setAttribute('aria-disabled', available ? 'false' : 'true');
}
