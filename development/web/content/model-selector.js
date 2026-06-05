const section = document.querySelector('[data-model-selector-section]');
const toggle = document.getElementById('modelSelectorToggle');

function setExpanded(expanded) {
    if (!section || !toggle) return;

    section.classList.toggle('is-collapsed', !expanded);
    toggle.setAttribute('aria-expanded', expanded ? 'true' : 'false');
}

toggle?.addEventListener('click', () => {
    const expanded = toggle.getAttribute('aria-expanded') !== 'false';
    setExpanded(!expanded);
});
