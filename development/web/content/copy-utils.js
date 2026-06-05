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

function setButtonText(button, text) {
    const label = button?.querySelector('span');
    if (label) {
        label.textContent = text;
    } else if (button) {
        button.textContent = text;
    }
}

export async function copyTextWithFeedback(button, text, emptyText = 'Empty', fallbackBuffer = null) {
    if (!button) return;
    if (!String(text || '').trim()) return;

    const label = button.querySelector('span');
    const originalText = label ? label.textContent : button.textContent;
    button.dataset.copyState = 'copying';

    try {
        await writeClipboard(text);
        setButtonText(button, text ? 'Copied' : emptyText);
        button.dataset.copyState = text ? 'copied' : 'empty';
    } catch (error) {
        if (fallbackBuffer) {
            fallbackBuffer.value = text;
            fallbackBuffer.classList.add('is-visible');
            fallbackBuffer.focus();
            fallbackBuffer.select();
            fallbackBuffer.setSelectionRange(0, fallbackBuffer.value.length);
        }
        setButtonText(button, text ? 'Selected' : emptyText);
        button.dataset.copyState = text ? 'selected' : 'empty';
    }

    setTimeout(() => {
        setButtonText(button, originalText || 'Copy');
        button.dataset.copyState = 'idle';
    }, 2500);
}

export function setCopyButtonAvailable(button, available) {
    if (!button) return;

    button.disabled = !available;
    button.setAttribute('aria-disabled', available ? 'false' : 'true');
}
