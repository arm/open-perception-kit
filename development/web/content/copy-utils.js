async function writeClipboard(text) {
    if (!navigator.clipboard?.writeText) {
        throw new Error('Clipboard API is unavailable');
    }

    try {
        await navigator.clipboard.writeText(text);
    } catch (error) {
        throw new Error('Clipboard write failed', { cause: error });
    }
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

export async function copyTextWithFeedback(button, text, emptyText = 'Empty', fallbackBuffer = null) {
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
        if (fallbackBuffer) {
            fallbackBuffer.value = text;
            fallbackBuffer.classList.add('is-visible');
            fallbackBuffer.focus();
            fallbackBuffer.select();
            fallbackBuffer.setSelectionRange(0, fallbackBuffer.value.length);
            setButtonFeedback(button, 'manual-copy', 'Select text to copy');
        } else {
            setButtonFeedback(button, 'failed', 'Copy failed');
        }
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
