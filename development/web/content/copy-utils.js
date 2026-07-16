async function writeClipboard(text) {
    try {
        if (navigator.clipboard?.writeText) {
            await navigator.clipboard.writeText(text);
            return;
        }
    } catch (error) {
        // Fall back below for non-secure origins or denied clipboard access.
    }

    const textarea = document.createElement('textarea');
    textarea.value = text;
    textarea.setAttribute('readonly', '');
    textarea.style.position = 'fixed';
    textarea.style.top = '0';
    textarea.style.left = '0';
    textarea.style.width = '1px';
    textarea.style.height = '1px';
    textarea.style.opacity = '0';
    document.body.appendChild(textarea);
    textarea.focus();
    textarea.select();
    textarea.setSelectionRange(0, textarea.value.length);

    const copied = document.execCommand('copy');
    textarea.remove();

    if (!copied) {
        throw new Error('Copy command failed');
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
        if (fallbackBuffer) {
            fallbackBuffer.value = text;
            fallbackBuffer.classList.add('is-visible');
            fallbackBuffer.focus();
            fallbackBuffer.select();
            fallbackBuffer.setSelectionRange(0, fallbackBuffer.value.length);
        }
        setButtonFeedback(button, text ? 'copied' : 'empty', text ? 'Copied' : emptyText);
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
