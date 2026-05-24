// --- ARD Control Center: Camera MCU Tab Module ---
document.addEventListener('DOMContentLoaded', () => {
    // Auto Capture Interval Sync
    const intervalSlider = document.getElementById('capture-interval');
    const intervalNum = document.getElementById('capture-interval-num');
    const intervalVal = document.getElementById('interval-val');
    
    const updateInterval = (val) => {
        if (intervalSlider) intervalSlider.value = val;
        if (intervalNum) intervalNum.value = val;
        if (intervalVal) intervalVal.textContent = val;
    };
    intervalSlider?.addEventListener('input', (e) => updateInterval(e.target.value));
    intervalNum?.addEventListener('input', (e) => updateInterval(e.target.value));

    // JPEG Quality Sync
    const qualitySlider = document.getElementById('jpeg_quality');
    const qualityNum = document.getElementById('jpeg_quality-num');
    const qualityVal = document.getElementById('quality-val');

    const updateQuality = (val) => {
        if (qualitySlider) qualitySlider.value = val;
        if (qualityNum) qualityNum.value = val;
        if (qualityVal) qualityVal.textContent = val;
    };
    qualitySlider?.addEventListener('input', (e) => updateQuality(e.target.value));
    qualityNum?.addEventListener('input', (e) => updateQuality(e.target.value));

    // --- 카메라 모델별 해상도 제한 로직 ---
    const camSelect = document.getElementById('camera_model');
    const resSelect = document.getElementById('resolution');
    const warnMsg = document.getElementById('cam-warning-msg');
    
    if (camSelect && resSelect) {
        const updateCamConstraints = () => {
            const cam = camSelect.value;
            const resOptions = resSelect.options;
            
            if (cam === 'OV2640') {
                for (let i = 0; i < resOptions.length; i++) {
                    if (resOptions[i].value === 'QSXGA') {
                        resOptions[i].disabled = true;
                    } else {
                        resOptions[i].disabled = false;
                    }
                }
                if (resSelect.value === 'QSXGA') {
                    resSelect.value = 'UXGA'; // 강제로 낮춤
                }
                if (warnMsg) {
                    warnMsg.textContent = 'OV2640 모듈은 최대 UXGA까지만 지원합니다.';
                    warnMsg.style.color = '#ef4444'; // 빨간색 경고
                }
            } else if (cam === 'OV3660') {
                for (let i = 0; i < resOptions.length; i++) {
                    resOptions[i].disabled = false;
                }
                if (warnMsg) {
                    warnMsg.textContent = 'OV3660은 안정성을 위해 가급적 UXGA 이하 사용을 권장합니다.';
                    warnMsg.style.color = '#facc15'; // 노란색 주의
                }
            } else {
                for (let i = 0; i < resOptions.length; i++) {
                    resOptions[i].disabled = false;
                }
                if (warnMsg) {
                    warnMsg.textContent = 'OV5640은 모든 해상도를 안정적으로 지원합니다.';
                    warnMsg.style.color = '#4ade80'; // 녹색 정상
                }
            }
        };

        camSelect.addEventListener('change', updateCamConstraints);
        // 페이지 로드 시 초기 상태 반영
        updateCamConstraints();
    }

    // --- 자동 촬영 로직 ---
    let autoCaptureTimer = null;
    const startAutoBtn = document.getElementById('start-auto-btn');
    const stopAutoBtn = document.getElementById('stop-auto-btn');

    async function triggerCapture() {
        try {
            await fetch('/api/transmitter/command', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ command: 'capture' })
            });
            if (window.addLog) window.addLog('System', 'Auto capture command sent.');
        } catch (e) {
            if (window.addLog) window.addLog('System', 'Auto capture failed: ' + e.message);
        }
    }

    startAutoBtn?.addEventListener('click', () => {
        if (!intervalSlider) return;
        const interval = parseInt(intervalSlider.value) * 1000;
        autoCaptureTimer = setInterval(triggerCapture, interval);
        
        startAutoBtn.disabled = true;
        if (stopAutoBtn) stopAutoBtn.disabled = false;
        intervalSlider.disabled = true;
        if (window.addLog) window.addLog('System', `Auto capture started (Interval: ${intervalSlider.value}s)`);
    });

    stopAutoBtn?.addEventListener('click', () => {
        if (autoCaptureTimer) {
            clearInterval(autoCaptureTimer);
            autoCaptureTimer = null;
        }
        
        if (startAutoBtn) startAutoBtn.disabled = false;
        stopAutoBtn.disabled = true;
        if (intervalSlider) intervalSlider.disabled = false;
        if (window.addLog) window.addLog('System', 'Auto capture stopped.');
    });

    // --- 수동 촬영 캡처 ---
    const handleCapture = async (btnId) => {
        const btn = document.getElementById(btnId);
        if (!btn) return;
        const currentLang = localStorage.getItem('ard_lang') || 'ko';
        btn.textContent = currentLang === 'ko' ? '촬영 요청 중...' : 'Requesting Capture...';
        btn.disabled = true;

        try {
            const res = await fetch('/api/imagesendermcu/command', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ command: 'capture' })
            });
            if (res.ok) {
                btn.textContent = currentLang === 'ko' ? '촬영 중 (5초 대기)...' : 'Capturing (Wait 5s)...';
                setTimeout(() => {
                    if (window.updateLanguage) window.updateLanguage(localStorage.getItem('ard_lang') || 'ko');
                    btn.disabled = false;
                }, 5000);
            }
        } catch (e) {
            if (window.addLog) window.addLog('System', 'Command failed: ' + e.message);
            btn.textContent = 'Error!';
            btn.disabled = false;
        }
    };

    document.getElementById('capture-btn')?.addEventListener('click', () => handleCapture('capture-btn'));
    document.getElementById('capture-btn-trans')?.addEventListener('click', () => handleCapture('capture-btn-trans'));

    document.getElementById('sync-t-ip')?.addEventListener('click', () => {
        const disp = document.getElementById('imagesendermcu-ip-display');
        const detectedIp = disp?.dataset.ip;
        const currentLang = localStorage.getItem('ard_lang') || 'ko';
        if (detectedIp && detectedIp !== 'Unknown') {
            const transmitterIpInput = document.getElementById('transmitter_ip');
            if (transmitterIpInput) transmitterIpInput.value = detectedIp;
            if (window.addLog) window.addLog('System', `ImageSenderMCU IP auto-filled: ${detectedIp}`);
        } else {
            alert(currentLang === 'ko' ? '감지된 IP가 없습니다.' : 'No detected IP available.');
        }
    });
});
