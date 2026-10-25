/* Bottom-edge log resizing. Pointer capture also keeps touch/pen drags local. */
(() => {
    'use strict';
    const area = document.querySelector('.bottom-area');
    const panel = area?.querySelector('.log-section');
    const handle = document.getElementById('log-resize-handle');
    if (!area || !panel || !handle) return;

    const storageKey = 'ard.logPanelHeight';
    let preferredHeight = null;
    let drag = null;
    try {
        const saved = localStorage.getItem(storageKey);
        const value = Number(saved);
        if (saved !== null && Number.isFinite(value) && value > 0) preferredHeight = value;
    } catch (_) { /* Resizing remains available when browser storage is blocked. */ }

    function bounds() {
        return { min: 130, max: Math.max(130, Math.min(800, Math.floor(window.innerHeight * 0.75))) };
    }
    function render() {
        const { min, max } = bounds();
        if (preferredHeight !== null) {
            area.style.setProperty('--log-panel-height', `${Math.max(min, Math.min(max, preferredHeight))}px`);
        } else {
            area.style.removeProperty('--log-panel-height');
        }
        handle.setAttribute('aria-valuemin', String(min));
        handle.setAttribute('aria-valuemax', String(max));
        const height = preferredHeight === null ? panel.getBoundingClientRect().height : Math.max(min, Math.min(max, preferredHeight));
        handle.setAttribute('aria-valuenow', String(Math.round(height)));
    }
    function setHeight(height) {
        const { min, max } = bounds();
        preferredHeight = Math.max(min, Math.min(max, Math.round(height)));
        render();
    }
    function save() {
        try {
            if (preferredHeight === null) localStorage.removeItem(storageKey);
            else localStorage.setItem(storageKey, String(preferredHeight));
        } catch (_) { /* Private browsing can disable storage. */ }
    }
    function endDrag(event) {
        if (!drag || (event?.pointerId !== undefined && event.pointerId !== drag.id)) return;
        const id = drag.id;
        drag = null;
        handle.classList.remove('is-resizing');
        document.body.classList.remove('is-resizing-log');
        if (handle.hasPointerCapture(id)) handle.releasePointerCapture(id);
        save();
    }
    handle.addEventListener('pointerdown', event => {
        if (drag || !event.isPrimary || event.button !== 0) return;
        event.preventDefault();
        handle.focus({ preventScroll: true });
        drag = { id: event.pointerId, y: event.clientY, height: panel.getBoundingClientRect().height };
        handle.setPointerCapture(event.pointerId);
        handle.classList.add('is-resizing');
        document.body.classList.add('is-resizing-log');
    });
    handle.addEventListener('pointermove', event => {
        if (!drag || event.pointerId !== drag.id) return;
        setHeight(drag.height + event.clientY - drag.y);
    });
    for (const event of ['pointerup', 'pointercancel', 'lostpointercapture']) handle.addEventListener(event, endDrag);
    window.addEventListener('blur', () => endDrag());
    handle.addEventListener('dblclick', () => {
        preferredHeight = null;
        render();
        save();
    });
    handle.addEventListener('keydown', event => {
        if (!['ArrowUp', 'ArrowDown', 'Home', 'End'].includes(event.key)) return;
        event.preventDefault();
        const { min, max } = bounds();
        const height = panel.getBoundingClientRect().height;
        const step = event.shiftKey ? 50 : 20;
        setHeight(event.key === 'Home' ? min : event.key === 'End' ? max : height + (event.key === 'ArrowDown' ? step : -step));
        save();
    });
    window.addEventListener('resize', () => { endDrag(); render(); });
    render();
})();
