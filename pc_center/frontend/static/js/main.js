document.addEventListener('DOMContentLoaded', () => {
    // --- Dynamic Loading Pulse Style Injection ---
    const style = document.createElement('style');
    style.innerHTML = `
        .pulse-dot {
            width: 8px;
            height: 8px;
            background: var(--primary, #a855f7);
            border-radius: 50%;
            display: inline-block;
            animation: pulse-dot-anim 1.2s infinite ease-in-out;
        }
        @keyframes pulse-dot-anim {
            0%, 100% { transform: scale(0.6); opacity: 0.4; }
            50% { transform: scale(1.2); opacity: 1; }
        }
    `;
    document.head.appendChild(style);

    // --- Startup Splash Handling ---
    async function handleStartup() {
        const overlay = document.getElementById('startup-overlay');
        const progress = document.getElementById('startup-progress');
        const stepText = document.getElementById('startup-step-text');
        
        if (!overlay) return;

        const checkStatus = async () => {
            try {
                const res = await fetch('/api/startup_status');
                const data = await res.json();
                
                if (progress) progress.style.width = data.progress + '%';
                if (stepText) stepText.textContent = data.text;
                
                if (data.id === 'ready') {
                    setTimeout(() => {
                        overlay.style.opacity = '0';
                        overlay.style.visibility = 'hidden';
                        setTimeout(() => overlay.remove(), 1000);
                    }, 800);
                    return true;
                }
            } catch (e) {
                // Server might not be ready yet
            }
            return false;
        };

        const interval = setInterval(async () => {
            const done = await checkStatus();
            if (done) clearInterval(interval);
        }, 500);
    }
    handleStartup();


    let currentLang = localStorage.getItem('ard_lang') || 'ko';

    function updateLanguage(lang) {
        currentLang = lang;
        localStorage.setItem('ard_lang', lang);
        
        document.querySelectorAll('[data-i18n]').forEach(el => {
            const key = el.getAttribute('data-i18n');
            if (translations[lang] && translations[lang][key]) {
                if (key === 'offline' || key === 'online') {
                    const isOnline = el.classList.contains('status-online') || el.closest('.status-box')?.classList.contains('status-box-online');
                    el.textContent = isOnline ? translations[lang]['online'] : translations[lang]['offline'];
                } else {
                    el.textContent = translations[lang][key];
                }
            }
        });

        document.querySelectorAll('[data-i18n-placeholder]').forEach(el => {
            const key = el.getAttribute('data-i18n-placeholder');
            if (translations[lang] && translations[lang][key]) {
                el.setAttribute('placeholder', translations[lang][key]);
            }
        });
        
        const langBtn = document.getElementById('lang-toggle');
        if (langBtn) {
            langBtn.innerHTML = `<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><line x1="2" y1="12" x2="22" y2="12"></line><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"></path></svg><span>${lang === 'ko' ? 'ENGLISH UI' : '한국어 UI'}</span>`;
        }

        if (typeof window.autoFitText === 'function') {
            setTimeout(window.autoFitText, 20);
        }
    }


    // Initialize Language
    updateLanguage(currentLang);

    const langToggleBtn = document.getElementById('lang-toggle');
    if (langToggleBtn) {
        langToggleBtn.addEventListener('click', () => {
            updateLanguage(currentLang === 'ko' ? 'en' : 'ko');
        });
    }

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

    // HMI Quality Sync
    const hmiQualitySlider = document.getElementById('hmi_image_quality');
    const hmiQualityVal = document.getElementById('hmi-quality-val');
    hmiQualitySlider?.addEventListener('input', (e) => {
        if (hmiQualityVal) hmiQualityVal.textContent = e.target.value;
    });

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

    // Server Shutdown Logic (Hold for 2 seconds to confirm)
    let shutdownTimer = null;
    const shutdownBtn = document.getElementById('server-shutdown-btn');
    
    if (shutdownBtn) {
        const startShutdown = () => {
            if (shutdownBtn.classList.contains('active')) return;
            shutdownBtn.classList.add('active');
            
            shutdownTimer = setTimeout(async () => {
                const showOfflineUI = () => {
                    document.body.innerHTML = `<div style="display:flex;align-items:center;justify-content:center;height:100vh;background:#020617;color:#f8fafc;font-family:sans-serif;text-align:center;">
                        <div>
                            <div style="margin-bottom: 20px; display: flex; justify-content: center;">
                                <svg width="64" height="64" viewBox="0 0 24 24" fill="none" stroke="#f87171" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M18.36 6.64a9 9 0 1 1-12.73 0"></path><line x1="12" y1="2" x2="12" y2="12"></line></svg>
                            </div>
                            <h1 style="color:#f87171; font-size: 3rem; margin-bottom: 20px; letter-spacing: 5px;">SYSTEM OFFLINE</h1>
                            <p style="font-size: 1.2rem; color: #94a3b8;">The ARD Control Server has been shut down successfully.</p>
                            <p style="margin-top: 20px; opacity: 0.6;">You can now safely close this browser window.</p>
                        </div>
                    </div>`;
                };

                try {
                    // 서버에 종료 신호를 보내고 응답을 기다리지 않음 (서버가 즉시 죽을 수 있으므로)
                    fetch('/api/shutdown', { method: 'POST' }).catch(() => {});
                    // 잠시 후 UI 전환
                    setTimeout(showOfflineUI, 300);
                } catch (e) {
                    showOfflineUI();
                }
            }, 1500); // 1.5초 동안 유지해야 종료됨 (실수 방지)
        };

        const cancelShutdown = () => {
            if (shutdownTimer) {
                clearTimeout(shutdownTimer);
                shutdownTimer = null;
            }
            shutdownBtn.classList.remove('active');
        };

        // Mouse Events
        shutdownBtn.addEventListener('mousedown', startShutdown);
        shutdownBtn.addEventListener('mouseup', cancelShutdown);
        shutdownBtn.addEventListener('mouseleave', cancelShutdown);

        // Touch Events
        shutdownBtn.addEventListener('touchstart', (e) => {
            e.preventDefault();
            startShutdown();
        });
        shutdownBtn.addEventListener('touchend', cancelShutdown);
        shutdownBtn.addEventListener('touchcancel', cancelShutdown);
    }

    // --- 설정 저장 로직 ---
    document.getElementById('config-form').addEventListener('submit', async (e) => {
        e.preventDefault();
        const config = {
            api_key: document.getElementById('api_key')?.value || "",
            model_name: document.getElementById('model_name')?.value || "gemini-2.5-flash",
            hmi_mcu_ip: document.getElementById('hmi_mcu_ip')?.value || "",
            transmitter_ip: document.getElementById('transmitter_ip')?.value || "",
            camera_model: document.getElementById('camera_model')?.value || "OV3660",
            resolution: document.getElementById('resolution')?.value || "UXGA",
            jpeg_quality: document.getElementById('jpeg_quality')?.value || "10",
            hmi_image_quality: document.getElementById('hmi_image_quality')?.value || "40",
            soundmcu_api_key: document.getElementById('soundmcu_api_key')?.value || "",
            transmission_mode: document.getElementById('transmission-mode')?.value || "UDP"
        };

        try {
            const res = await fetch('/api/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(config)
            });

            if (res.ok) {
                showSuccessCheck();
                addLog('System', 'Configuration updated successfully.');
            } else {
                addLog('System', 'Failed to save configuration: ' + res.statusText);
            }
        } catch (e) {
            addLog('System', 'Error saving config: ' + e.message);
        }
    });

    // (Consolidated above)

    // --- 자동 촬영 로직 ---
    let autoCaptureTimer = null;
    const startAutoBtn = document.getElementById('start-auto-btn');
    const stopAutoBtn = document.getElementById('stop-auto-btn');

    // intervalSlider and intervalVal are consolidated in the top sync block

    async function triggerCapture() {
        try {
            await fetch('/api/transmitter/command', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ command: 'capture' })
            });
            addLog('System', 'Auto capture command sent.');
        } catch (e) {
            addLog('System', 'Auto capture failed: ' + e.message);
        }
    }

    startAutoBtn.addEventListener('click', () => {
        const interval = parseInt(intervalSlider.value) * 1000;
        autoCaptureTimer = setInterval(triggerCapture, interval);
        
        startAutoBtn.disabled = true;
        stopAutoBtn.disabled = false;
        intervalSlider.disabled = true;
        addLog('System', `Auto capture started (Interval: ${intervalSlider.value}s)`);
    });

    stopAutoBtn.addEventListener('click', () => {
        clearInterval(autoCaptureTimer);
        autoCaptureTimer = null;
        
        startAutoBtn.disabled = false;
        stopAutoBtn.disabled = true;
        intervalSlider.disabled = false;
        addLog('System', 'Auto capture stopped.');
    });

    // --- 로그 시스템 ---
    function addLog(source, message, timestamp = null) {
        const container = document.getElementById('log-container');
        const time = timestamp || new Date().toLocaleTimeString();
        const entry = document.createElement('div');
        entry.className = 'log-entry';
        
        let sourceColor = '#4ade80'; 
        let textColor = '#4ade80';
        if (source === 'Transmitter') sourceColor = '#38bdf8'; 
        if (source === 'Receiver') sourceColor = '#fb7185';    
        if (source === 'System') sourceColor = '#94a3b8';      
        if (source === 'ControlMCU') {
            sourceColor = '#fb923c'; // Vibrant Orange
            textColor = '#fed7aa';   // Soft Orange Text
        }
        if (source === 'Gemini' || source === 'Network') {
            sourceColor = '#f87171';
            textColor = '#fca5a5';
        }
        
        entry.style.color = textColor;
        entry.innerHTML = `<span style="color: #666;">[${time}]</span> <span style="color: ${sourceColor}; font-weight: bold;">[${source}]</span> ${message}`;
        container.appendChild(entry);
        container.scrollTop = container.scrollHeight;
    }

    // 전역으로 사용할 수 있게 노출
    window.addLog = addLog;

    // 로그 삭제 버튼
    document.getElementById('clear-log-btn').addEventListener('click', () => {
        document.getElementById('log-container').innerHTML = '';
        addLog('System', 'Logs cleared.');
    });

    // 수동 새로고침 버튼
    const refreshBtn = document.getElementById('manual-refresh-btn');
    if (refreshBtn) {
        refreshBtn.addEventListener('click', () => {
            // 새로고침 효과를 주기 위해 아이콘 스핀
            const svg = refreshBtn.querySelector('svg');
            if (svg) svg.classList.add('sync-spin');
            refreshBtn.disabled = true;
            addLog('System', '수동 동기화 요청 중...');
            setTimeout(() => {
                window.location.reload();
            }, 300);
        });
    }

    // --- 탭 전환 로직 ---
    const tabContents = document.querySelectorAll('.tab-content');

    // 전역 탭 전환 함수: HTML onclick에서도 직접 호출 가능
    window.switchTab = function(tabId) {
        tabContents.forEach(c => c.classList.remove('active'));

        // 상태박스 active 표시 (상단 박스 하이라이트)
        document.querySelectorAll('.status-box').forEach(box => {
            box.classList.remove('tab-active-box');
        });
        const tabToBoxMap = {
            'imagesendermcu-tab': 'imagesendermcu-box',
            'hmi_mcu-tab': 'hmi_mcu-box',
            'drivemcu-tab': 'drivemcu-box',
            'controlmcu-tab': 'controlmcu-box',
            'robot3axismcu-tab': 'robot3axismcu-box'
        };
        const activeBox = document.getElementById(tabToBoxMap[tabId]);
        if (activeBox) activeBox.classList.add('tab-active-box');

        // --- Toggle Main Left-Col Views based on Tab ---
        const imageContainer = document.getElementById('image-container');
        const voiceMediaContainer = document.getElementById('voice-media-container');
        const simView = document.getElementById('simulation-view');
        const angleCard = document.getElementById('angle-monitor-card');
        const r3MainView = document.getElementById('robot3axis-main-view');
        const mcView = document.getElementById('master-control-view');

        // Reset all to none
        if (imageContainer) imageContainer.style.display = 'none';
        if (voiceMediaContainer) voiceMediaContainer.style.display = 'none';
        if (simView) { simView.style.display = 'none'; stopSim(); }
        if (angleCard) angleCard.style.display = 'none';
        if (r3MainView) r3MainView.style.display = 'none';
        if (mcView) mcView.style.display = 'none';

        if (tabId === 'imagesendermcu-tab') {
            if (imageContainer) imageContainer.style.display = 'block';
        } else if (tabId === 'drivemcu-tab') {
            if (simView) { simView.style.display = 'flex'; startSim(); }
            if (angleCard) angleCard.style.display = 'block';
        } else if (tabId === 'controlmcu-tab') {
            if (voiceMediaContainer) voiceMediaContainer.style.display = 'flex';
        } else if (tabId === 'hmi_mcu-tab') {
            // leave empty
        } else if (tabId === 'robot3axismcu-tab') {
            if (r3MainView) r3MainView.style.display = 'flex';
            if (window.startRobot3AxisSim) window.startRobot3AxisSim();
        }


        const targetContent = document.getElementById(tabId);
        if (targetContent) targetContent.classList.add('active');

        // Update View sizes (Three.js etc)
        if (typeof onWindowResize === 'function') onWindowResize();

        if (tabId === 'controlmcu-tab') {
            updateSoundMCUUI();
            loadPersonaFile();
        }

        if (typeof window.autoFitText === 'function') {
            setTimeout(window.autoFitText, 30);
        }
    };

    // 기존 data-tab 버튼 지원 (하위 호환)
    document.querySelectorAll('.tab-btn[data-tab]').forEach(btn => {
        btn.addEventListener('click', () => window.switchTab(btn.dataset.tab));
    });


    // [Extracted Module: Audio & Voice Controller loaded from audio-voice-controller.js]

    // --- UI 업데이트 로직 ---
    function updateUI(data) {
        const imgElement = document.getElementById('latest-image');
        const overlay = document.getElementById('coordinate-overlay');
        const noImage = document.getElementById('no-image');

        // [이미지 표시] image_b64가 있을 때만 이미지 갱신
        if (data.image_b64) {
            imgElement.src = `data:image/jpeg;base64,${data.image_b64}`;

            // HUD 미니 이미지 동기화
            const hudMiniImg = document.getElementById('hud-mini-image');
            const hudPlaceholder = document.getElementById('hud-mini-placeholder');
            if (hudMiniImg) {
                hudMiniImg.src = imgElement.src;
                hudMiniImg.style.display = 'block';
                if (hudPlaceholder) hudPlaceholder.style.display = 'none';
            }

            // 이미지센더 탭 내의 이미지 및 노이미지 문구 전환
            imgElement.style.display = 'block';
            if (noImage) noImage.style.display = 'none';
        }

        // [분석 결과 표시] image_b64 여부와 무관하게 항상 렌더링
        if (data.analysis && data.analysis.x !== undefined && data.analysis.object) {
            // "Analyzing..." 상태는 패널 갱신 건너뜀
            if (data.analysis.object === 'Analyzing...') return;

            if (overlay) {
                const px = data.analysis.x * 100;
                const py = data.analysis.y * 100;
                overlay.style.display = 'block';
                overlay.style.left = px + '%';
                overlay.style.top  = py + '%';

                // Update Guide Lines
                const lx = document.getElementById('target-line-x');
                const ly = document.getElementById('target-line-y');
                if (lx) { lx.style.display = 'block'; lx.style.top = py + '%'; }
                if (ly) { ly.style.display = 'block'; ly.style.left = px + '%'; }
            }

            const objName = document.getElementById('object-name');
            const objType = document.getElementById('object-type');
            const objCoords = document.getElementById('object-coords');
            const processTime = document.getElementById('process-time');
            if (objName)   objName.textContent   = data.analysis.object   || 'Unknown';
            if (objType)   objType.textContent   = data.analysis.type     || '-';
            if (objCoords) objCoords.textContent = `X: ${data.analysis.x.toFixed(2)}, Y: ${data.analysis.y.toFixed(2)}`;
            if (processTime) processTime.textContent = data.metadata ? data.metadata.timestamp : new Date().toLocaleTimeString();

            // 재질 뱃지
            const matEl = document.getElementById('object-material');
            if (matEl) {
                const cat = data.analysis.category || 'Others';
                const matNames = {
                    Plastic: currentLang === 'ko' ? '플라스틱' : 'Plastic',
                    Metal:   currentLang === 'ko' ? '금속'     : 'Metal',
                    Glass:   currentLang === 'ko' ? '유리'     : 'Glass',
                    Paper:   currentLang === 'ko' ? '종이'     : 'Paper',
                    Others:  currentLang === 'ko' ? '기타'     : 'Others'
                };
                matEl.className = `mat-badge mat-${cat.toLowerCase()}`;
                matEl.textContent = matNames[cat] || cat;
            }

            // HUD 분석 결과 업데이트
            const hudObj  = document.getElementById('hud-obj-name');
            const hudType = document.getElementById('hud-obj-type');
            const hudMat  = document.getElementById('hud-obj-material');
            if (hudObj)  hudObj.textContent  = data.analysis.object   || '-';
            if (hudType) hudType.textContent = data.analysis.type     || '-';
            if (hudMat) {
                const cat = data.analysis.category || 'Others';
                const matNames = {
                    Plastic: currentLang === 'ko' ? '플라스틱' : 'Plastic',
                    Metal:   currentLang === 'ko' ? '금속'     : 'Metal',
                    Glass:   currentLang === 'ko' ? '유리'     : 'Glass',
                    Paper:   currentLang === 'ko' ? '종이'     : 'Paper',
                    Others:  currentLang === 'ko' ? '기타'     : 'Others'
                };
                hudMat.className  = `mat-badge mat-${cat.toLowerCase()}`;
                hudMat.textContent = matNames[cat] || cat;
            }
        }

        // 메타데이터 표시
        if (data.metadata) {
            const sensorInfo   = document.getElementById('sensor-info');
            const resInfo      = document.getElementById('res-info');
            const sizeInfo     = document.getElementById('size-info');
            const durationInfo = document.getElementById('duration-info');
            if (sensorInfo)   sensorInfo.textContent   = data.metadata.sensor;
            if (resInfo)      resInfo.textContent      = data.metadata.resolution || '-';
            if (sizeInfo)     sizeInfo.textContent     = data.metadata.size_kb + ' KB';
            if (durationInfo) durationInfo.textContent = data.metadata.duration_ms + ' ms';
        }
    }

    function updateDeviceUI(device, data) {
        if (!data) return;
        const isOnline = data.state === "Online";
        
        const statusEl = document.getElementById(`${device}-status`);
        const textEl = document.getElementById(`${device}-status-text`);
        const boxEl = document.getElementById(`${device}-box`);
        
        if (statusEl) statusEl.className = `indicator ${isOnline ? 'status-online' : 'status-offline'}`;
        if (textEl) {
            let onlineText = isOnline ? "Online" : "Offline";
            try {
                if (window.translations && window.translations[currentLang]) {
                    onlineText = isOnline ? window.translations[currentLang]['online'] : window.translations[currentLang]['offline'];
                } else if (typeof translations !== 'undefined' && translations[currentLang]) {
                    onlineText = isOnline ? translations[currentLang]['online'] : translations[currentLang]['offline'];
                }
            } catch(e) {
                console.warn("Translation failed for online/offline text:", e);
            }
            textEl.textContent = onlineText;
            textEl.className = `val ${isOnline ? 'status-online' : 'status-offline'}`;
        }
        
        if (boxEl) {
            if (isOnline) {
                boxEl.classList.add('status-box-online');
                boxEl.classList.remove('status-box-offline');
            } else {
                boxEl.classList.add('status-box-offline');
                boxEl.classList.remove('status-box-online');
            }
        }

        if (device === 'controlmcu') {
            const modeDisp = document.getElementById('controlmcu-mode-display');
            const subDisp = document.getElementById('controlmcu-sub-display');
            if (modeDisp) modeDisp.textContent = isOnline ? data.mode : '-';
            if (subDisp) subDisp.textContent = isOnline ? data.sub_mode : '-';
        }

        const ipDisplay = document.getElementById(`${device}-ip-display`);
        const rssiText = document.getElementById(`${device}-rssi`);
        const tempText = document.getElementById(`${device}-temp`);
        const sIpDisp = document.getElementById('server-ip-display');
        const costInfo = document.getElementById('cost-info');

        if (sIpDisp && data.server_ip) {
            sIpDisp.textContent = data.server_ip;
        }

        if (rssiText) {
            rssiText.textContent = isOnline ? `RSSI: ${data.rssi}dBm` : 'RSSI: -dBm';
        }

        if (tempText) {
            let tempLabel = "Temp";
            try {
                if (window.translations && window.translations[currentLang]) {
                    tempLabel = window.translations[currentLang]['temp_label'];
                } else if (typeof translations !== 'undefined' && translations[currentLang]) {
                    tempLabel = translations[currentLang]['temp_label'];
                }
            } catch(e) {
                console.warn("Translation failed for temp label:", e);
            }
            if (isOnline) {
                tempText.innerHTML = `${tempLabel}: ${data.temp}°C`;
                tempText.style.color = data.temp > 45 ? 'var(--danger)' : 'var(--text-dim)';
            } else {
                tempText.innerHTML = `${tempLabel}: --°C`;
                tempText.style.color = 'var(--text-dim)';
            }
        }

        if (ipDisplay) {
            if (isOnline && data.ip) {
                ipDisplay.style.display = 'inline-block';
                ipDisplay.textContent = `IP: ${data.ip}`;
                ipDisplay.dataset.ip = data.ip;
            } else {
                ipDisplay.style.display = 'none';
                ipDisplay.textContent = '';
                ipDisplay.dataset.ip = '';
            }
        }

        function updateVisuals(prefix, rssi, temp, isOnline) {
            const signalContainer = document.getElementById(`${prefix}-signal-visual`);
            const tempFill = document.getElementById(`${prefix}-temp-fill`);
            if (!signalContainer || !tempFill) return;

            const bars = signalContainer.querySelectorAll('.sig-bar');

            if (!isOnline) {
                bars.forEach(b => b.className = 'sig-bar');
                tempFill.style.width = '0%';
                tempFill.className = 'temp-fill';
                return;
            }

            let activeBars = 0;
            let sigClass = 'active';
            if (rssi > -45) activeBars = 4;
            else if (rssi > -65) activeBars = 3;
            else if (rssi > -80) { activeBars = 2; sigClass = 'sig-mid active'; }
            else { activeBars = 1; sigClass = 'sig-weak active'; }

            bars.forEach((b, i) => {
                b.className = i < activeBars ? `sig-bar ${sigClass}` : 'sig-bar';
            });

            const tempPercent = Math.min(Math.max(temp, 0), 100);
            tempFill.style.width = tempPercent + '%';
            
            if (temp > 50) tempFill.className = 'temp-fill temp-hot';
            else if (temp > 35) tempFill.className = 'temp-fill temp-warm';
            else tempFill.className = 'temp-fill';
        }

        updateVisuals(device, data.rssi, data.temp, isOnline);

        if (costInfo && data.total_cost !== undefined) {
            const krw = data.total_cost_krw.toLocaleString();
            const usd = data.total_cost.toFixed(4);
            costInfo.textContent = `${krw}원 ($${usd})`;
        }

        if (device === 'drivemcu') {
            const gpsDisp = document.getElementById('gps-coords-display');
            const mapBtn = document.getElementById('gps-map-btn');
            if (gpsDisp && mapBtn) {
                if (isOnline && data.gps_lat !== undefined && data.gps_lat !== 0.0) {
                    gpsDisp.textContent = `GPS: ${data.gps_lat.toFixed(5)}, ${data.gps_lng.toFixed(5)}`;
                    mapBtn.style.display = 'inline-block';
                    mapBtn.href = `https://www.google.com/maps/search/?api=1&query=${data.gps_lat},${data.gps_lng}`;
                } else {
                    gpsDisp.textContent = currentLang === 'ko' ? 'GPS: 신호 대기 중...' : 'Waiting for GPS...';
                    mapBtn.style.display = 'none';
                    mapBtn.href = '#';
                }
            }
        }
    }

    const handleCapture = async (btnId) => {
        const btn = document.getElementById(btnId);
        if (!btn) return;
        const originalContent = btn.innerHTML;
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
                    updateLanguage(currentLang);
                    btn.disabled = false;
                }, 5000);
            }
        } catch (e) {
            addLog('System', 'Command failed: ' + e.message);
            btn.textContent = 'Error!';
            btn.disabled = false;
        }
    };

    document.getElementById('capture-btn')?.addEventListener('click', () => handleCapture('capture-btn'));
    document.getElementById('capture-btn-trans')?.addEventListener('click', () => handleCapture('capture-btn-trans'));

    const modal = document.getElementById('pricing-modal');
    const openBtn = document.getElementById('open-pricing-modal');
    const closeBtn = document.getElementById('close-modal');

    if (openBtn && modal) openBtn.onclick = () => modal.style.display = 'block';
    if (closeBtn && modal) closeBtn.onclick = () => modal.style.display = 'none';


    // [Extracted Module: Stats Monitor loaded from stats-monitor.js]

    // --- Sandwich Dropdown Menu Handler ---
    const menuTrigger = document.getElementById('header-menu-trigger');
    const dropdownMenu = document.getElementById('header-dropdown-menu');

    if (menuTrigger && dropdownMenu) {
        menuTrigger.addEventListener('click', (e) => {
            e.stopPropagation();
            const isOpen = dropdownMenu.classList.contains('show');
            dropdownMenu.classList.toggle('show', !isOpen);
            menuTrigger.setAttribute('aria-expanded', !isOpen);
        });

        // Close dropdown when any item inside is clicked
        dropdownMenu.querySelectorAll('.dropdown-item').forEach(btn => {
            btn.addEventListener('click', () => {
                dropdownMenu.classList.remove('show');
                menuTrigger.setAttribute('aria-expanded', 'false');
            });
        });
    }

    window.addEventListener('click', (e) => {
        if (dropdownMenu && !dropdownMenu.contains(e.target) && e.target !== menuTrigger) {
            dropdownMenu.classList.remove('show');
            if (menuTrigger) menuTrigger.setAttribute('aria-expanded', 'false');
        }
        if (e.target === modal) modal.style.display = 'none';
        if (e.target === statsModal) {
            statsModal.style.display = 'none';
            if (statsInterval) clearInterval(statsInterval);
        }
        if (e.target === hwModal) {
            hwModal.style.display = 'none';
            if (hwInterval) clearInterval(hwInterval);
        }
    });


    // [Extracted Module: Hardware Monitor loaded from hardware-monitor.js]

    document.getElementById('sync-t-ip').addEventListener('click', () => {
        const disp = document.getElementById('imagesendermcu-ip-display');
        const detectedIp = disp.dataset.ip;
        if (detectedIp && detectedIp !== 'Unknown') {
            document.getElementById('transmitter_ip').value = detectedIp;
            addLog('System', `ImageSenderMCU IP auto-filled: ${detectedIp}`);
        } else {
            alert(currentLang === 'ko' ? '감지된 IP가 없습니다.' : 'No detected IP available.');
        }
    });

    document.getElementById('sync-r-ip').addEventListener('click', () => {
        const disp = document.getElementById('hmi_mcu-ip-display');
        const detectedIp = disp.dataset.ip;
        if (detectedIp && detectedIp !== 'Unknown') {
            document.getElementById('receiver_ip').value = detectedIp;
            addLog('System', `HMI_MCU IP auto-filled: ${detectedIp}`);
        } else {
            alert(currentLang === 'ko' ? '감지된 IP가 없습니다.' : 'No detected IP available.');
        }
    });



    // [Extracted Module: Drive Simulation loaded from simulation-drive.js]

    // SSE Listener for real-time updates
    const eventSource = new EventSource('/api/status/stream');
    let syncTimer = null;

    eventSource.onmessage = (event) => {
        // 동기화 애니메이션 트리거
        const syncInd = document.getElementById('sync-indicator');
        if (syncInd) {
            syncInd.style.opacity = '1';
            clearTimeout(syncTimer);
            syncTimer = setTimeout(() => {
                syncInd.style.opacity = '0';
            }, 800);
        }

        try {
            const data = JSON.parse(event.data);
            if (data.type === 'status') {
                const stats = data.status || data.data;
                if (stats) {
                    // Cache the latest motor state globally for updateHUD to use
                    if (stats.drivemcu) {
                        window.currentMotorState = window.currentMotorState || {};
                        window.currentMotorState.tof_dist = stats.drivemcu.tof_dist;
                        window.currentMotorState.tof_status = stats.drivemcu.tof_status;
                        
                        // Update Environment Data if present (sent every 30s)
                        if (stats.drivemcu.env_temp !== undefined) {
                            const tEl = document.getElementById('env-temp');
                            const hEl = document.getElementById('env-humid');
                            const pEl = document.getElementById('env-press');
                            if (tEl) tEl.textContent = stats.drivemcu.env_temp.toFixed(1);
                            if (hEl) hEl.textContent = stats.drivemcu.env_humid.toFixed(1);
                            if (pEl) pEl.textContent = Math.round(stats.drivemcu.env_press);
                        }

                    }
                    Object.keys(stats).forEach(dev => {
                        updateDeviceUI(dev, stats[dev]);
                        
                        if ((dev === 'transmitter' || dev === 'imagesendermcu') && stats[dev].config) {
                            // --- HMI 카메라 설정 → PC UI 동기화 ---
                            const cfg = stats[dev].config;
                            const camModelEl = document.getElementById('camera_model');
                            if (camModelEl && cfg.camera_model) camModelEl.value = cfg.camera_model;
                            const resEl = document.getElementById('resolution');
                            if (resEl && cfg.resolution) resEl.value = cfg.resolution;
                            if (cfg.jpeg_quality !== undefined) {
                                const qRange = document.getElementById('jpeg_quality');
                                const qNum = document.getElementById('jpeg_quality-num');
                                const qVal = document.getElementById('quality-val');
                                if (qRange) qRange.value = cfg.jpeg_quality;
                                if (qNum) qNum.value = cfg.jpeg_quality;
                                if (qVal) qVal.textContent = cfg.jpeg_quality;
                            }
                            const modeEl = document.getElementById('transmission-mode');
                            if (modeEl && cfg.transmission_mode) modeEl.value = cfg.transmission_mode;
                            // AI Model sync
                            if (stats[dev].model_name) {
                                const modelEl = document.getElementById('gemini-model');
                                if (modelEl) modelEl.value = stats[dev].model_name;
                            }
                        } else if (dev === 'robot3axismcu' && stats[dev].state_data) {
                            window.currentRobot3AxisState = stats[dev].state_data;
                            if (window.updateRobot3AxisHUD) {
                                window.updateRobot3AxisHUD();
                            }
                        } else if (dev === 'controlmcu') {
                            const cData = stats[dev];
                            // Virtual Joystick Display
                            const vjX = document.getElementById('vj-x');
                            const vjY = document.getElementById('vj-y');
                            const stick = document.getElementById('virtual-joystick-stick');
                            if (vjX) vjX.textContent = cData.joy_x !== undefined ? cData.joy_x : 0;
                            if (vjY) vjY.textContent = cData.joy_y !== undefined ? cData.joy_y : 0;
                            
                            if (stick && cData.joy_x !== undefined && cData.joy_y !== undefined) {
                                const px = (cData.joy_x / 100) * 40;
                                const py = (-cData.joy_y / 100) * 40; 
                                stick.style.transform = `translate(calc(-50% + ${px}px), calc(-50% + ${py}px))`;
                            }
                            
                            const mcModeDisplay = document.getElementById('mc-mode-display');
                            const mcSubModeDisplay = document.getElementById('mc-submode-display');
                            const mcMappingInfo = document.getElementById('mc-mapping-info');
                            
                            if (mcModeDisplay) {
                                if (cData.mode === 'Mode A' || cData.mode === 'A') {
                                    mcModeDisplay.textContent = 'MODE A (Robot)';
                                    mcModeDisplay.style.color = 'var(--success)';
                                    if (mcSubModeDisplay) mcSubModeDisplay.textContent = 'Sub-Mode 1: X/Z Axis';
                                    if (mcMappingInfo) mcMappingInfo.innerHTML = '<strong>UP/DOWN:</strong> Z Axis Control<br><strong>LEFT/RIGHT:</strong> X Axis Control';
                                } else if (cData.mode === 'Mode B' || cData.mode === 'B') {
                                    mcModeDisplay.textContent = 'MODE B (Drive)';
                                    mcModeDisplay.style.color = 'var(--primary)';
                                    if (mcSubModeDisplay) mcSubModeDisplay.textContent = 'Sub-Mode 1: Differential Drive';
                                    if (mcMappingInfo) mcMappingInfo.innerHTML = '<strong>UP/DOWN:</strong> Forward/Backward<br><strong>LEFT/RIGHT:</strong> Turn (Yaw)';
                                } else {
                                    mcModeDisplay.textContent = 'NEUTRAL / OFFLINE';
                                    mcModeDisplay.style.color = 'var(--text-dim)';
                                    if (mcSubModeDisplay) mcSubModeDisplay.textContent = 'No Sub-Mode';
                                    if (mcMappingInfo) mcMappingInfo.innerHTML = 'No active mapping.';
                                }
                            }
                        }
                    });
                }
            } else if (data.type === 'user_speaking') {
                const indicator = document.getElementById('user-speaking-indicator');
                if (indicator) {
                    indicator.style.display = data.speaking ? 'flex' : 'none';
                }
                
                // 음성 입력 감지 즉시 채팅창 입력측(User)에 로딩 애니메이션 출력
                if (data.speaking) {
                    const mainChat = document.getElementById('chat-display-main');
                    if (mainChat && !document.getElementById('voice-loading-bubble')) {
                        // 기존 플레이스홀더 제거
                        const placeholder = document.getElementById('image-sensor-placeholder');
                        if (placeholder) placeholder.remove();

                        const bubble = document.createElement('div');
                        bubble.id = 'voice-loading-bubble';
                        bubble.className = 'chat-bubble chat-bubble-user';
                        bubble.innerHTML = `
                            <span class="role">User</span>
                            <div class="text" style="display: flex; align-items: center; gap: 5px; min-height: 20px;">
                                <span class="pulse-dot"></span>
                                <span class="pulse-dot" style="animation-delay: 0.2s;"></span>
                                <span class="pulse-dot" style="animation-delay: 0.4s;"></span>
                            </div>
                        `;
                        mainChat.appendChild(bubble);
                        mainChat.scrollTop = mainChat.scrollHeight;
                    }
                }
            } else if (data.type === 'control_mic') {
                if (data.action === 'start' && !isPcMicActive) {
                    if (typeof togglePCMic === 'function') togglePCMic();
                } else if (data.action === 'stop' && isPcMicActive) {
                    if (typeof stopPCMic === 'function') stopPCMic();
                }
            } else if (data.type === 'update') {
                if (data.analysis && data.analysis.type === 'voice') {
                    // STT 완료 시 임시 로딩 버블 즉시 삭제
                    const voiceLoading = document.getElementById('voice-loading-bubble');
                    if (voiceLoading) voiceLoading.remove();

                    addChatMessage(data.analysis.role, data.analysis.text, data.timings, data.turn_id);
                    
                    // PC 오디오 출력 처리 (AI 응답이고 토글이 켜져 있을 때)
                    const audioUrl = data.analysis.audio_url || data.audio_url;
                    const pcAudioToggle = document.getElementById('pc-audio-output-toggle');
                    if (audioUrl && pcAudioToggle?.checked && data.analysis.role === 'AI') {
                        const audio = new Audio(audioUrl);
                        audio.play().catch(e => console.error("Audio playback failed:", e));
                    }
                } else if (data.turn_id && data.timings) {
                    // 비동기 레이스 컨디션 해결: AI 메시지 버블이 완전히 DOM에 마운트될 때까지 최대 3초간 폴링 재시도
                    let retries = 0;
                    const maxRetries = 30;
                    const updateTimingWithRetry = () => {
                        const msgId = `msg-${data.turn_id}-ai`;
                        if (document.getElementById(msgId)) {
                            addChatMessage('AI', null, data.timings, data.turn_id);
                        } else if (retries < maxRetries) {
                            retries++;
                            setTimeout(updateTimingWithRetry, 100);
                        } else {
                            console.warn(`[Timing Update] AI message bubble with ID ${msgId} not found after 3s.`);
                        }
                    };
                    updateTimingWithRetry();
                } else {
                    updateUI(data);
                }
            } else if (data.type === 'log') {
                addLog(data.source, data.message, data.timestamp);
            } else if (data.type === 'youtube_progress') {
                const mainChat = document.getElementById('chat-display-main');
                if (mainChat) {
                    let progBubble = document.getElementById('youtube-progress-bubble');
                    if (!progBubble) {
                        progBubble = document.createElement('div');
                        progBubble.id = 'youtube-progress-bubble';
                        progBubble.className = 'chat-bubble chat-bubble-ai';
                        progBubble.style.background = 'rgba(16, 185, 129, 0.1)';
                        progBubble.style.border = '1px solid #10b981';
                        progBubble.innerHTML = `<span class="role" style="color:#10b981;">System Info</span><div class="text" id="youtube-progress-text"></div>`;
                        mainChat.appendChild(progBubble);
                    }
                    document.getElementById('youtube-progress-text').textContent = data.text;
                    mainChat.scrollTop = mainChat.scrollHeight;
                    
                    if (window.ytProgTimer) clearTimeout(window.ytProgTimer);
                    window.ytProgTimer = setTimeout(() => {
                        const pb = document.getElementById('youtube-progress-bubble');
                        if (pb) pb.remove();
                    }, 5000);
                }
            } else if (data.type === 'robot3axis_update') {
                window.currentRobot3AxisState = data.state;
                if (window.updateRobot3AxisHUD) {
                    window.updateRobot3AxisHUD();
                }
                // Sync Speed Inputs from HMI/Server
                if (data.state) {
                    const s = data.state;
                    if (s.x_speed !== undefined) {
                        const el = document.getElementById('r3_speed_x');
                        if (el) el.value = s.x_speed;
                    }
                    if (s.y_speed !== undefined) {
                        const el = document.getElementById('r3_speed_y');
                        if (el) el.value = s.y_speed;
                    }
                    if (s.z_speed !== undefined) {
                        const el = document.getElementById('r3_speed_z');
                        if (el) el.value = s.z_speed;
                    }
                }
            } else if (data.type === 'drive_update') {
                window.currentMotorState = data.state;
                if (window.syncDriveSim) {
                    window.syncDriveSim();
                }
                // --- HMI 설정값 → PC UI 동기화 ---
                if (data.settings) {
                    const s = data.settings;

                    // I2C 클럭 드롭다운 업데이트
                    const i2cSelect = document.getElementById('i2c-clock-speed');
                    if (i2cSelect && s.i2c_clock_hz !== undefined) {
                        i2cSelect.value = String(s.i2c_clock_hz);
                    }

                    // 하트비트 주기 → tof-tx-interval 숫자 필드 업데이트
                    const txIntervalEl = document.getElementById('tof-tx-interval');
                    if (txIntervalEl && s.hb_interval_ms !== undefined) {
                        txIntervalEl.value = s.hb_interval_ms;
                    }

                    // PID 토글 업데이트 (강제 이벤트 발생으로 UI 동기화)
                    const pidToggle = document.getElementById('rail-pid-toggle');
                    if (pidToggle && s.pid_mode !== undefined && pidToggle.checked !== s.pid_mode) {
                        pidToggle.checked = s.pid_mode;
                        // change 이벤트를 수동으로 트리거하여 UI 요소들이 반응하도록 함
                        pidToggle.dispatchEvent(new Event('change', { bubbles: true }));
                    }

                    // PID 목표 슬라이더 및 표시값 업데이트
                    if (s.pid_target_tof !== undefined) {
                        const pidSlider = document.getElementById('pid-target-slider');
                        const pidVal = document.getElementById('pid-target-val');
                        if (pidSlider) pidSlider.value = s.pid_target_tof;
                        if (pidVal) pidVal.textContent = s.pid_target_tof;
                    }
                }
            }
        } catch (e) {
            console.error("Failed to parse SSE data", e);
        }
    };

    eventSource.onerror = (e) => {
        addLog('System', 'SSE Connection lost. Reconnecting...');
    };
    function addChatMessage(role, text, timings = null, turnId = null) {
        const mainChat = document.getElementById('chat-display-main');
        if (!mainChat) return;
        
        // --- 1. 기존 메시지 업데이트 로직 (턴ID + 역할 조합으로 고유성 보장) ---
        if (turnId && role) {
            const msgId = `msg-${turnId}-${role.toLowerCase()}`;
            const existingMsg = document.getElementById(msgId);
            if (existingMsg) {
                if (text) {
                    const textEl = existingMsg.querySelector('.text');
                    if (textEl) textEl.textContent = text;
                }
                if (timings) {
                    let timingEl = existingMsg.querySelector('.timing-info');
                    if (!timingEl) {
                        timingEl = document.createElement('div');
                        timingEl.className = 'timing-info';
                        timingEl.style = "font-size: 0.75rem; color: #94a3b8; margin-top: 8px; font-family: monospace; border-top: 1px solid rgba(255,255,255,0.05); padding-top: 5px; display: flex; gap: 10px; flex-wrap: wrap;";
                        existingMsg.appendChild(timingEl);
                    }
                    
                    let html = '';
                    if (timings.stt !== undefined) html += `<span>STT: ${timings.stt}ms</span>`;
                    if (timings.ai !== undefined) html += `<span>AI: ${timings.ai}ms</span>`;
                    if (timings.tts !== undefined) html += `<span>TTS: ${timings.tts}ms</span>`;
                    if (timings.total !== undefined) html += `<span style="color: var(--primary); font-weight: bold;">Total: ${timings.total}ms</span>`;
                    timingEl.innerHTML = html;
                }
                return;
            }
        }

        // --- 2. 새 메시지 생성 로직 ---
        if (!role) return;

        const placeholder = document.getElementById('image-sensor-placeholder');
        if (placeholder) placeholder.remove();

        const bubble = document.createElement('div');
        bubble.className = `chat-bubble chat-bubble-${role.toLowerCase()}`;
        // ID에 역할명을 포함하여 질문과 답변이 서로 덮어쓰지 않도록 함
        if (turnId) bubble.id = `msg-${turnId}-${role.toLowerCase()}`;
        
        const showTiming = role === 'AI' || (timings && timings.stt !== undefined);
        let timingHtml = '';
        if (showTiming) {
            if (timings) {
                if (timings.stt !== undefined) timingHtml += `<span>STT: ${timings.stt}ms</span>`;
                if (timings.ai !== undefined) timingHtml += `<span>AI: ${timings.ai}ms</span>`;
                if (timings.tts !== undefined) timingHtml += `<span>TTS: ${timings.tts}ms</span>`;
                if (timings.total !== undefined) timingHtml += `<span style="color: var(--primary); font-weight: bold;">Total: ${timings.total}ms</span>`;
            } else {
                timingHtml = '<span>Processing...</span>';
            }
        }

        bubble.innerHTML = `
            <span class="role">${role}</span>
            <div class="text">${text || '...'}</div>
            ${showTiming ? `
            <div class="timing-info" style="font-size: 0.75rem; color: #94a3b8; margin-top: 8px; font-family: monospace; border-top: 1px solid rgba(255,255,255,0.05); padding-top: 5px; display: flex; gap: 10px; flex-wrap: wrap;">
                ${timingHtml}
            </div>
            ` : ''}
        `;
        
        mainChat.appendChild(bubble);
        mainChat.scrollTop = mainChat.scrollHeight;
        
        if (role && text) addLog(role, text);
    }


    // [Extracted Module: Robot 3-Axis Controller loaded from robot3axis-controller.js]


    // [Extracted Module: Media Controller loaded from media-controller.js]

});
