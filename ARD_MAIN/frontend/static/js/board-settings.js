/* Server-owned board selection, shared by the settings page and live dashboard. */
(() => {
    'use strict';
    let settings = window.ARD_HARDWARE || {robot3axis_enabled: true, control_terminal: 'legacy', voice_device: 'controlmcu', jc1060_ip: ''};
    const $ = id => document.getElementById(id);
    const isJC = () => settings.control_terminal === 'jc1060';
    function isTabEnabled(tab) {
        return tab !== 'robot3axismcu-tab' || settings.robot3axis_enabled;
    }
    function apply(next) {
        if (next) settings = {...next};
        window.ARD_HARDWARE = settings;
        if (!settings.robot3axis_enabled) window.onRobotBoardDisabled?.();
        document.querySelectorAll('[data-board="robot3axismcu"]').forEach(el => el.classList.toggle('board-hidden', !settings.robot3axis_enabled));
        document.querySelectorAll('[data-legacy-only]').forEach(el => el.classList.toggle('board-hidden', isJC()));
        document.querySelectorAll('[data-jc-only]').forEach(el => el.classList.toggle('board-hidden', !isJC()));
        document.querySelectorAll('[data-jc-voice-note]').forEach(el => el.classList.toggle('board-hidden', settings.voice_device !== 'jc1060'));
        const en = localStorage.getItem('ard_lang') === 'en';
        const names = {
            hmi: isJC() ? 'JC1060P470C' : (en ? 'HMI UI Board' : 'HMI UI 보드'),
            drive: en ? 'Drive Board' : '모터 구동 보드',
            control: settings.voice_device === 'jc1060' ? (en ? 'JC1060 Voice' : 'JC1060 음성') : isJC() ? (en ? 'ControlMCU Voice' : '음성 보드 (ControlMCU)') : (en ? 'Control Main Board' : '제어 메인 보드'),
            hmi_settings: isJC() ? 'JC1060 통합 단말' : (en ? 'HMI Receiver Settings' : 'HMI 수신 보드 설정'),
            hmi_ip: isJC() ? 'JC1060 IP' : 'HMI MCU IP'
        };
        document.querySelectorAll('[data-board-label]').forEach(el => {
            const name = names[el.dataset.boardLabel];
            if (name) el.textContent = name;
        });
        ['hmi_mcu', 'drivemcu', 'controlmcu'].forEach(key => {
            const chip = $(`chip-${key}`);
            const label = chip?.querySelector('.chip-name');
            if (label) label.textContent = names[{hmi_mcu:'hmi', drivemcu:'drive', controlmcu:'control'}[key]];
        });
        if ($('chip-robot3axismcu')) $('chip-robot3axismcu').classList.toggle('board-hidden', !settings.robot3axis_enabled);
        if ($('jc1060-ip')) $('jc1060-ip').textContent = settings.jc1060_ip || '자동 감지 대기';
        if (typeof window.switchTab === 'function' && window.activeDashboardTab) {
            window.switchTab(isTabEnabled(window.activeDashboardTab) ? window.activeDashboardTab : 'sorting-tab');
        }
    }
    function updateStatus(status) {
        if (status.hardware && JSON.stringify(status.hardware) !== JSON.stringify(settings)) apply(status.hardware);
        const jc = status.jc1060;
        if (jc) {
            if ($('jc1060-ip')) $('jc1060-ip').textContent = jc.ip || '자동 감지 대기';
            if ($('jc1060-connection')) $('jc1060-connection').textContent = jc.state === 'Online' ? '온라인' : '오프라인';
        }
    }
    window.BoardUI = {apply, updateStatus, isTabEnabled, isJC, getSettings: () => ({...settings})};
    document.addEventListener('DOMContentLoaded', () => {
        apply(settings);
        const requestedTab = window.location.hash.slice(1);
        const requestedButton = [...document.querySelectorAll('.settings-tab-btn')].find(button => button.dataset.target === requestedTab);
        if (requestedButton) requestedButton.click();
        const form = $('board-config-form');
        if (!form) return;
        const payload = () => ({robot3axis_enabled: $('board-robot-enabled').checked,
            control_terminal: $('board-terminal').value, voice_device: $('board-voice').value,
            jc1060_ip: $('board-jc-ip').value.trim()});
        const describe = () => {
            const next = payload();
            $('board-selection-summary').textContent = `${next.robot3axis_enabled ? '3축 로봇 사용' : '3축 로봇 숨김'} · ${next.control_terminal === 'jc1060' ? 'JC1060 단말' : '구형 제어 단말'} · ${next.voice_device === 'jc1060' ? 'JC1060 내장 마이크·스피커' : 'ControlMCU 음성'}`;
        };
        form.addEventListener('change', event => {
            if (event.target.id === 'board-terminal') $('board-voice').value = event.target.value === 'jc1060' ? 'jc1060' : 'controlmcu';
            describe();
        });
        describe();
        form.addEventListener('submit', async event => {
            event.preventDefault();
            const message = $('board-save-message');
            $('board-save-btn').disabled = true;
            message.textContent = '저장 중…';
            try {
                const response = await fetch('/api/hardware/config', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(payload())});
                const result = await response.json();
                if (!response.ok) throw new Error(result.message || '보드 구성을 저장하지 못했습니다.');
                apply(result.hardware);
                message.style.color = 'var(--success)';
                message.textContent = '저장했습니다. 대시보드에 적용되었습니다.';
            } catch (error) {
                message.style.color = 'var(--danger)';
                message.textContent = error.message;
            } finally { $('board-save-btn').disabled = false; }
        });
    });
})();
