/* Navigation changes views only; it never sends device commands. */
(() => {
    const buttons = [...document.querySelectorAll('[data-mobile-tab]')];
    const syncNavigation = () => {
        const active = document.querySelector('.status-box.tab-active-box');
        const selected = active?.getAttribute('onclick') || '';
        buttons.forEach(button => {
            if (selected.includes(button.dataset.mobileTab)) button.setAttribute('aria-current', 'page');
            else button.removeAttribute('aria-current');
        });
    };
    buttons.forEach(button => button.addEventListener('click', () => {
        if (typeof window.switchTab !== 'function') return;
        window.switchTab(button.dataset.mobileTab);
        syncNavigation();
        document.querySelector('.dashboard-page main')?.scrollIntoView({ block: 'start' });
    }));
    const strip = document.querySelector('.device-status-strip');
    if (strip) new MutationObserver(syncNavigation).observe(strip, { subtree: true, attributes: true, attributeFilter: ['class'] });
    syncNavigation();
})();
