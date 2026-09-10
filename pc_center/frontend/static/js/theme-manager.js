/**
 * ARD Unified Theme Manager
 * Supports custom color selection (Primary, Secondary, Background, Card Surface)
 * and presets with localStorage persistence and cross-tab synchronization.
 */

(function () {
    'use strict';

    const STORAGE_KEY = 'ard_custom_theme';

    const PRESETS = {
        autodesk: {
            '--primary': '#0696D7',      // Autodesk Blue
            '--secondary': '#FFFF00',    // Autodesk Yellow
            '--bg-color': '#1E1E1E',     // Background Canvas
            '--card-bg': '#2B2B2B',      // Panel Surface
            '--text-main': '#FFFFFF',    // Text Primary
            '--text-dim': '#A0A0A0',     // Text Secondary
            '--accent': '#FFFF00',       // Highlight Yellow
            '--success': '#0696D7',
            '--danger': '#E04040'
        },
        monochrome: {
            '--primary': '#ffffff',
            '--secondary': '#94a3b8',
            '--bg-color': '#000000',
            '--card-bg': '#111111',
            '--text-main': '#ffffff',
            '--text-dim': '#888888',
            '--accent': '#cccccc',
            '--success': '#ffffff',
            '--danger': '#ff4444'
        },
        ocean: {
            '--primary': '#38bdf8',
            '--secondary': '#818cf8',
            '--bg-color': '#0f172a',
            '--card-bg': '#1e293b',
            '--text-main': '#f8fafc',
            '--text-dim': '#94a3b8',
            '--accent': '#fb7185',
            '--success': '#4ade80',
            '--danger': '#f87171'
        },
        cyberpunk: {
            '--primary': '#f43f5e',
            '--secondary': '#a855f7',
            '--bg-color': '#090514',
            '--card-bg': '#170f2c',
            '--text-main': '#fdf4ff',
            '--text-dim': '#c084fc',
            '--accent': '#06b6d4',
            '--success': '#10b981',
            '--danger': '#ef4444'
        },
        emerald: {
            '--primary': '#10b981',
            '--secondary': '#14b8a6',
            '--bg-color': '#062016',
            '--card-bg': '#0b3324',
            '--text-main': '#ecfdf5',
            '--text-dim': '#6ee7b7',
            '--accent': '#f59e0b',
            '--success': '#34d399',
            '--danger': '#f87171'
        }
    };

    function loadTheme() {
        try {
            const saved = localStorage.getItem(STORAGE_KEY);
            if (saved) {
                const themeData = JSON.parse(saved);
                applyTheme(themeData);
                return themeData;
            }
        } catch (e) {
            console.error('Theme load error:', e);
        }
        applyTheme(PRESETS.autodesk);
        return PRESETS.autodesk;
    }

    function applyTheme(themeObj) {
        if (!themeObj) return;
        const root = document.documentElement;
        for (const [key, val] of Object.entries(themeObj)) {
            root.style.setProperty(key, val);
        }

        // Toggle theme-autodesk class based on background & card colors or primary
        if (themeObj['--bg-color'] === '#1E1E1E' || themeObj['--primary'] === '#0696D7') {
            document.body?.classList.add('theme-autodesk');
        } else {
            document.body?.classList.remove('theme-autodesk');
        }
    }

    function saveTheme(themeObj) {
        try {
            localStorage.setItem(STORAGE_KEY, JSON.stringify(themeObj));
            applyTheme(themeObj);
        } catch (e) {
            console.error('Theme save error:', e);
        }
    }

    function applyPreset(presetName) {
        if (PRESETS[presetName]) {
            saveTheme(PRESETS[presetName]);
            return PRESETS[presetName];
        }
        return null;
    }

    function resetTheme() {
        applyPreset('autodesk');
    }

    // Listen for storage events across tabs
    window.addEventListener('storage', (e) => {
        if (e.key === STORAGE_KEY && e.newValue) {
            try {
                const updated = JSON.parse(e.newValue);
                applyTheme(updated);
            } catch (err) {
                console.error(err);
            }
        }
    });

    // Immediate initial apply on load
    loadTheme();

    window.ThemeManager = {
        PRESETS,
        loadTheme,
        applyTheme,
        saveTheme,
        applyPreset,
        resetTheme
    };
})();
