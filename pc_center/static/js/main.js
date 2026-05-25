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
            langBtn.textContent = lang === 'ko' ? 'ENGLISH UI' : '한국어 UI';
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
                            <div style="font-size: 5rem; margin-bottom: 20px;">💤</div>
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
    };

    // 기존 data-tab 버튼 지원 (하위 호환)
    document.querySelectorAll('.tab-btn[data-tab]').forEach(btn => {
        btn.addEventListener('click', () => window.switchTab(btn.dataset.tab));
    });

    // --- AI 성격 파일 에디터 ---
    async function loadPersonaFile() {
        const editor = document.getElementById('persona-editor');
        const status = document.getElementById('persona-save-status');
        if (!editor) return;
        try {
            const res = await fetch('/api/ai_persona');
            const data = await res.json();
            if (data.status === 'success') {
                editor.value = data.content;
                if (status) { status.textContent = '✓ 파일 로드됨'; status.style.color = 'var(--success)'; status.style.display = 'inline'; }
            }
        } catch (e) {
            if (editor) editor.value = '// 파일 로드 실패: 서버를 확인해 주세요.';
            console.error('Failed to load persona file', e);
        }
    }

    document.getElementById('save-persona-btn')?.addEventListener('click', async () => {
        const editor = document.getElementById('persona-editor');
        const status = document.getElementById('persona-save-status');
        if (!editor) return;
        const btn = document.getElementById('save-persona-btn');
        btn.disabled = true;
        btn.textContent = '저장 중...';
        try {
            const res = await fetch('/api/ai_persona', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ content: editor.value })
            });
            const data = await res.json();
            if (data.status === 'success') {
                if (status) { status.textContent = '✅ 저장 완료 — AI에 즉시 반영됨'; status.style.color = 'var(--success)'; status.style.display = 'inline'; }
            } else {
                if (status) { status.textContent = '❌ 저장 실패'; status.style.color = 'var(--danger)'; status.style.display = 'inline'; }
            }
        } catch (e) {
            if (status) { status.textContent = '❌ 오류'; status.style.color = 'var(--danger)'; status.style.display = 'inline'; }
        }
        btn.disabled = false;
        btn.textContent = '💾 저장';
        setTimeout(() => { if (status) status.style.display = 'none'; }, 4000);
    });

    document.getElementById('reload-persona-btn')?.addEventListener('click', loadPersonaFile);

    // --- SoundMCU 통합 설정 (VAD, Sensitivity, TTS) ---
    async function updateSoundMCUUI() {
        try {
            const res = await fetch('/api/soundmcu/status');
            const data = await res.json();
            if (data.status === 'success') {
                const conf = data.config;
                // VAD/PTT 모드 업데이트
                const modeToggle = document.getElementById('voice-mode-toggle');
                if (modeToggle) modeToggle.checked = (conf.mode === 'VAD');
                
                // 감도 업데이트
                const sensSlider = document.getElementById('voice-sensitivity');
                if (sensSlider) sensSlider.value = conf.sensitivity;

                // 볼륨 업데이트
                const volumeSlider = document.getElementById('mcu-volume');
                if (volumeSlider) {
                    volumeSlider.value = conf.volume !== undefined ? conf.volume : 12;
                    const volValSpan = document.getElementById('mcu-volume-val');
                    if (volValSpan) volValSpan.textContent = volumeSlider.value;
                    
                    // 신규 글로벌 미디어 볼륨 슬라이더 싱크!
                    const globalVolSlider = document.getElementById('global-media-volume');
                    if (globalVolSlider) {
                        globalVolSlider.value = volumeSlider.value;
                        const globalVolSpan = document.getElementById('global-media-volume-val');
                        if (globalVolSpan) globalVolSpan.textContent = volumeSlider.value;
                    }
                }
                
                // TTS 기본 설정
                const pitchSlider = document.getElementById('tts-pitch');
                const rateSlider = document.getElementById('tts-rate');
                const effectSelect = document.getElementById('tts-effect');
                const openaiKeyInput = document.getElementById('openai_api_key');
                const azureKeyInput = document.getElementById('azure_api_key');
                const azureRegionInput = document.getElementById('azure_region');
                
                if (pitchSlider) { pitchSlider.value = conf.tts_pitch || 0; document.getElementById('pitch-val').textContent = pitchSlider.value; }
                if (rateSlider) { rateSlider.value = conf.tts_rate || 0; document.getElementById('rate-val').textContent = rateSlider.value; }
                
                // 감정 강도 설정 불러오기 동기화
                const degreeSlider = document.getElementById('emotion-lab-degree');
                const degreeSpan = document.getElementById('emotion-lab-degree-val');
                if (degreeSlider) {
                    const savedDegree = conf.tts_style_degree !== undefined ? conf.tts_style_degree : 1.5;
                    degreeSlider.value = savedDegree;
                    if (degreeSpan) degreeSpan.textContent = parseFloat(savedDegree).toFixed(1);
                }
                if (effectSelect) effectSelect.value = conf.tts_effect || 'None';
                if (openaiKeyInput) openaiKeyInput.value = conf.openai_api_key || '';
                if (azureKeyInput) azureKeyInput.value = conf.azure_api_key || '';
                if (azureRegionInput) azureRegionInput.value = conf.azure_region || 'koreacentral';
                
                const controlIP = document.getElementById('controlmcu_ip');
                if (controlIP) controlIP.value = conf.ip || '';

                // TTS 엔진 업데이트
                const engineSelect = document.getElementById('tts-engine-select');
                if (engineSelect) {
                    engineSelect.value = conf.tts_engine || 'Edge-TTS';
                    toggleOpenAIKeyUI(engineSelect.value);
                    // 엔진에 맞는 목소리 목록 로드
                    await loadTTSVoices(engineSelect.value, conf.tts_voice);
                }

                // STT 엔진 업데이트
                const sttSelect = document.getElementById('stt-engine-select');
                if (sttSelect) sttSelect.value = conf.stt_engine || 'Whisper-Local';
            }
        } catch (e) {
            console.error('Failed to update SoundMCU UI', e);
        }
    }

    async function loadTTSVoices(engine, selectedVoice = null) {
        const voiceSelect = document.getElementById('tts-voice-select');
        if (!voiceSelect) return;
        
        try {
            const res = await fetch(`/api/soundmcu/voices?engine=${engine}`);
            const data = await res.json();
            if (data.status === 'success') {
                voiceSelect.innerHTML = '';
                data.voices.forEach(v => {
                    const opt = document.createElement('option');
                    opt.value = v.id;
                    opt.textContent = v.name;
                    if (selectedVoice && v.id === selectedVoice) opt.selected = true;
                    voiceSelect.appendChild(opt);
                });
            }
        } catch (e) {
            console.error('Failed to load TTS voices', e);
        }
    }

    async function saveSoundMCUConfig() {
        window.saveSoundMCUConfig = saveSoundMCUConfig;
        const mode = document.getElementById('voice-mode-toggle')?.checked ? 'VAD' : 'PTT';
        const sensitivity = document.getElementById('voice-sensitivity')?.value;
        const volume = document.getElementById('mcu-volume')?.value;
        const engine = document.getElementById('tts-engine-select')?.value;
        const pitch = document.getElementById('tts-pitch')?.value;
        const rate = document.getElementById('tts-rate')?.value;
        const effect = document.getElementById('tts-effect')?.value;
        const styleDegree = document.getElementById('emotion-lab-degree')?.value || 1.5;
        const sttEngine = document.getElementById('stt-engine-select')?.value;
        const openaiKey = document.getElementById('openai_api_key')?.value;
        const azureKey = document.getElementById('azure_api_key')?.value;
        const azureRegion = document.getElementById('azure_region')?.value;
        const ip = document.getElementById('controlmcu_ip')?.value;

        try {
            await fetch('/api/soundmcu/settings', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({
                    mode: mode,
                    sensitivity: parseInt(sensitivity),
                    volume: parseInt(volume),
                    tts_engine: engine,
                    tts_voice: document.getElementById('tts-voice-select')?.value,
                    tts_pitch: pitch,
                    tts_rate: rate,
                    tts_effect: effect,
                    tts_style_degree: parseFloat(styleDegree),
                    stt_engine: sttEngine,
                    openai_api_key: openaiKey,
                    azure_api_key: azureKey,
                    azure_region: azureRegion,
                    ip: ip
                })
            });
            // 시스템 로그에 표시
            if (window.addLog) window.addLog('System', `Sound 설정 저장됨: STT(${sttEngine}) / TTS(${engine}) / Vol(${volume}) / EmotionDegree(${styleDegree})`);
        } catch (e) {
            console.error('Failed to save SoundMCU config', e);
        }
    }

    window.stopAllAudio = async function() {
        try {
            const response = await fetch('/api/soundmcu/stop', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'}
            });
            const data = await response.json();
            if (data.status === 'success') {
                if (window.addLog) window.addLog('Sound', '모든 오디오 즉각 정지 명령 전송 완료');
            } else {
                console.error('Failed to stop audio:', data.message);
            }
        } catch (e) {
            console.error('Failed to stop audio:', e);
        }
    };

    function toggleOpenAIKeyUI(engine) {
        const container = document.getElementById('openai-key-container');
        if (container) container.style.display = (engine === 'OpenAI') ? 'block' : 'none';
        
        const azureContainer = document.getElementById('azure-key-container');
        if (azureContainer) azureContainer.style.display = (engine === 'Azure-TTS') ? 'block' : 'none';
    }

    window.applyTTSPreset = function(type) {
        const pitchSlider = document.getElementById('tts-pitch');
        const rateSlider = document.getElementById('tts-rate');
        const effectSelect = document.getElementById('tts-effect');

        if (type === 'child') {
            if (pitchSlider) pitchSlider.value = 40;
            if (rateSlider) rateSlider.value = 15;
            if (effectSelect) effectSelect.value = 'None';
        } else if (type === 'elderly') {
            if (pitchSlider) pitchSlider.value = -20;
            if (rateSlider) rateSlider.value = -15;
            if (effectSelect) effectSelect.value = 'None';
        } else if (type === 'reset') {
            if (pitchSlider) pitchSlider.value = 0;
            if (rateSlider) rateSlider.value = 0;
            if (effectSelect) effectSelect.value = 'None';
        }

        if (pitchSlider) document.getElementById('pitch-val').textContent = pitchSlider.value;
        if (rateSlider) document.getElementById('rate-val').textContent = rateSlider.value;
        
        saveSoundMCUConfig();
    };

    // 이벤트 리스너 등록
    document.getElementById('tts-engine-select')?.addEventListener('change', async (e) => {
        const engine = e.target.value;
        toggleOpenAIKeyUI(engine);
        await loadTTSVoices(engine);
        saveSoundMCUConfig();
    });

    // --- PC Microphone Logic (Internal) ---
    let pcMicAudioCtx = null;
    let pcMicStream = null;
    let pcMicProcessor = null;
    let isPcMicActive = false;
    let pcAudioBuffer = [];

    async function togglePCMic() {
        const btn = document.getElementById('toggle-pc-mic');
        const indicator = document.getElementById('pc-mic-indicator');
        const btnText = document.getElementById('mic-btn-text');

        if (isPcMicActive) {
            stopPCMic();
        } else {
            // 보안 컨텍스트 체크 (HTTPS 또는 localhost가 아니면 마이크 사용 불가)
            if (!window.isSecureContext) {
                alert("브라우저 보안 정책상 IP 주소 접속 시에는 마이크를 사용할 수 없습니다.\n\n'http://localhost:5000' 주소로 접속하거나 HTTPS 환경을 사용해 주세요.");
                return;
            }

            try {
                pcMicStream = await navigator.mediaDevices.getUserMedia({ audio: true });
                pcMicAudioCtx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 16000 });
                const source = pcMicAudioCtx.createMediaStreamSource(pcMicStream);
                pcMicProcessor = pcMicAudioCtx.createScriptProcessor(4096, 1, 1);

                pcAudioBuffer = [];
                isPcMicActive = true;
                btn.style.background = 'var(--danger)';
                btnText.textContent = 'STOP MIC';
                indicator.style.background = 'var(--danger)';
                indicator.classList.add('pulse-mic');
                const waveContainer = document.getElementById('mic-wave-container');
                if (waveContainer) waveContainer.style.opacity = '1';

                pcMicProcessor.onaudioprocess = (e) => {
                    const inputData = e.inputBuffer.getChannelData(0);
                    let sum = 0;
                    for(let i=0; i<inputData.length; i++) {
                        sum += inputData[i] * inputData[i];
                        pcAudioBuffer.push(inputData[i]);
                    }
                    const rms = Math.sqrt(sum / inputData.length);
                    const level = Math.min(100, rms * 400); // 볼륨 감도 조정
                    
                    // Update Wave Bars
                    const bars = document.querySelectorAll('.mic-bar');
                    bars.forEach((bar, idx) => {
                        // 중앙으로 갈수록 가중치 부여 (웨이브 효과)
                        const weight = 1 - Math.abs(idx - 3) / 4; 
                        const h = Math.max(10, level * weight * 1.5 + (Math.sin(Date.now()/50 + idx)*5));
                        bar.style.height = Math.min(100, h) + '%';
                        bar.style.background = h > 50 ? 'var(--primary)' : 'rgba(56, 189, 248, 0.5)';
                    });
                };

                source.connect(pcMicProcessor);
                pcMicProcessor.connect(pcMicAudioCtx.destination);
                
                if (window.addLog) window.addLog('System', 'PC Microphone recording started (16kHz)...');
            } catch (err) {
                console.error("Microphone access error:", err);
                alert("마이크 접근 권한이 없거나 오류가 발생했습니다: " + err);
            }
        }
    }

    function stopPCMic() {
        if (!isPcMicActive) return;
        
        isPcMicActive = false;
        const btn = document.getElementById('toggle-pc-mic');
        const indicator = document.getElementById('pc-mic-indicator');
        const btnText = document.getElementById('mic-btn-text');

        if (btn) btn.style.background = '';
        if (btnText) btnText.textContent = 'START MIC';
        if (indicator) {
            indicator.style.background = '#475569';
            indicator.classList.remove('pulse-mic');
        }
        const waveContainer = document.getElementById('mic-wave-container');
        if (waveContainer) {
            waveContainer.style.opacity = '0';
            document.querySelectorAll('.mic-bar').forEach(b => b.style.height = '10%');
        }

        if (pcAudioBuffer.length > 0) {
            sendMicDataToServer(pcAudioBuffer);
        }

        if (pcMicProcessor) { pcMicProcessor.disconnect(); pcMicProcessor = null; }
        if (pcMicAudioCtx) { pcMicAudioCtx.close(); pcMicAudioCtx = null; }
        if (pcMicStream) { pcMicStream.getTracks().forEach(t => t.stop()); pcMicStream = null; }
    }

    async function sendMicDataToServer(floatBuffer) {
        // Convert Float32 to Int16
        const int16Buffer = new Int16Array(floatBuffer.length);
        for (let i = 0; i < floatBuffer.length; i++) {
            let s = Math.max(-1, Math.min(1, floatBuffer[i]));
            int16Buffer[i] = s < 0 ? s * 0x8000 : s * 0x7FFF;
        }

        try {
            if (window.addLog) window.addLog('System', `Sending PC Mic data (${int16Buffer.length} samples)...`);
            const res = await fetch('/api/pc_mic/voice', {
                method: 'POST',
                headers: { 'Content-Type': 'application/octet-stream' },
                body: int16Buffer.buffer
            });
            const result = await res.json();
            if (result.status === 'success' && window.addLog) {
                window.addLog('Success', 'PC Mic voice processing triggered.');
            }
        } catch (e) {
            console.error("Failed to send Mic data", e);
            if (window.addLog) window.addLog('Error', 'Failed to send PC Mic data: ' + e);
        }
    }

    document.getElementById('toggle-pc-mic')?.addEventListener('click', togglePCMic);

    document.getElementById('tts-voice-select')?.addEventListener('change', saveSoundMCUConfig);
    document.getElementById('stt-engine-select')?.addEventListener('change', saveSoundMCUConfig);
    document.getElementById('voice-mode-toggle')?.addEventListener('change', saveSoundMCUConfig);
    document.getElementById('voice-sensitivity')?.addEventListener('change', saveSoundMCUConfig);
    document.getElementById('mcu-volume')?.addEventListener('input', (e) => { document.getElementById('mcu-volume-val').textContent = e.target.value; });
    document.getElementById('mcu-volume')?.addEventListener('change', saveSoundMCUConfig);
    document.getElementById('controlmcu_ip')?.addEventListener('change', saveSoundMCUConfig);
    
    document.getElementById('tts-pitch')?.addEventListener('input', (e) => { document.getElementById('pitch-val').textContent = e.target.value; });
    document.getElementById('tts-pitch')?.addEventListener('change', saveSoundMCUConfig);
    document.getElementById('tts-rate')?.addEventListener('input', (e) => { document.getElementById('rate-val').textContent = e.target.value; });
    document.getElementById('tts-rate')?.addEventListener('change', saveSoundMCUConfig);
    document.getElementById('tts-effect')?.addEventListener('change', saveSoundMCUConfig);
    document.getElementById('openai_api_key')?.addEventListener('change', saveSoundMCUConfig);
    document.getElementById('azure_api_key')?.addEventListener('change', saveSoundMCUConfig);
    document.getElementById('azure_region')?.addEventListener('change', saveSoundMCUConfig);
    document.getElementById('emotion-lab-degree')?.addEventListener('change', saveSoundMCUConfig);

    // --- 🎭 Microsoft Azure 감정 음성 연구소 (Emotional Voice Lab) 재생/정지 제어 ---
    let emotionLabAudio = null;

    window.playEmotionLabSpeech = async function() {
        const text = document.getElementById('emotion-lab-text')?.value?.trim();
        const style = document.getElementById('emotion-lab-style')?.value;
        const degree = document.getElementById('emotion-lab-degree')?.value;
        const statusSpan = document.getElementById('emotion-lab-status');
        const playBtn = document.getElementById('emotion-lab-play-btn');

        if (!text) {
            alert("테스트 발화 텍스트를 입력해 주세요!");
            return;
        }

        try {
            if (statusSpan) {
                statusSpan.textContent = "Azure Neural TTS 합성 및 오디오 생성 중... ⚡";
                statusSpan.style.display = "block";
            }
            if (playBtn) playBtn.disabled = true;

            // 이전 오디오 즉각 중지
            if (emotionLabAudio) {
                emotionLabAudio.pause();
                emotionLabAudio = null;
            }

            const res = await fetch('/api/soundmcu/test_emotion_tts', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    text: text,
                    style: style,
                    style_degree: parseFloat(degree)
                })
            });

            const data = await res.json();
            if (data.status === 'success') {
                if (statusSpan) statusSpan.textContent = "오디오 재생 중... 🔊";
                
                emotionLabAudio = new Audio(data.audio_url);
                emotionLabAudio.play();

                emotionLabAudio.addEventListener('ended', () => {
                    if (statusSpan) statusSpan.style.display = "none";
                    if (playBtn) playBtn.disabled = false;
                    emotionLabAudio = null;
                });

                emotionLabAudio.addEventListener('error', (err) => {
                    console.error("Audio Playback Error:", err);
                    if (statusSpan) {
                        statusSpan.textContent = "❌ 오디오 재생 실패";
                        statusSpan.style.color = "var(--danger)";
                    }
                    if (playBtn) playBtn.disabled = false;
                    emotionLabAudio = null;
                    setTimeout(() => { if (statusSpan) statusSpan.style.display = "none"; }, 3000);
                });
            } else {
                alert("감정 음성 합성 실패: " + data.message);
                if (statusSpan) statusSpan.style.display = "none";
                if (playBtn) playBtn.disabled = false;
            }
        } catch (e) {
            console.error("Emotion Lab Fetch Error:", e);
            alert("서버 연결 실패: " + e.message);
            if (statusSpan) statusSpan.style.display = "none";
            if (playBtn) playBtn.disabled = false;
        }
    };

    window.stopEmotionLabSpeech = function() {
        const statusSpan = document.getElementById('emotion-lab-status');
        const playBtn = document.getElementById('emotion-lab-play-btn');
        
        if (emotionLabAudio) {
            emotionLabAudio.pause();
            emotionLabAudio = null;
        }
        if (statusSpan) statusSpan.style.display = "none";
        if (playBtn) playBtn.disabled = false;
        if (window.addLog) window.addLog('Sound', '감정 테스트 음성 재생 수동 정지');
    };

    // --- AI 성격 파일 에디터 ---
    const sendChatMessage = async () => {
        const input = document.getElementById('chat-input');
        const text = input?.value?.trim();
        if (!text) return;
        input.value = '';
        
        // 로컬 고유 턴 ID 생성
        const turnId = `t_${Date.now()}`;
        // 텍스트 입력 즉시 채팅창에 User 말풍선 노출 (turnId 바인딩!)
        addChatMessage('User', text, null, turnId);
        
        try {
            await fetch('/api/soundmcu/chat', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ text: text, turn_id: turnId })
            });
        } catch (e) { console.error(e); }
    };

    document.getElementById('send-chat-btn')?.addEventListener('click', sendChatMessage);
    document.getElementById('chat-input')?.addEventListener('keypress', (e) => {
        if (e.key === 'Enter') sendChatMessage();
    });

    // --- Audio Test Function ---
    window.testAudio = function() {
        try {
            const ctx = new (window.AudioContext || window.webkitAudioContext)();
            const osc = ctx.createOscillator();
            const gain = ctx.createGain();
            
            osc.type = 'sine';
            osc.frequency.setValueAtTime(440, ctx.currentTime); // A4 note
            
            gain.gain.setValueAtTime(0, ctx.currentTime);
            gain.gain.linearRampToValueAtTime(0.3, ctx.currentTime + 0.05);
            gain.gain.linearRampToValueAtTime(0, ctx.currentTime + 0.2);
            
            osc.connect(gain);
            gain.connect(ctx.destination);
            
            osc.start();
            osc.stop(ctx.currentTime + 0.2);
            
            if (window.addLog) window.addLog('System', 'Audio test signal sent to browser.');
        } catch (e) {
            console.error('Audio test failed:', e);
            alert('Audio test failed: ' + e);
        }
    };

    // --- Melody & TTS Test Event Handlers ---
    const testMelodyBtn = document.getElementById('test-melody-btn');
    const testTtsBtn = document.getElementById('test-tts-btn');
    const testTtsInput = document.getElementById('test-tts-input');
    const testAudioStatus = document.getElementById('test-audio-status');

    const setAudioStatus = (text, isError = false) => {
        if (!testAudioStatus) return;
        testAudioStatus.textContent = text;
        testAudioStatus.style.color = isError ? 'var(--danger, #ef4444)' : 'var(--success, #10b981)';
        testAudioStatus.style.display = 'block';
        setTimeout(() => {
            testAudioStatus.style.display = 'none';
        }, 5000);
    };

    testMelodyBtn?.addEventListener('click', async () => {
        if (testMelodyBtn.disabled) return;
        testMelodyBtn.disabled = true;
        setAudioStatus('멜로디 생성 및 전송 중...');
        try {
            const res = await fetch('/api/test_melody', { method: 'POST' });
            const result = await res.json();
            if (result.status === 'success') {
                setAudioStatus('✅ 멜로디 전송 완료! MCU 출력을 확인하세요.');
                if (window.addLog) window.addLog('System', '멜로디 테스트 전송이 시작되었습니다.');
            } else {
                setAudioStatus('❌ 전송 실패: ' + result.message, true);
            }
        } catch (e) {
            setAudioStatus('❌ 네트워크 오류: ' + e.message, true);
        } finally {
            testMelodyBtn.disabled = false;
        }
    });

    testTtsBtn?.addEventListener('click', async () => {
        if (testTtsBtn.disabled) return;
        const text = testTtsInput?.value?.trim() || "안녕하세요. 지능형 로봇 시스템 테스트 중입니다.";
        testTtsBtn.disabled = true;
        setAudioStatus('TTS 오디오 생성 및 전송 중...');
        try {
            const res = await fetch('/api/test_tts', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ text: text })
            });
            const result = await res.json();
            if (result.status === 'success') {
                setAudioStatus('✅ TTS 전송 완료! MCU 출력을 확인하세요.');
                if (window.addLog) window.addLog('System', `TTS 테스트 전송 시작: "${text}"`);
            } else {
                setAudioStatus('❌ 전송 실패: ' + result.message, true);
            }
        } catch (e) {
            setAudioStatus('❌ 네트워크 오류: ' + e.message, true);
        } finally {
            testTtsBtn.disabled = false;
        }
    });

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

    openBtn.onclick = () => modal.style.display = 'block';
    closeBtn.onclick = () => modal.style.display = 'none';

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


    // --- DriveMCU 모터 제어 로직 ---
    const sendMotorCmd = async (cmd) => {
        try {
            const res = await fetch('/api/drivemcu/command', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ command: cmd })
            });
            if (!res.ok) throw new Error("Server responded with " + res.status);
            addLog('System', `Drive command sent: ${cmd}`);
        } catch (e) {
            addLog('System', `Motor command failed: ${e.message}`);
        }
    };

    // --- DriveMCU Dual-Axis Motor Control ---
    const setupMotorAxis = (axis) => {
        const prefix = `m${axis}`; // mx or my
        const stepsInput = document.getElementById(`motor-steps-${axis}`);
        const rpmSlider = document.getElementById(`motor-rpm-${axis}`);
        const accelSlider = document.getElementById(`motor-accel-${axis}`);
        const rpmVal = document.getElementById(`rpm-val-${axis}`);
        const delayVal = document.getElementById(`delay-val-${axis}`);
        const accelVal = document.getElementById(`accel-val-${axis}`);
        const stepsVal = document.getElementById(`steps-val-${axis}`);

        const updateRpmUI = () => {
            const rpm = parseInt(rpmSlider.value);
            const stepsPerSec = Math.round(rpm * 200 / 60);
            const delayUs = stepsPerSec > 0 ? Math.round(1000000 / stepsPerSec) : 0;
            rpmVal.textContent = rpm;
            delayVal.textContent = delayUs;
            const rpmNum = document.getElementById(`motor-rpm-${axis}-num`);
            if (rpmNum) rpmNum.value = rpm;
        };

        const rpmNum = document.getElementById(`motor-rpm-${axis}-num`);
        rpmNum?.addEventListener('input', (e) => {
            rpmSlider.value = e.target.value;
            updateRpmUI();
            const stepsPerSec = Math.round(parseInt(e.target.value) * 200 / 60);
            sendMotorCmd(`${prefix}s${stepsPerSec}`);
        });

        rpmSlider?.addEventListener('input', updateRpmUI);
        rpmSlider?.addEventListener('change', (e) => {
            const rpm = parseInt(e.target.value);
            const stepsPerSec = Math.round(rpm * 200 / 60);
            sendMotorCmd(`${prefix}s${stepsPerSec}`);
        });

        const accelNum = document.getElementById(`motor-accel-${axis}-num`);
        accelSlider?.addEventListener('input', (e) => {
            accelVal.textContent = e.target.value;
            if (accelNum) accelNum.value = e.target.value;
        });
        accelNum?.addEventListener('input', (e) => {
            accelSlider.value = e.target.value;
            accelVal.textContent = e.target.value;
            sendMotorCmd(`${prefix}a${e.target.value}`);
        });
        accelSlider?.addEventListener('change', (e) => {
            sendMotorCmd(`${prefix}a${e.target.value}`);
        });

        stepsInput?.addEventListener('input', (e) => {
            stepsVal.textContent = e.target.value;
        });

        document.getElementById(`motor-start-btn-${axis}`)?.addEventListener('click', () => sendMotorCmd(`${prefix}e`));
        document.getElementById(`motor-stop-btn-${axis}`)?.addEventListener('click', () => sendMotorCmd(`${prefix}x`));
        document.getElementById(`move-fw-btn-${axis}`)?.addEventListener('click', () => {
            const steps = parseInt(stepsInput.value || 200);
            const gearRatio = parseFloat(document.getElementById(`gear_ratio_${axis}`)?.value || 1.0);
            const motorSteps = Math.round(steps * gearRatio); // 물리 모터 스텝 증폭
            sendMotorCmd(`${prefix}r${motorSteps}`);
            
            // 시뮬레이션은 사용자가 의도한 최종 출력 각도만큼 회전 표시
            const rad = ((steps / 100) * Math.PI);
            if (axis === 'x') targetAngleX += rad;
            else if (axis === 'y') targetAngleY += rad;
            else if (axis === 'z') targetAngleZ += rad;
        });
        document.getElementById(`move-bw-btn-${axis}`)?.addEventListener('click', () => {
            const steps = parseInt(stepsInput.value || 200);
            const gearRatio = parseFloat(document.getElementById(`gear_ratio_${axis}`)?.value || 1.0);
            const motorSteps = Math.round(steps * gearRatio);
            sendMotorCmd(`${prefix}r-${motorSteps}`);
            
            const rad = ((steps / 100) * Math.PI);
            if (axis === 'x') targetAngleX -= rad;
            else if (axis === 'y') targetAngleY -= rad;
            else if (axis === 'z') targetAngleZ -= rad;
        });
    };

    setupMotorAxis('x');
    setupMotorAxis('y');
    setupMotorAxis('z');

    document.getElementById('motor-global-stop')?.addEventListener('click', () => {
        sendMotorCmd('x');
        stopSimAnimations();
    });

    // --- Differential Simulation Control ---
    // --- Advanced 3D Differential Simulation (Three.js) ---
    
    const diskStepsInput = document.getElementById('motor-steps-disk');
    const diskStepsNum = document.getElementById('motor-steps-disk-num');
    const diskStepsVal = document.getElementById('disk-steps-val');

    const updateDiskSteps = (val) => {
        diskStepsVal.textContent = val;
        if (diskStepsInput) diskStepsInput.value = val;
        if (diskStepsNum) diskStepsNum.value = val;
    };

    diskStepsInput?.addEventListener('input', (e) => updateDiskSteps(e.target.value));
    diskStepsNum?.addEventListener('input', (e) => updateDiskSteps(e.target.value));

    let scene, camera, renderer, clock;
    let miniCamera, miniRenderer;
    let motorX, gearX, motorY, gearY, coupler, disk, pins = [];
    let isSimRunning = false;
    let animationId;
    
    // Z-axis specific targets for Rail
    let gearAngleZ = 0;
    let targetAngleZ = 0;
    const SCREW_LEAD = 8.0; // T8 lead screw default 8mm/rev
    
    // Predicted Displacement State
    let predictedState = {
        roll: 0,
        yaw: 0,
        mx: 0,
        my: 0,
        mz: 0,
        z_mm: 0
    };

    let carrierGroup, yawGroup, bottomSpider;
    let angleChart = null;

    const initChart = () => {
        const ctx = document.getElementById('angle-chart').getContext('2d');
        const gradientRoll = ctx.createLinearGradient(0, 0, 0, 150);
        gradientRoll.addColorStop(0, 'rgba(56, 189, 248, 0.4)');
        gradientRoll.addColorStop(1, 'rgba(56, 189, 248, 0)');

        const gradientYaw = ctx.createLinearGradient(0, 0, 0, 150);
        gradientYaw.addColorStop(0, 'rgba(129, 140, 248, 0.4)');
        gradientYaw.addColorStop(1, 'rgba(129, 140, 248, 0)');

        angleChart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: Array(50).fill(''),
                datasets: [{
                    label: 'Disk Roll',
                    data: Array(50).fill(0),
                    borderColor: '#38bdf8',
                    backgroundColor: gradientRoll,
                    borderWidth: 2,
                    fill: true,
                    tension: 0.4,
                    pointRadius: 0
                }, {
                    label: 'Disk Yaw',
                    data: Array(50).fill(0),
                    borderColor: '#818cf8',
                    backgroundColor: gradientYaw,
                    borderWidth: 2,
                    fill: true,
                    tension: 0.4,
                    pointRadius: 0
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                animation: false,
                scales: {
                    x: { display: false },
                    y: { 
                        grid: { color: 'rgba(255,255,255,0.05)' },
                        ticks: { color: '#94a3b8', font: { size: 10 } }
                    }
                },
                plugins: {
                    legend: {
                        labels: { color: '#f8fafc', font: { size: 10 }, boxWidth: 10 }
                    }
                }
            }
        });
    };

    const updateChart = (roll, yaw) => {
        if (!angleChart) initChart();
        const data = angleChart.data;
        data.datasets[0].data.shift();
        data.datasets[0].data.push(roll);
        data.datasets[1].data.shift();
        data.datasets[1].data.push(yaw);
        angleChart.update();
    };

    const initThreeJS = () => {
        const container = document.getElementById('three-container');
        if (!container || scene) return;

        scene = new THREE.Scene();
        clock = new THREE.Clock();
        
        const width = container.clientWidth;
        const height = container.clientHeight;
        
        camera = new THREE.PerspectiveCamera(45, width / height, 0.1, 1000);
        camera.position.set(0, 15, 30);
        camera.lookAt(0, 0, 0);

        // --- Main Renderer ---
        renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
        renderer.setPixelRatio(window.devicePixelRatio);
        renderer.setSize(container.clientWidth, container.clientHeight);
        renderer.setClearColor(0x000000, 0); 
        container.appendChild(renderer.domElement);

        // --- Mini Camera Array ---
        const miniContainer = document.getElementById('mini-three-container');
        if (miniContainer) {
            miniCamera = new THREE.PerspectiveCamera(40, miniContainer.clientWidth / 180, 0.1, 1000);
            miniCamera.position.set(20, 15, 20);
            miniCamera.lookAt(0, 0, 0);
        }

        // Lighting
        const ambientLight = new THREE.AmbientLight(0xffffff, 1.2);
        scene.add(ambientLight);
        
        const pointLight = new THREE.PointLight(0x38bdf8, 2, 100);
        pointLight.position.set(10, 20, 10);
        scene.add(pointLight);

        const spotLight = new THREE.SpotLight(0xffffff, 1);
        spotLight.position.set(-10, 15, 10);
        scene.add(spotLight);

        // Groups
        const differentialGroup = new THREE.Group();
        scene.add(differentialGroup);
        
        carrierGroup = new THREE.Group();
        differentialGroup.add(carrierGroup);
        
        yawGroup = new THREE.Group();
        carrierGroup.add(yawGroup);

        // Materials
        const matMotorFrame = new THREE.MeshPhongMaterial({ color: 0x334155, shininess: 10 });
        const matMotorCore = new THREE.MeshPhongMaterial({ color: 0x0f172a, shininess: 10 });
        
        const matGearX = new THREE.MeshPhongMaterial({ color: 0xef4444, shininess: 50 }); // Red
        const matGearY = new THREE.MeshPhongMaterial({ color: 0x3b82f6, shininess: 50 }); // Blue
        const matSpider = new THREE.MeshPhongMaterial({ color: 0x10b981, shininess: 60 }); // Green
        
        const matShaft = new THREE.MeshPhongMaterial({ color: 0x94a3b8, shininess: 80 }); 
        const matDisk = new THREE.MeshPhongMaterial({ color: 0xa855f7, transparent: true, opacity: 0.8, shininess: 80 }); // Purple
        const matBracket = new THREE.MeshPhongMaterial({ color: 0xf59e0b, shininess: 40 }); // Amber

        // Helper for outlines
        const addOutlines = (mesh, color = 0xffffff) => {
            const edges = new THREE.EdgesGeometry(mesh.geometry);
            const line = new THREE.LineSegments(edges, new THREE.LineBasicMaterial({ color: color, transparent: true, opacity: 0.5 }));
            mesh.add(line);
        };

        // 1. Motors & Mounts
        const motorGeom = new THREE.BoxGeometry(4, 4, 5);
        const motorBlackGeom = new THREE.BoxGeometry(3.8, 3.8, 4.2);
        
        motorX = new THREE.Group();
        const mXFrame = new THREE.Mesh(motorGeom, matMotorFrame);
        const mXCore = new THREE.Mesh(motorBlackGeom, matMotorCore);
        addOutlines(mXFrame, 0x94a3b8);
        mXCore.position.x = -0.1;
        motorX.add(mXFrame); motorX.add(mXCore);
        motorX.position.set(-8.5, 0, 0);
        differentialGroup.add(motorX);

        motorY = new THREE.Group();
        const mYFrame = new THREE.Mesh(motorGeom, matMotorFrame);
        const mYCore = new THREE.Mesh(motorBlackGeom, matMotorCore);
        addOutlines(mYFrame, 0x94a3b8);
        mYCore.position.x = 0.1;
        motorY.add(mYFrame); motorY.add(mYCore);
        motorY.position.set(8.5, 0, 0);
        differentialGroup.add(motorY);

        // Legs (approximation)
        const legGeom = new THREE.BoxGeometry(1, 6, 2);
        const legMat = new THREE.MeshPhongMaterial({ color: 0x475569 });
        const leg1 = new THREE.Mesh(legGeom, legMat); leg1.position.set(-8, -4, 2); leg1.rotation.z = Math.PI/8;
        const leg2 = new THREE.Mesh(legGeom, legMat); leg2.position.set(-8, -4, -2); leg2.rotation.z = Math.PI/8;
        const leg3 = new THREE.Mesh(legGeom, legMat); leg3.position.set(8, -4, 2); leg3.rotation.z = -Math.PI/8;
        const leg4 = new THREE.Mesh(legGeom, legMat); leg4.position.set(8, -4, -2); leg4.rotation.z = -Math.PI/8;
        [leg1, leg2, leg3, leg4].forEach(l => addOutlines(l, 0x64748b));
        differentialGroup.add(leg1, leg2, leg3, leg4);

        // 2. Large Input Bevel Gears
        const largeGearGeom = new THREE.CylinderGeometry(2, 3.5, 2, 24);
        gearX = new THREE.Mesh(largeGearGeom, matGearX);
        addOutlines(gearX, 0xffffff);
        gearX.rotation.z = -Math.PI / 2;
        gearX.position.set(-4.5, 0, 0);
        differentialGroup.add(gearX);

        gearY = new THREE.Mesh(largeGearGeom, matGearY);
        addOutlines(gearY, 0xffffff);
        gearY.rotation.z = Math.PI / 2;
        gearY.position.set(4.5, 0, 0);
        differentialGroup.add(gearY);

        // 3. Carrier Shafts & Hub
        const hShaft = new THREE.Mesh(new THREE.CylinderGeometry(0.5, 0.5, 12, 16), matShaft);
        hShaft.rotation.z = Math.PI / 2;
        addOutlines(hShaft, 0xffffff);
        differentialGroup.add(hShaft);

        const vShaft = new THREE.Mesh(new THREE.CylinderGeometry(0.6, 0.6, 7, 16), matShaft);
        addOutlines(vShaft, 0xffffff);
        carrierGroup.add(vShaft); // Orbits with carrier

        const crossHub = new THREE.Mesh(new THREE.BoxGeometry(2, 2, 2), matShaft);
        addOutlines(crossHub, 0xffffff);
        carrierGroup.add(crossHub);

        // 4. Spider Gears
        const smallGearGeom = new THREE.CylinderGeometry(1.2, 2.2, 1.5, 24);
        
        const topSpider = new THREE.Mesh(smallGearGeom, matSpider);
        addOutlines(topSpider, 0xffffff);
        topSpider.position.y = 2.5;
        yawGroup.add(topSpider); // Rotates with yaw
        
        bottomSpider = new THREE.Mesh(smallGearGeom, matSpider);
        addOutlines(bottomSpider, 0xffffff);
        bottomSpider.position.y = -2.5;
        bottomSpider.rotation.x = Math.PI; // Inverted
        carrierGroup.add(bottomSpider);

        // 5. Top Disk & Bracket
        const bracketGeom = new THREE.BoxGeometry(3, 1.5, 1.5);
        const bracket = new THREE.Mesh(bracketGeom, matBracket);
        addOutlines(bracket, 0xffffff);
        bracket.position.y = 4;
        yawGroup.add(bracket);

        const diskGeom = new THREE.CylinderGeometry(8, 8, 0.5, 32);
        disk = new THREE.Mesh(diskGeom, matDisk);
        addOutlines(disk, 0xffffff);
        disk.position.y = 5;
        yawGroup.add(disk);

        // Grid/Floor
        const grid = new THREE.GridHelper(50, 20, 0x334155, 0x1e293b);
        grid.position.y = -7;
        scene.add(grid);

        window.addEventListener('resize', onWindowResize);
        
        // Initial setup for the correct tab
        setTimeout(onWindowResize, 10);
    };

    const onWindowResize = () => {
        if (!renderer) return;
        
        const activeTab = document.querySelector('.tab-btn.active');
        const isTxTab = activeTab && activeTab.dataset.tab === 'imagesendermcu-tab';
        
        if (isTxTab) {
            // Render inside Mini Container
            const miniContainer = document.getElementById('mini-three-container');
            if (miniContainer && miniContainer.clientWidth > 0 && miniCamera) {
                if (renderer.domElement.parentElement !== miniContainer) {
                    miniContainer.appendChild(renderer.domElement);
                }
                const h = 180; // Fixed card height
                miniCamera.aspect = miniContainer.clientWidth / h;
                miniCamera.updateProjectionMatrix();
                renderer.setSize(miniContainer.clientWidth, h);
            }
        } else {
            // Render inside Main Container
            const container = document.getElementById('three-container');
            if (container && container.clientWidth > 0 && container.clientHeight > 0 && camera) {
                if (renderer.domElement.parentElement !== container) {
                    container.appendChild(renderer.domElement);
                }
                camera.aspect = container.clientWidth / container.clientHeight;
                camera.updateProjectionMatrix();
                renderer.setSize(container.clientWidth, container.clientHeight);
            }
        }
    };

    let gearAngleX = 0;
    let gearAngleY = 0;
    let targetAngleX = 0;
    let targetAngleY = 0;

    window.syncDriveSim = () => {
        if (!window.currentMotorState) return;
        targetAngleX = window.currentMotorState.mx_deg * Math.PI / 180.0;
        targetAngleY = window.currentMotorState.my_deg * Math.PI / 180.0;
        targetAngleZ = window.currentMotorState.mz_deg * Math.PI / 180.0;
        if (typeof updateHUD === 'function') updateHUD();
    };

    const updateHUD = () => {
        const rollEl = document.getElementById('hud-roll');
        const yawEl = document.getElementById('hud-yaw');
        const mxEl = document.getElementById('hud-mx');
        const myEl = document.getElementById('hud-my');
        if (rollEl) rollEl.textContent = predictedState.roll.toFixed(1);
        if (yawEl) yawEl.textContent = predictedState.yaw.toFixed(1);
        if (mxEl) mxEl.textContent = predictedState.mx.toFixed(1);
        if (myEl) myEl.textContent = predictedState.my.toFixed(1);
        
        const mzEl = document.getElementById('hud-mz');
        if (mzEl) mzEl.textContent = predictedState.mz.toFixed(1);
        const mzMmEl = document.getElementById('hud-mz-mm');
        if (mzMmEl) mzMmEl.textContent = predictedState.z_mm.toFixed(2);

        const rangeBar = document.getElementById('rail-range-bar');
        if (rangeBar) {
            // Map 145-175mm to 0-100%
            let percent = ((predictedState.z_mm - 145) / 30) * 100;
            percent = Math.max(0, Math.min(100, percent));
            rangeBar.style.width = percent + '%';
        }
        
        const tofEl = document.getElementById('hud-tof');
        const tofStatusEl = document.getElementById('hud-tof-status');
        if (window.currentMotorState && window.currentMotorState.tof_dist !== undefined) {
            if (tofEl) tofEl.textContent = window.currentMotorState.tof_dist;
            
            if (tofStatusEl && window.currentMotorState.tof_status) {
                const status = window.currentMotorState.tof_status;
                tofStatusEl.textContent = `(${status})`;
                
                // Colorize based on status
                if (status === 'OK') {
                    tofStatusEl.style.color = 'var(--success)';
                } else if (status === 'OUT_OF_RANGE') {
                    tofStatusEl.style.color = 'var(--text-dim)';
                } else {
                    tofStatusEl.style.color = 'var(--error)';
                }
            }
        }
    };

    const animate = () => {
        animationId = requestAnimationFrame(animate);
        const delta = clock.getDelta();
        
        // Lerp gear angles towards targets (smooth animation)
        gearAngleX = THREE.MathUtils.lerp(gearAngleX, targetAngleX, delta * 5);
        gearAngleY = THREE.MathUtils.lerp(gearAngleY, targetAngleY, delta * 5);
        
        // 미세 부동소수점 오차로 인한 그래프 깨짐 현상(1e-21 레벨) 방지용 스냅 로직 추가
        if (Math.abs(gearAngleX - targetAngleX) < 1e-4) gearAngleX = targetAngleX;
        if (Math.abs(gearAngleY - targetAngleY) < 1e-4) gearAngleY = targetAngleY;
        
        // Input gears rotate around X axis
        if (gearX) gearX.rotation.x = gearAngleX;
        if (gearY) gearY.rotation.x = gearAngleY;
        
        // Z axis (Rail) logic - smooth transition
        gearAngleZ = THREE.MathUtils.lerp(gearAngleZ, targetAngleZ, delta * 5);
        if (Math.abs(gearAngleZ - targetAngleZ) < 1e-4) gearAngleZ = targetAngleZ;

        // Mechanism Math
        // 작동 부호 동일(X+, Y+)일 때 전후진(Roll)
        const rollDelta = (gearAngleX + gearAngleY) / 2;
        if (carrierGroup) carrierGroup.rotation.x = rollDelta;

        // 작동 부호 반대(X+, Y-)일 때 제자리 회전(Yaw)
        const yawDelta = (gearAngleX - gearAngleY) / 2; 
        if (yawGroup) yawGroup.rotation.y = yawDelta;
        if (bottomSpider) bottomSpider.rotation.y = -yawDelta;

        // Predict State
        predictedState.mx = THREE.MathUtils.radToDeg(gearAngleX);
        predictedState.my = THREE.MathUtils.radToDeg(gearAngleY);
        predictedState.roll = THREE.MathUtils.radToDeg(rollDelta);
        predictedState.yaw = THREE.MathUtils.radToDeg(yawDelta);
        
        predictedState.mz = THREE.MathUtils.radToDeg(gearAngleZ);
        // Rail 이동 범위: 145mm ~ 175mm (30mm 가동 범위)
        // 1000 steps * 1.8 deg = 1800 deg.
        predictedState.z_mm = 145.0 + (predictedState.mz / 1800.0) * 30.0;

        updateHUD();
        
        // Throttle chart update to ~10Hz (once every 100ms)
        const now = performance.now();
        if (!this.lastChartUpdate || now - this.lastChartUpdate > 100) {
            updateChart(predictedState.roll, predictedState.yaw);
            this.lastChartUpdate = now;
        }
        
        if (renderer) {
            const activeTab = document.querySelector('.tab-btn.active');
            const isTxTab = activeTab && activeTab.dataset.tab === 'imagesendermcu-tab';
            
            if (isTxTab && miniCamera) {
                renderer.render(scene, miniCamera);
            } else if (camera) {
                renderer.render(scene, camera);
            }
        }
    };

    const runSimAction = (action) => {
        const steps = diskStepsInput ? parseInt(diskStepsInput.value) : 100;
        
        // --- Gear Ratio Logic ---
        const gearRatioX = parseFloat(document.getElementById('gear_ratio_x')?.value || 1.0);
        const gearRatioY = parseFloat(document.getElementById('gear_ratio_y')?.value || 1.0);

        let speedX = 0; let speedY = 0;
        switch(action) {
            case 'df': speedX = 1; speedY = -1; break;  // Forward Roll (실물 마주보는 모터 기준)
            case 'db': speedX = -1; speedY = 1; break;  // Backward Roll
            case 'dr': speedX = 1; speedY = 1; break;   // CW Yaw (회전)
            case 'dl': speedX = -1; speedY = -1; break; // CCW Yaw
        }

        // 사용자가 입력한 'steps'는 디스크(최종 출력) 기준 타겟 스텝으로 간주하고,
        // 드라이브에는 감속비(Gear Ratio)만큼 증폭된 실제 물리 모터 스텝을 송신합니다.
        const motorStepsX = Math.round(steps * speedX * gearRatioX);
        const motorStepsY = Math.round(steps * speedY * gearRatioY);

        let batchCmd = "";
        if (motorStepsX !== 0 || motorStepsY !== 0) {
            batchCmd = `b${motorStepsX},${motorStepsY}`;
        }
        
        if (batchCmd) sendMotorCmd(batchCmd);
        
        // 시뮬레이션 물리 (출력 기어 기준 절대 회전량)
        const rad = (steps / 100) * Math.PI;
        
        targetAngleX += speedX * rad;
        targetAngleY += speedY * rad;
    };

    const startSim = () => {
        if (!scene) initThreeJS();
        if (!isSimRunning) {
            isSimRunning = true;
            clock.start();
            if (!angleChart) initChart();
            animate();
        }
    };

    const stopSim = () => {
        isSimRunning = false;
        cancelAnimationFrame(animationId);
    };

    document.getElementById('disk-fw-btn')?.addEventListener('click', () => runSimAction('df'));
    document.getElementById('disk-bw-btn')?.addEventListener('click', () => runSimAction('db'));
    document.getElementById('disk-cw-btn')?.addEventListener('click', () => runSimAction('dr'));
    document.getElementById('disk-ccw-btn')?.addEventListener('click', () => runSimAction('dl'));

    // Rail PID Control Listeners
    document.getElementById('rail-pid-toggle')?.addEventListener('change', function() {
        const isChecked = this.checked;
        if (isChecked) {
            sendMotorCmd('mzm1');
            addLog('System', 'Rail PID Auto Mode: ENABLED');
        } else {
            sendMotorCmd('mzm0');
            addLog('System', 'Rail PID Auto Mode: DISABLED');
        }
    });

    document.getElementById('pid-target-slider')?.addEventListener('input', (e) => {
        const val = e.target.value;
        document.getElementById('pid-target-val').textContent = val;
        sendMotorCmd(`mzt${val}`);
    });

    document.getElementById('i2c-clock-speed')?.addEventListener('change', (e) => {
        sendMotorCmd(`mzc${e.target.value}`);
        addLog('System', `I2C Clock set to ${e.target.value} Hz`);
    });

    document.getElementById('tof-tx-interval')?.addEventListener('change', (e) => {
        sendMotorCmd(`mzh${e.target.value}`);
        addLog('System', `ToF Interval set to ${e.target.value} ms`);
    });

    ['pid-p-input', 'pid-i-input', 'pid-d-input'].forEach(id => {
        document.getElementById(id)?.addEventListener('change', (e) => {
            const val = e.target.value;
            const subCmd = id.includes('-p-') ? 'p' : id.includes('-i-') ? 'i' : 'd';
            sendMotorCmd(`mz${subCmd}${val}`);
            addLog('System', `PID ${subCmd.toUpperCase()} updated: ${val}`);
        });
    });

    // Rail Calibration Event Listeners
    document.getElementById('rail-auto-cal-btn')?.addEventListener('click', () => {
        if (confirm(currentLang === 'ko' ? '자동 캘리브레이션을 시작하시겠습니까? 모터가 1000스텝 이동합니다.' : 'Start auto calibration? Motor will move 1000 steps.')) {
            sendMotorCmd('mza');
            addLog('System', 'Z-Axis Auto Calibration (MZA) Request Sent.');
        }
    });

    document.getElementById('rail-steps-mm-apply-btn')?.addEventListener('click', () => {
        const val = document.getElementById('rail-steps-mm-input').value;
        sendMotorCmd(`mzs${val}`);
        addLog('System', `Z-Axis Steps/mm Manual Set: ${val}`);
    });

    document.getElementById('system-home-btn')?.addEventListener('click', () => {
        if (confirm(currentLang === 'ko' ? '시스템 전체를 원점으로 복귀시키겠습니까?' : 'Return entire system to home?')) {
            sendMotorCmd('home');
            addLog('System', 'System Home Request Sent.');
        }
    });

    document.getElementById('disk-home-btn')?.addEventListener('click', () => {
        const gearRatioX = parseFloat(document.getElementById('gear_ratio_x')?.value || 1.0);
        const gearRatioY = parseFloat(document.getElementById('gear_ratio_y')?.value || 1.0);

        // 출력 각도(targetAngle)를 원상복구(Homing)시키기 위한 모터의 역스텝 계산
        // Angle = (Steps / 100 * PI) / GearRatio -> Steps = (Angle * 100 * GearRatio) / PI
        const correctedHomeX = Math.round((-targetAngleX * 100 * gearRatioX) / Math.PI);
        const correctedHomeY = Math.round((-targetAngleY * 100 * gearRatioY) / Math.PI);

        let batchHome = "";
        if (correctedHomeX !== 0 || correctedHomeY !== 0) {
            batchHome = `b${correctedHomeX},${correctedHomeY}`;
        }
        
        if (batchHome) sendMotorCmd(batchHome);
        
        targetAngleX = 0;
        targetAngleY = 0;
        addLog('System', `Homing Sequence Corrected: X ${correctedHomeX} steps, Y ${correctedHomeY} steps (Ratios: ${gearRatioX}:${gearRatioY})`);
    });

    document.getElementById('sync-d-ip').addEventListener('click', () => {
        const disp = document.getElementById('drivemcu-ip-display');
        const detectedIp = disp.dataset.ip;
        if (detectedIp && detectedIp !== 'Unknown') {
            document.getElementById('drivemcu_ip').value = detectedIp;
            addLog('System', `DriveMCU IP auto-filled: ${detectedIp}`);
        } else {
            alert(currentLang === 'ko' ? '감지된 IP가 없습니다.' : 'No detected IP available.');
        }
    });

    document.getElementById('sync-r-ip-config')?.addEventListener('click', () => {
        const disp = document.getElementById('hmi_mcu-ip-display');
        const detectedIp = disp.dataset.ip;
        if (detectedIp && detectedIp !== 'Unknown') {
            document.getElementById('hmi_mcu_ip').value = detectedIp;
            addLog('System', `HMI MCU IP auto-filled: ${detectedIp}`);
        } else {
            alert(currentLang === 'ko' ? '감지된 IP가 없습니다.' : 'No detected IP available.');
        }
    });

    const gpsToggle = document.getElementById('gps-enable-toggle');
    const gpsStatusText = document.getElementById('gps-status-text');
    gpsToggle?.addEventListener('change', (e) => {
        if (gpsStatusText) {
            gpsStatusText.textContent = e.target.checked ? 'ON' : 'OFF';
            gpsStatusText.style.color = e.target.checked ? 'var(--primary)' : 'var(--text-dim)';
        }
    });

    document.getElementById('save-drive-config')?.addEventListener('click', async () => {
        const ip = document.getElementById('drivemcu_ip').value;
        const grX = document.getElementById('gear_ratio_x').value;
        const grY = document.getElementById('gear_ratio_y').value;
        const delay = document.getElementById('motor_delay').value;
        const gpsOn = document.getElementById('gps-enable-toggle')?.checked || false;

        // Sync local motor delay & GPS state immediately
        sendMotorCmd(`md${delay}`);
        sendMotorCmd(`gps${gpsOn ? '1' : '0'}`);

        try {
            const res = await fetch('/api/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ 
                    drivemcu_ip: ip,
                    gear_ratio_x: grX,
                    gear_ratio_y: grY,
                    gear_ratio_z: document.getElementById('gear_ratio_z')?.value || 1.0,
                    motor_delay: delay,
                    drivemcu_gps: gpsOn
                })
            });
            if (res.ok) {
                showSuccessCheck();
                addLog('System', `Config updated: IP ${ip}, Ratios X:${grX}, Y:${grY}, Delay: ${delay}ms, GPS: ${gpsOn?'ON':'OFF'}`);
            } else {
                addLog('System', `Failed to save DriveMCU config: ${res.statusText}`);
            }
        } catch (e) {
            addLog('System', `Error saving DriveMCU config: ${e.message}`);
        }
    });

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

    // ==========================================
    // Robot3AxisMCU Logic (3D Sim, Chart, Controls)
    // ==========================================
    window.currentRobot3AxisState = { x_deg: 0, y_mm: 0, z_mm: 0 };
    let robot3AxisChart = null;

    // --- Chart.js ---
    function initRobot3AxisChart() {
        const ctx = document.getElementById('robot3axis-chart');
        if (!ctx) return;
        robot3AxisChart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: Array(20).fill(''),
                datasets: [
                    { label: 'X Axis (°)', data: Array(20).fill(0), borderColor: '#f43f5e', borderWidth: 2, tension: 0.1 },
                    { label: 'Y Axis (mm)', data: Array(20).fill(0), borderColor: '#3b82f6', borderWidth: 2, tension: 0.1 },
                    { label: 'Z Axis (mm)', data: Array(20).fill(0), borderColor: '#10b981', borderWidth: 2, tension: 0.1 }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                animation: false,
                plugins: {
                    legend: { labels: { color: '#cbd5e1' } }
                },
                scales: {
                    x: { display: false },
                    y: { 
                        display: true, 
                        grid: { color: 'rgba(255,255,255,0.1)' },
                        ticks: { color: '#94a3b8' }
                    }
                }
            }
        });
    }

    // --- Three.js ---
    let r3Scene, r3Camera, r3Renderer;
    let r3Base, r3ArmY, r3ArmZ, r3Head;
    let r3ReqId;
    let targetR3 = { x: 0, y: 0, z: 0 };
    let visualR3 = { x: 0, y: 0, z: 0 };
    let r3TargetGroup, r3TargetStar, r3TargetGround, r3TargetLine;


    function initRobot3AxisSim() {
        const container = document.getElementById('robot3axis-three-container');
        if (!container || r3Scene) return;

        r3Scene = new THREE.Scene();
        r3Scene.background = new THREE.Color(0x0f172a);
        
        r3Camera = new THREE.PerspectiveCamera(45, container.clientWidth / container.clientHeight, 0.1, 1000);
        r3Camera.position.set(400, 300, 400);
        r3Camera.lookAt(0, 100, 0);

        r3Renderer = new THREE.WebGLRenderer({ antialias: true });
        r3Renderer.setSize(container.clientWidth, container.clientHeight);
        container.appendChild(r3Renderer.domElement);

        const ambientLight = new THREE.AmbientLight(0xffffff, 0.6);
        r3Scene.add(ambientLight);
        const dirLight = new THREE.DirectionalLight(0xffffff, 0.8);
        dirLight.position.set(100, 200, 50);
        r3Scene.add(dirLight);

        // Base
        const baseGeom = new THREE.CylinderGeometry(80, 80, 20, 32);
        const baseMat = new THREE.MeshStandardMaterial({ color: 0x334155, roughness: 0.7 });
        r3Base = new THREE.Mesh(baseGeom, baseMat);
        r3Scene.add(r3Base);

        // Y Arm (Vertical Pillar)
        const armYGeom = new THREE.BoxGeometry(10, 400, 10);
        armYGeom.translate(0, 200, 0); // Origin at bottom
        const armYMat = new THREE.MeshStandardMaterial({ color: 0x3b82f6 });
        r3ArmY = new THREE.Mesh(armYGeom, armYMat);
        r3ArmY.position.set(0, 10, 0); // On top of base
        r3Base.add(r3ArmY); // Rotates with base

        // Z Arm (Horizontal Boom, biased)
        const armZGeom = new THREE.BoxGeometry(400, 7, 7);
        armZGeom.translate(150, 0, 0); // Shift so origin is near the back end
        const armZMat = new THREE.MeshStandardMaterial({ color: 0x10b981 });
        r3ArmZ = new THREE.Mesh(armZGeom, armZMat);
        r3ArmZ.position.set(0, 40, 8); // Mounted on front face
        r3ArmY.add(r3ArmZ); // Moves vertically along Y arm

        // Head (Red Cube on top)
        const headGeom = new THREE.BoxGeometry(20, 20, 20);
        const headMat = new THREE.MeshStandardMaterial({ color: 0xff0000 }); // Bright red
        r3Head = new THREE.Mesh(headGeom, headMat);
        r3Head.position.set(0, 13.5, 0); // On top of the thinner Z arm (7/2 + 20/2 = 13.5)
        r3ArmZ.add(r3Head);

        // --- Target Marker Group (Star, Lines, Ground) ---
        r3TargetGroup = new THREE.Group();
        r3TargetGroup.visible = false;
        r3Scene.add(r3TargetGroup);

        // Flashing Star (Octahedron)
        const starGeom = new THREE.OctahedronGeometry(12, 0);
        const starMat = new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.8 });
        r3TargetStar = new THREE.Mesh(starGeom, starMat);
        r3TargetGroup.add(r3TargetStar);

        // Ground Marker (Circle)
        const groundMarkerGeom = new THREE.CircleGeometry(15, 32);
        const groundMarkerMat = new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.4, side: THREE.DoubleSide });
        r3TargetGround = new THREE.Mesh(groundMarkerGeom, groundMarkerMat);
        r3TargetGround.rotation.x = Math.PI / 2;
        r3TargetGroup.add(r3TargetGround);

        // Dashed Guideline (Vertical)
        const lineGeom = new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, 1, 0)]);
        const lineMat = new THREE.LineDashedMaterial({ color: 0xffffff, dashSize: 5, gapSize: 3, transparent: true, opacity: 0.5 });
        r3TargetLine = new THREE.Line(lineGeom, lineMat);
        r3TargetGroup.add(r3TargetLine);

        // Grid
        const grid = new THREE.GridHelper(500, 10, 0x475569, 0x1e293b);
        grid.position.y = -10;
        r3Scene.add(grid);

        initRobot3AxisChart();
        animateRobot3AxisSim();
    }

    let lastR3Time = performance.now();
    let velR3 = { x: 0, y: 0, z: 0 };

    function animateRobot3AxisSim() {
        r3ReqId = requestAnimationFrame(animateRobot3AxisSim);
        
        const now = performance.now();
        const dt = (now - lastR3Time) / 1000; // seconds
        lastR3Time = now;

        if (dt <= 0 || dt > 0.1) return; // Skip if tab was inactive

        // Get Speeds and Accel from UI (Steps/s)
        const speedX_steps = parseFloat(document.getElementById('r3_speed_x')?.value || 100);
        const speedY_steps = parseFloat(document.getElementById('r3_speed_y')?.value || 700);
        const speedZ_steps = parseFloat(document.getElementById('r3_speed_z')?.value || 1000);
        const accelX_steps = parseFloat(document.getElementById('r3_accel_x')?.value || 2000);

        // Convert to units/s
        const maxVX = speedX_steps * 0.5;      // deg/s
        const maxVY = speedY_steps * 0.043;    // mm/s
        const maxVZ = speedZ_steps * 0.22857;  // mm/s
        const accX = accelX_steps * 0.5;       // deg/s²

        // --- Y & Z Axis (Constant Speed, No Accel) ---
        ['y', 'z'].forEach(axis => {
            const target = targetR3[axis];
            const current = visualR3[axis];
            const maxV = (axis === 'y') ? maxVY : maxVZ;
            
            if (Math.abs(target - current) > 0.01) {
                const dir = Math.sign(target - current);
                const move = maxV * dt;
                if (move >= Math.abs(target - current)) {
                    visualR3[axis] = target;
                } else {
                    visualR3[axis] += dir * move;
                }
            }
        });

        // --- X Axis (With Acceleration) ---
        const targetX = targetR3.x;
        const currentX = visualR3.x;
        const distX = targetX - currentX;

        if (Math.abs(distX) > 0.01) {
            const dirX = Math.sign(distX);
            
            // Basic trapezoidal-ish movement (simplified)
            // If we are close to stopping, we should decelerate
            // Stopping distance = v^2 / (2 * a)
            const stopDist = (velR3.x * velR3.x) / (2 * accX);
            
            if (Math.abs(distX) <= stopDist) {
                // Decelerate
                velR3.x -= accX * dt;
            } else {
                // Accelerate
                velR3.x += accX * dt;
            }
            
            // Clamp velocity
            velR3.x = Math.min(Math.max(velR3.x, 0.1), maxVX);
            
            const moveX = velR3.x * dt;
            if (moveX >= Math.abs(distX)) {
                visualR3.x = targetX;
                velR3.x = 0;
            } else {
                visualR3.x += dirX * moveX;
            }
        } else {
            visualR3.x = targetX;
            velR3.x = 0;
        }

        // Pulse and rotate the target star
        if (r3TargetGroup && r3TargetGroup.visible) {
            const pulse = 1 + 0.15 * Math.sin(now * 0.008);
            r3TargetStar.scale.set(pulse, pulse, pulse);
            r3TargetStar.rotation.y += 0.05;
            r3TargetStar.rotation.x += 0.03;
            
            // Check if reached target
            const dx = Math.abs(targetR3.x - visualR3.x);
            const dy = Math.abs(targetR3.y - visualR3.y);
            const dz = Math.abs(targetR3.z - visualR3.z);
            if (dx < 0.1 && dy < 0.1 && dz < 0.1) {
                r3TargetGroup.visible = false;
            }
        }

        // Apply visual state to 3D Model
        if (r3Base && r3ArmZ && r3Head) {
            r3Base.rotation.y = THREE.MathUtils.degToRad(visualR3.x);
            r3ArmZ.position.y = visualR3.y + 40; 
            r3Head.position.x = visualR3.z;
        }

        if (r3Renderer && r3Scene && r3Camera) {
            r3Renderer.render(r3Scene, r3Camera);
        }
    }

    window.startRobot3AxisSim = function() {
        if (!r3Scene) initRobot3AxisSim();
        // Resize handle
        const container = document.getElementById('robot3axis-three-container');
        if (container && r3Renderer) {
            r3Camera.aspect = container.clientWidth / container.clientHeight;
            r3Camera.updateProjectionMatrix();
            r3Renderer.setSize(container.clientWidth, container.clientHeight);
        }
    };

    window.updateRobot3AxisHUD = function() {
        const state = window.currentRobot3AxisState;
        if (!state) return;

        // Update Target for Animation
        targetR3.x = state.x_deg;
        targetR3.y = state.y_mm;
        targetR3.z = state.z_mm;

        // Update Target Marker Position
        if (r3TargetGroup) {
            const baseAngle = THREE.MathUtils.degToRad(targetR3.x);
            const localX = targetR3.z; 
            const localZ = 8; // Z-arm mount offset
            
            // Apply 2D Rotation Matrix (around Y axis)
            // WorldX = localX * cos(angle) + localZ * sin(angle)
            // WorldZ = -localX * sin(angle) + localZ * cos(angle)
            const tx = localX * Math.cos(baseAngle) + localZ * Math.sin(baseAngle);
            const tz = -localX * Math.sin(baseAngle) + localZ * Math.cos(baseAngle);
            const ty = targetR3.y + 10 + 40 + 13.5 + 3.5; 
            
            r3TargetGroup.position.set(tx, ty, tz);
            r3TargetGround.position.set(0, -ty, 0); 
            
            // Update dashed line
            const points = [new THREE.Vector3(0, 0, 0), new THREE.Vector3(0, -ty, 0)];
            r3TargetLine.geometry.setFromPoints(points);
            r3TargetLine.computeLineDistances();
            
            r3TargetGroup.visible = true;
        }

        // --- NEW: Update Robot Head Overlay on Image ---
        const headOverlay = document.getElementById('robot-head-overlay');
        const headLineX = document.getElementById('head-line-x');
        const headLineY = document.getElementById('head-line-y');

        if (headOverlay) {
            // Map Robot Coordinates to Image Coordinates (approximate mapping)
            // Assuming Z (horizontal) -> X axis, Y (vertical) -> Y axis
            // Scaling factors based on a 400mm x 400mm workspace mapped to 100%
            const centerOffset = 50; // Center is 50%
            const scaleFactor = 0.15; // 1mm = 0.15% (Adjusted for visual clarity)

            const headX = centerOffset + (state.z_mm * scaleFactor);
            const headY = centerOffset - (state.y_mm * scaleFactor);

            headOverlay.style.display = 'block';
            headOverlay.style.left = headX + '%';
            headOverlay.style.top = headY + '%';

            if (headLineX) { headLineX.style.display = 'block'; headLineX.style.top = headY + '%'; }
            if (headLineY) { headLineY.style.display = 'block'; headLineY.style.left = headX + '%'; }
        }

        // Update HUD Values (Immediate)
        const hudX = document.getElementById('hud-r3-x');
        const hudY = document.getElementById('hud-r3-y');
        const hudZ = document.getElementById('hud-r3-z');
        if (hudX) hudX.textContent = state.x_deg.toFixed(2);
        if (hudY) hudY.textContent = state.y_mm.toFixed(2);
        if (hudZ) hudZ.textContent = state.z_mm.toFixed(2);
        
        // Sync Gripper Toggle
        const grabToggle = document.getElementById('robot3axis-grab-toggle');
        const grabStatus = document.getElementById('grab-status-text');
        if (grabToggle && state.grab_state) {
            const isTargetOpen = (state.grab_state === 'open');
            if (grabToggle.checked !== isTargetOpen) {
                grabToggle.checked = isTargetOpen;
                if (grabStatus) {
                    if (isTargetOpen) grabStatus.innerHTML = 'Status: <span style="color: #3b82f6; font-weight: bold;">OPENING / OPEN</span>';
                    else grabStatus.innerHTML = 'Status: <span style="color: #10b981; font-weight: bold;">CLOSED (DEFAULT)</span>';
                }
            }
        }

        // Update Chart
        if (robot3AxisChart) {
            robot3AxisChart.data.datasets[0].data.shift();
            robot3AxisChart.data.datasets[0].data.push(state.x_deg);
            robot3AxisChart.data.datasets[1].data.shift();
            robot3AxisChart.data.datasets[1].data.push(state.y_mm);
            robot3AxisChart.data.datasets[2].data.shift();
            robot3AxisChart.data.datasets[2].data.push(state.z_mm);
            robot3AxisChart.update();
        }
    };

    // --- Robot3Axis Execution Status Display ---
    let r3StatusTimer = null;
    let r3StatusInterval = null;
    function showR3Status(cmdName, estTime) {
        const ring = document.getElementById('robot3axis-execution-ring');
        const progress = document.getElementById('r3-ring-progress');
        const timeEl = document.getElementById('r3-ring-time');
        const cmdEl = document.getElementById('r3-ring-cmd');
        
        if (!ring || !progress || !timeEl || !cmdEl) return;
        
        // Use translated labels if available
        const labelExecuting = (translations[currentLang] && translations[currentLang]['executing_label']) || "EXECUTING:";
        const labelEstTime = (translations[currentLang] && translations[currentLang]['est_time_label']) || "EST. TIME:";

        cmdEl.textContent = cmdName;
        timeEl.textContent = estTime.toFixed(2);
        
        // Reset progress
        progress.style.transition = 'none';
        progress.style.strokeDashoffset = '283'; // Total perimeter (2 * PI * 45)
        ring.classList.remove('active');
        void ring.offsetHeight; // Trigger reflow
        
        ring.classList.add('active');
        
        // Start progress animation (animates from 283 to 0)
        progress.style.transition = `stroke-dashoffset ${estTime}s linear`;
        progress.style.strokeDashoffset = '0';
        
        // Countdown text
        let timeLeft = estTime;
        if (r3StatusInterval) clearInterval(r3StatusInterval);
        r3StatusInterval = setInterval(() => {
            timeLeft -= 0.1;
            if (timeLeft < 0) timeLeft = 0;
            timeEl.textContent = timeLeft.toFixed(2);
            if (timeLeft <= 0) clearInterval(r3StatusInterval);
        }, 100);
        
        if (r3StatusTimer) clearTimeout(r3StatusTimer);
        r3StatusTimer = setTimeout(() => {
            ring.classList.remove('active');
            clearInterval(r3StatusInterval);
        }, Math.max(estTime * 1000 + 800, 2500)); 
    }

    function calculateR3Time(sx, sy, sz) {
        const speedX = parseFloat(document.getElementById('r3_speed_x')?.value || 100);
        const speedY = parseFloat(document.getElementById('r3_speed_y')?.value || 700);
        const speedZ = parseFloat(document.getElementById('r3_speed_z')?.value || 1000);
        const accelX = parseFloat(document.getElementById('r3_accel_x')?.value || 2000);

        // Y & Z (Constant Speed)
        const tY = Math.abs(sy) / (speedY || 1);
        const tZ = Math.abs(sz) / (speedZ || 1);

        // X (With Acceleration)
        let tX = 0;
        const dist = Math.abs(sx);
        if (dist > 0) {
            const t_acc = speedX / (accelX || 1);
            const d_acc = 0.5 * accelX * t_acc * t_acc;
            if (dist > 2 * d_acc) {
                tX = 2 * t_acc + (dist - 2 * d_acc) / speedX;
            } else {
                const peakV = Math.sqrt(accelX * dist);
                tX = 2 * peakV / (accelX || 1);
            }
        }

        return Math.max(tX, tY, tZ);
    }

    // --- Robot3Axis Command Queue System ---
    let r3CommandQueue = [];
    let r3IsExecuting = false;

    function addR3Command(name, estTime, apiData) {
        const cmdId = Date.now() + Math.random();
        r3CommandQueue.push({ id: cmdId, name, estTime, apiData });
        updateR3QueueUI();
        if (!r3IsExecuting) {
            processNextR3Command();
        }
    }

    async function processNextR3Command() {
        if (r3CommandQueue.length === 0) {
            r3IsExecuting = false;
            return;
        }

        r3IsExecuting = true;
        const cmd = r3CommandQueue.shift();
        updateR3QueueUI();

        // Show execution UI
        showR3Status(cmd.name, cmd.estTime);

        // Send API call
        try {
            await fetch('/api/robot3axis/command', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(cmd.apiData)
            });
        } catch (e) {
            console.error('Failed to send Robot3Axis command', e);
        }

        // Wait for estimated time before next command
        setTimeout(() => {
            processNextR3Command();
        }, cmd.estTime * 1000);
    }

    function updateR3QueueUI() {
        const queueEl = document.getElementById('r3-command-queue');
        if (!queueEl) return;

        // Clear and rebuild (with simple animations handled by CSS)
        queueEl.innerHTML = '';
        
        r3CommandQueue.slice(0, 3).forEach((cmd, index) => {
            const item = document.createElement('div');
            item.className = 'queue-item';
            
            const labelNext = (translations[currentLang] && translations[currentLang]['next_up_label']) || "NEXT UP";
            
            item.innerHTML = `
                <div class="label">${labelNext} ${index + 1}</div>
                <div class="cmd-name">${cmd.name}</div>
            `;
            queueEl.appendChild(item);
        });

        if (r3CommandQueue.length > 3) {
            const more = document.createElement('div');
            more.style = "font-size: 0.6rem; color: var(--text-dim); text-align: center; margin-top: 5px;";
            more.textContent = `+ ${r3CommandQueue.length - 3} more...`;
            queueEl.appendChild(more);
        }
    }

    async function sendRobot3AxisCmd(cmd, steps = {x:0, y:0, z:0}, name = "MOVE") {
        const estTime = calculateR3Time(steps.x, steps.y, steps.z);
        if (window.addLog) window.addLog('System', `Robot3Axis Command Queued: ${name} (${cmd})`);
        addR3Command(name, estTime, { command: cmd });
    }

    document.getElementById('robot3axis-x-cw')?.addEventListener('click', () => {
        const steps = parseInt(document.getElementById('robot3axis-motor-steps-x').value);
        sendRobot3AxisCmd(`rx${steps}`, {x: steps, y: 0, z: 0}, "X CW Move");
    });
    document.getElementById('robot3axis-x-ccw')?.addEventListener('click', () => {
        const steps = parseInt(document.getElementById('robot3axis-motor-steps-x').value);
        sendRobot3AxisCmd(`rx-${steps}`, {x: -steps, y: 0, z: 0}, "X CCW Move");
    });

    document.getElementById('robot3axis-y-up')?.addEventListener('click', () => {
        const steps = parseInt(document.getElementById('robot3axis-motor-steps-y').value);
        sendRobot3AxisCmd(`ry-${steps}`, {x: 0, y: -steps, z: 0}, "Y UP Move");
    });
    document.getElementById('robot3axis-y-down')?.addEventListener('click', () => {
        const steps = parseInt(document.getElementById('robot3axis-motor-steps-y').value);
        sendRobot3AxisCmd(`ry${steps}`, {x: 0, y: steps, z: 0}, "Y DOWN Move");
    });

    document.getElementById('robot3axis-z-fw')?.addEventListener('click', () => {
        const steps = parseInt(document.getElementById('robot3axis-motor-steps-z').value);
        sendRobot3AxisCmd(`rz${steps}`, {x: 0, y: 0, z: steps}, "Z FW Move");
    });
    document.getElementById('robot3axis-z-bw')?.addEventListener('click', () => {
        const steps = parseInt(document.getElementById('robot3axis-motor-steps-z').value);
        sendRobot3AxisCmd(`rz-${steps}`, {x: 0, y: 0, z: -steps}, "Z BW Move");
    });

    document.getElementById('robot3axis-home')?.addEventListener('click', () => {
        addR3Command("HOMING...", 3.0, { command: 'home' });
    });
    document.getElementById('robot3axis-stop')?.addEventListener('click', () => {
        // Stop is urgent, clear queue and send immediately
        r3CommandQueue = [];
        updateR3QueueUI();
        showR3Status("EMERGENCY STOP", 0.1);
        fetch('/api/robot3axis/command', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ command: 'stop' })
        });
    });

    // PTP Move Listener
    document.getElementById('robot3axis-ptp-move')?.addEventListener('click', async () => {
        const tx = parseFloat(document.getElementById('ptp-x').value);
        const ty = parseFloat(document.getElementById('ptp-y').value);
        const tz = parseFloat(document.getElementById('ptp-z').value);
        
        const target_x_steps = parseInt(tx * 2.0);
        const target_y_steps = parseInt(ty * 1000.0 / -43.0);
        const target_z_steps = parseInt(tz * 1400.0 / -320.0);
        
        const currentState = window.currentRobot3AxisState || {x_steps: 0, y_steps: 0, z_steps: 0};
        const sx = target_x_steps - (currentState.x_steps || 0);
        const sy = target_y_steps - (currentState.y_steps || 0);
        const sz = target_z_steps - (currentState.z_steps || 0);
        
        const estTime = calculateR3Time(sx, sy, sz);
        if (window.addLog) window.addLog('System', `Robot3Axis PTP Move Queued: Target(${tx}, ${ty}, ${tz})`);
        addR3Command("PTP MOVE", estTime, { 
            x: tx, 
            y: ty, 
            z: tz, 
            absolute: true 
        });
    });

    document.getElementById('robot3axis-ptp-home-convenience')?.addEventListener('click', () => {
        showR3Status("HOMING...", 3.0);
        sendRobot3AxisCmd('home');
    });

    // --- Config Settings ---
    document.getElementById('sync-r3-ip')?.addEventListener('click', () => {
        const disp = document.getElementById('robot3axismcu-ip-display');
        const detectedIp = disp ? disp.dataset.ip : null;
        if (detectedIp && detectedIp !== 'Unknown') {
            const input = document.getElementById('robot3axismcu_ip');
            if (input) input.value = detectedIp;
            addLog('System', `Robot3AxisMCU IP auto-filled: ${detectedIp}`);
        } else {
            alert(currentLang === 'ko' ? '감지된 IP가 없습니다.' : 'No detected IP available.');
        }
    });

    document.getElementById('save-r3-config')?.addEventListener('click', () => {
        const sx = document.getElementById('r3_speed_x')?.value || 100;
        const ax = document.getElementById('r3_accel_x')?.value || 2000;
        const sy = document.getElementById('r3_speed_y')?.value || 700;
        const ay = document.getElementById('r3_accel_y')?.value || 2000;
        const sz = document.getElementById('r3_speed_z')?.value || 1000;
        const az = document.getElementById('r3_accel_z')?.value || 2000;

        // Send sequentially with slight delay to avoid TCP buffer overflow
        sendRobot3AxisCmd(`sx${sx}`);
        setTimeout(() => sendRobot3AxisCmd(`ax${ax}`), 50);
        setTimeout(() => sendRobot3AxisCmd(`sy${sy}`), 100);
        setTimeout(() => sendRobot3AxisCmd(`ay${ay}`), 150);
        setTimeout(() => sendRobot3AxisCmd(`sz${sz}`), 200);
        setTimeout(() => sendRobot3AxisCmd(`az${az}`), 250);

        if (window.addLog) {
            window.addLog('System', `Robot3Axis Config Saved - Speed: ${sx}/${sy}/${sz}, Accel: ${ax}/${ay}/${az}`);
        }
        showSuccessCheck();
    });

    // --- Sequence Control Implementation ---
    let sequenceSteps = [];
    let isSequenceRunning = false;
    let currentStepIndex = 0;
    let sequenceTimer = null;

    const sequenceListEl = document.getElementById('sequence-list');
    const emptySeqMsg = document.getElementById('empty-seq-msg');
    const statusBadge = document.getElementById('sequence-status-badge');

    function updateSequenceUI() {
        if (!sequenceListEl) return;
        
        // Clear all except empty message
        const rows = sequenceListEl.querySelectorAll('tr:not(#empty-seq-msg)');
        rows.forEach(r => r.remove());

        if (sequenceSteps.length === 0) {
            if (emptySeqMsg) emptySeqMsg.style.display = 'table-row';
            return;
        }

        if (emptySeqMsg) emptySeqMsg.style.display = 'none';

        sequenceSteps.forEach((step, idx) => {
            const tr = document.createElement('tr');
            tr.id = `seq-step-${idx}`;
            tr.style.borderBottom = '1px solid rgba(255,255,255,0.05)';
            tr.style.transition = 'background 0.3s ease';
            if (isSequenceRunning && currentStepIndex === idx) {
                tr.style.background = 'rgba(16, 185, 129, 0.2)';
                tr.style.borderLeft = '3px solid var(--success)';
            }

            tr.innerHTML = `
                <td style="padding: 10px; text-align: center; color: var(--text-dim);">${idx + 1}</td>
                <td style="padding: 10px; font-weight: 600;">X:${step.x} Y:${step.y} Z:${step.z}</td>
                <td style="padding: 10px; text-align: center;">${step.wait.toFixed(1)}s</td>
                <td style="padding: 10px; text-align: center; color: var(--primary); font-weight: bold;">${step.duration.toFixed(1)}s</td>
                <td style="padding: 10px; text-align: center;">
                    <button class="delete-step" data-index="${idx}" style="background: none; border: none; color: var(--danger); cursor: pointer; padding: 4px; transition: transform 0.2s;">
                        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="3 6 5 6 21 6"></polyline><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"></path></svg>
                    </button>
                </td>
            `;
            sequenceListEl.appendChild(tr);
        });

        // Add delete listeners
        document.querySelectorAll('.delete-step').forEach(btn => {
            btn.addEventListener('click', (e) => {
                if (isSequenceRunning) return;
                const idx = parseInt(e.currentTarget.dataset.index);
                sequenceSteps.splice(idx, 1);
                updateSequenceUI();
            });
        });
    }

    document.getElementById('add-seq-step')?.addEventListener('click', () => {
        const x = parseFloat(document.getElementById('seq-in-x').value);
        const y = parseFloat(document.getElementById('seq-in-y').value);
        const z = parseFloat(document.getElementById('seq-in-z').value);
        const wait = parseFloat(document.getElementById('seq-in-wait').value);

        // Calculate relative steps for duration estimate
        const lastStep = sequenceSteps.length > 0 ? sequenceSteps[sequenceSteps.length - 1] : (window.currentRobot3AxisState ? {x: window.currentRobot3AxisState.x_deg, y: window.currentRobot3AxisState.y_mm, z: window.currentRobot3AxisState.z_mm} : {x: 0, y: 0, z: 0});
        
        const target_x_steps = parseInt(x * 2.0);
        const target_y_steps = parseInt(y * 1000.0 / -43.0);
        const target_z_steps = parseInt(z * 1400.0 / -320.0);
        
        const last_x_steps = parseInt(lastStep.x * 2.0);
        const last_y_steps = parseInt(lastStep.y * 1000.0 / -43.0);
        const last_z_steps = parseInt(lastStep.z * 1400.0 / -320.0);

        const duration = calculateR3Time(target_x_steps - last_x_steps, target_y_steps - last_y_steps, target_z_steps - last_z_steps);

        sequenceSteps.push({ x, y, z, wait, duration });
        updateSequenceUI();
    });

    document.getElementById('clear-sequence-btn')?.addEventListener('click', () => {
        if (isSequenceRunning) return;
        sequenceSteps = [];
        updateSequenceUI();
    });

    document.getElementById('run-sequence-btn')?.addEventListener('click', async () => {
        if (isSequenceRunning) {
            stopSequence();
            return;
        }

        if (sequenceSteps.length === 0) {
            alert(currentLang === 'ko' ? '시퀀스에 단계가 없습니다.' : 'No steps in sequence.');
            return;
        }

        isSequenceRunning = true;
        currentStepIndex = 0;
        statusBadge.textContent = "RUNNING";
        statusBadge.style.color = "var(--success)";
        statusBadge.style.background = "rgba(74, 222, 128, 0.1)";
        document.getElementById('run-sequence-btn').textContent = "⏹ STOP SEQUENCE";
        
        executeNextStep();
    });

    function stopSequence() {
        isSequenceRunning = false;
        if (sequenceTimer) clearTimeout(sequenceTimer);
        statusBadge.textContent = "READY";
        statusBadge.style.color = "var(--text-dim)";
        statusBadge.style.background = "rgba(255,255,255,0.05)";
        document.getElementById('run-sequence-btn').textContent = "▶ RUN FULL SEQUENCE";
        updateSequenceUI();
    }

    async function executeNextStep() {
        if (!isSequenceRunning || currentStepIndex >= sequenceSteps.length) {
            isSequenceRunning = false;
            statusBadge.textContent = "FINISHED";
            statusBadge.style.color = "var(--primary)";
            document.getElementById('run-sequence-btn').textContent = "▶ RUN FULL SEQUENCE";
            updateSequenceUI();
            return;
        }

        updateSequenceUI();
        const step = sequenceSteps[currentStepIndex];
        
        // PTP Move Logic
        const target_x_steps = parseInt(step.x * 2.0);
        const target_y_steps = parseInt(step.y * 1000.0 / -43.0);
        const target_z_steps = parseInt(step.z * 1400.0 / -320.0);
        
        const currentState = window.currentRobot3AxisState || {x_steps: 0, y_steps: 0, z_steps: 0};
        const sx = target_x_steps - (currentState.x_steps || 0);
        const sy = target_y_steps - (currentState.y_steps || 0);
        const sz = target_z_steps - (currentState.z_steps || 0);
        
        const estTime = calculateR3Time(sx, sy, sz);
        
        // Send command through the existing queue system
        addR3Command(`SEQ STEP ${currentStepIndex + 1}`, estTime, { 
            x: step.x, 
            y: step.y, 
            z: step.z, 
            absolute: true 
        });

        // Wait for Move Duration + User defined Wait time
        const totalWaitMs = (estTime + step.wait) * 1000;
        
        sequenceTimer = setTimeout(() => {
            if (isSequenceRunning) {
                currentStepIndex++;
                executeNextStep();
            }
        }, totalWaitMs);
    }

    // --- Utility: Success Checkmark Animation ---
    function showSuccessCheck() {
        const overlay = document.getElementById('success-checkmark');
        if (!overlay) return;
        
        overlay.classList.add('active');
        setTimeout(() => {
            overlay.classList.remove('active');
        }, 1500);
    }

    // --- Global Preset Applier ---
    window.applyPreset = function(x, y, z) {
        const inputX = document.getElementById('ptp-x');
        const inputY = document.getElementById('ptp-y');
        const inputZ = document.getElementById('ptp-z');
        const moveBtn = document.getElementById('robot3axis-ptp-move');

        if (inputX) inputX.value = x;
        if (inputY) inputY.value = y;
        if (inputZ) inputZ.value = z;

        if (moveBtn) {
            // Trigger visual feedback on inputs
            [inputX, inputY, inputZ].forEach(el => {
                if (el) {
                    el.style.background = 'rgba(16, 185, 129, 0.1)';
                    setTimeout(() => el.style.background = '', 500);
                }
            });
            // Execute move
            moveBtn.click();
            addLog('System', `Preset applied: (${x}, ${y}, ${z})`);
        }
    };
    // --- Gripper Control Logic ---
    document.getElementById('robot3axis-grab-toggle')?.addEventListener('change', (e) => {
        const isOpening = e.target.checked;
        const statusText = document.getElementById('grab-status-text');
        
        if (isOpening) {
            sendRobot3AxisCmd('grab open', {x: 0, y: 0, z: 0}, "Gripper Opening");
            if (statusText) statusText.innerHTML = 'Status: <span style="color: #3b82f6; font-weight: bold;">OPENING / OPEN</span>';
        } else {
            sendRobot3AxisCmd('grab close', {x: 0, y: 0, z: 0}, "Gripper Closing");
            if (statusText) statusText.innerHTML = 'Status: <span style="color: #10b981; font-weight: bold;">CLOSED (DEFAULT)</span>';
        }
    });

    let selectedMusicFile = null;

    window.refreshMusicList = async function() {
        const container = document.getElementById('music-list-container');
        if (!container) return;
        
        try {
            const response = await fetch('/api/music/list');
            const data = await response.json();
            if (data.status === 'success') {
                container.innerHTML = '';
                const files = data.files || [];
                if (files.length === 0) {
                    container.innerHTML = '<div style="color: var(--text-dim); font-size: 0.8rem; text-align: center; padding-top: 200px;">보관함이 비어 있습니다. (MusicData 폴더)</div>';
                    return;
                }
                
                files.forEach(file => {
                    const item = document.createElement('div');
                    item.className = 'music-item';
                    item.style.padding = '12px 16px';
                    item.style.margin = '6px 0';
                    item.style.borderRadius = '8px';
                    item.style.cursor = 'pointer';
                    item.style.fontSize = '0.85rem';
                    item.style.color = '#fff';
                    item.style.background = 'rgba(255,255,255,0.03)';
                    item.style.border = '1px solid rgba(255,255,255,0.06)';
                    item.style.transition = 'all 0.2s cubic-bezier(0.4, 0, 0.2, 1)';
                    item.style.whiteSpace = 'nowrap';
                    item.style.overflow = 'hidden';
                    item.style.textOverflow = 'ellipsis';
                    item.style.display = 'flex';
                    item.style.alignItems = 'center';
                    item.style.gap = '10px';
                    
                    item.innerHTML = `<span class="music-icon" style="font-size: 0.95rem; color: #94a3b8; transition: color 0.2s;">🎵</span> <span style="flex: 1; overflow: hidden; text-overflow: ellipsis;">${file}</span>`;
                    
                    item.addEventListener('mouseenter', () => {
                        if (selectedMusicFile !== file) {
                            item.style.background = 'rgba(255,255,255,0.06)';
                            item.style.border = '1px solid rgba(255,255,255,0.12)';
                            item.style.transform = 'translateX(4px)';
                            item.querySelector('.music-icon').style.color = 'var(--success)';
                        }
                    });
                    
                    item.addEventListener('mouseleave', () => {
                        if (selectedMusicFile !== file) {
                            item.style.background = 'rgba(255,255,255,0.03)';
                            item.style.border = '1px solid rgba(255,255,255,0.06)';
                            item.style.transform = 'none';
                            item.querySelector('.music-icon').style.color = '#94a3b8';
                        }
                    });
                    
                    item.addEventListener('click', () => {
                        selectedMusicFile = file;
                        
                        // 즉시 핫 스위칭 재생 전환 전격 가동!
                        window.playSelectedMusic();
                        
                        // Reset all sibling styles
                        const items = container.querySelectorAll('.music-item');
                        items.forEach(el => {
                            el.style.background = 'rgba(255,255,255,0.03)';
                            el.style.border = '1px solid rgba(255,255,255,0.06)';
                            el.style.transform = 'none';
                            const icon = el.querySelector('.music-icon');
                            if (icon) icon.style.color = '#94a3b8';
                        });
                        // Apply active style with premium emerald glow!
                        item.style.background = 'rgba(16, 185, 129, 0.16)';
                        item.style.border = '1px solid var(--success)';
                        item.style.boxShadow = '0 0 12px rgba(16,185,129,0.25)';
                        item.querySelector('.music-icon').style.color = 'var(--success)';
                    });
                    
                    container.appendChild(item);
                });
            }
        } catch (e) {
            console.error('Failed to load music files', e);
        }
    };

    let isSeeking = false; // To prevent seek bar updates while dragging!

    // Helper to format MM:SS
    function formatTime(sec) {
        if (isNaN(sec) || sec === null || sec === undefined) return "00:00";
        const m = Math.floor(sec / 60);
        const s = Math.floor(sec % 60);
        return `${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`;
    }

    // Update Equalizer value labels on input
    window.updateEQValues = function() {
        const bassVal = parseFloat(document.getElementById('eq-bass').value);
        const midVal = parseFloat(document.getElementById('eq-mid').value);
        const trebleVal = parseFloat(document.getElementById('eq-treble').value);
        
        document.getElementById('eq-bass-val').innerText = (bassVal >= 0 ? '+' : '') + bassVal.toFixed(1) + 'dB';
        document.getElementById('eq-mid-val').innerText = (midVal >= 0 ? '+' : '') + midVal.toFixed(1) + 'dB';
        document.getElementById('eq-treble-val').innerText = (trebleVal >= 0 ? '+' : '') + trebleVal.toFixed(1) + 'dB';
    };

    // Send EQ configuration to backend
    window.sendEQConfig = async function() {
        const bassVal = parseFloat(document.getElementById('eq-bass').value);
        const midVal = parseFloat(document.getElementById('eq-mid').value);
        const trebleVal = parseFloat(document.getElementById('eq-treble').value);
        
        try {
            await fetch('/api/music/eq', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ bass: bassVal, mid: midVal, treble: trebleVal })
            });
        } catch (e) {
            console.error('Failed to update EQ', e);
        }
    };

    // Apply EQ presets
    window.applyEQPreset = function(preset) {
        const bassSlider = document.getElementById('eq-bass');
        const midSlider = document.getElementById('eq-mid');
        const trebleSlider = document.getElementById('eq-treble');
        
        if (preset === 'flat') {
            bassSlider.value = 0;
            midSlider.value = 0;
            trebleSlider.value = 0;
        } else if (preset === 'bass') {
            bassSlider.value = 8;
            midSlider.value = 0;
            trebleSlider.value = 2;
        } else if (preset === 'vocal') {
            bassSlider.value = -2;
            midSlider.value = 6;
            trebleSlider.value = 2;
        }
        
        window.updateEQValues();
        window.sendEQConfig();
    };

    // Periodic Music Status Tracker
    async function trackMusicStatus() {
        try {
            const response = await fetch('/api/music/status');
            const data = await response.json();
            if (data.status === 'success' && data.status_data) {
                const s = data.status_data;
                
                // Update track info
                const titleEl = document.getElementById('music-current-title');
                const statusEl = document.getElementById('music-current-status');
                const cdArt = document.getElementById('music-cd-art');
                const playBtn = document.getElementById('music-play-btn');
                
                if (s.is_loaded) {
                    titleEl.innerText = s.filename;
                    if (s.is_playing) {
                        statusEl.innerText = "Playing...";
                        if (playBtn) playBtn.innerText = "⏸";
                        if (cdArt) {
                            cdArt.classList.remove('music-art-stopped');
                            cdArt.classList.add('music-art-playing');
                        }
                    } else {
                        statusEl.innerText = "Paused";
                        if (playBtn) playBtn.innerText = "▶";
                        if (cdArt) {
                            cdArt.classList.remove('music-art-playing');
                            cdArt.classList.add('music-art-stopped');
                        }
                    }
                    
                    // Time updates
                    const elapsedEl = document.getElementById('music-time-elapsed');
                    const totalEl = document.getElementById('music-time-total');
                    if (elapsedEl) elapsedEl.innerText = formatTime(s.position);
                    if (totalEl) totalEl.innerText = formatTime(s.duration);
                    
                    // Progress bar seek slider update (only if user is not actively dragging it!)
                    const seekBar = document.getElementById('music-seek-bar');
                    if (seekBar && !isSeeking) {
                        seekBar.value = s.progress * 100;
                    }
                } else {
                    if (titleEl) titleEl.innerText = "No Song Loaded";
                    if (statusEl) statusEl.innerText = "Select a song to start playing";
                    if (playBtn) playBtn.innerText = "▶";
                    const elapsedEl = document.getElementById('music-time-elapsed');
                    const totalEl = document.getElementById('music-time-total');
                    if (elapsedEl) elapsedEl.innerText = "00:00";
                    if (totalEl) totalEl.innerText = "00:00";
                    const seekBar = document.getElementById('music-seek-bar');
                    if (seekBar && !isSeeking) seekBar.value = 0;
                    if (cdArt) {
                        cdArt.classList.remove('music-art-playing');
                        cdArt.classList.add('music-art-stopped');
                    }
                }
            }
        } catch (e) {
            console.error('Failed to fetch music status', e);
        }
    }

    // Toggle Play / Pause
    window.toggleMusicPlay = async function() {
        // If nothing is loaded, try playing selected list file
        try {
            const statusResponse = await fetch('/api/music/status');
            const statusData = await statusResponse.json();
            const s = statusData.status_data;
            
            if (!s.is_loaded) {
                if (!selectedMusicFile) {
                    alert('목록에서 재생할 음악 파일을 먼저 선택해 주세요!');
                    return;
                }
                window.playSelectedMusic();
                return;
            }
            
            // If loaded, toggle play/pause
            const endpoint = s.is_playing ? '/api/music/pause' : '/api/music/play';
            const bodyData = s.is_playing ? {} : { filename: selectedMusicFile || s.filename };
            const response = await fetch(endpoint, {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(bodyData)
            });
            const data = await response.json();
            if (data.status === 'success') {
                trackMusicStatus();
            }
        } catch (e) {
            console.error('Failed to toggle play state', e);
        }
    };

    window.playSelectedMusic = async function() {
        if (!selectedMusicFile) {
            alert('재생할 음악 파일을 목록에서 먼저 선택해 주세요!');
            return;
        }
        
        try {
            if (window.addLog) window.addLog('Sound', `음악 재생 준비 중: ${selectedMusicFile}`);
            const response = await fetch('/api/music/play', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ filename: selectedMusicFile })
            });
            const data = await response.json();
            if (data.status === 'success') {
                if (window.addLog) window.addLog('Sound', `음악 재생 시작: ${selectedMusicFile}`);
                trackMusicStatus();
            } else {
                alert('음악 재생 실패: ' + data.message);
            }
        } catch (e) {
            console.error('Failed to play music', e);
        }
    };

    // Stop music playback
    window.stopMusicPlayer = async function() {
        try {
            const response = await fetch('/api/music/stop', { method: 'POST' });
            const data = await response.json();
            if (data.status === 'success') {
                if (window.addLog) window.addLog('Sound', '음악 재생이 정지되었습니다.');
                trackMusicStatus();
            }
        } catch (e) {
            console.error('Failed to stop music', e);
        }
    };

    // Drag Seek jump
    window.seekMusicPlayer = async function(val) {
        const progress = parseFloat(val) / 100;
        try {
            const response = await fetch('/api/music/seek', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ progress: progress })
            });
            const data = await response.json();
            if (data.status === 'success') {
                trackMusicStatus();
            }
        } catch (e) {
            console.error('Failed to seek music', e);
        }
    };

    // Bind seek bar drag locks
    document.addEventListener('DOMContentLoaded', () => {
        const seekBar = document.getElementById('music-seek-bar');
        if (seekBar) {
            seekBar.addEventListener('mousedown', () => { isSeeking = true; });
            seekBar.addEventListener('mouseup', () => { isSeeking = false; });
            seekBar.addEventListener('touchstart', () => { isSeeking = true; });
            seekBar.addEventListener('touchend', () => { isSeeking = false; });
        }
    });

    // Start periodic tracking
    setInterval(trackMusicStatus, 500);

    // Radio Station List & Status Manager
    let isRadioListLoaded = false;

    window.loadRadioStatusAndList = async function() {
        try {
            const response = await fetch('/api/radio/status');
            const data = await response.json();
            if (data.status === 'success') {
                const stations = data.stations || [];
                const select = document.getElementById('radio-station-select');
                
                // Populate select box if not done already
                if (select && !isRadioListLoaded && stations.length > 0) {
                    select.innerHTML = '';
                    stations.forEach((station, idx) => {
                        const opt = document.createElement('option');
                        opt.value = idx;
                        opt.innerText = station.name;
                        select.appendChild(opt);
                    });
                    isRadioListLoaded = true;
                }
                
                // Update UI status
                const s = data.status_data;
                const statusText = document.getElementById('radio-status-text');
                const pulseIcon = document.getElementById('radio-pulse-icon');
                
                if (s && s.is_playing) {
                    if (statusText) statusText.innerText = `Playing: ${s.station_name}`;
                    if (pulseIcon) {
                        pulseIcon.style.background = '#0ea5e9';
                        pulseIcon.style.boxShadow = '0 0 12px #0ea5e9, 0 0 0 6px rgba(14, 165, 233, 0.2)';
                        pulseIcon.style.animation = 'pulse-cyan 1.5s infinite';
                    }
                } else {
                    if (statusText) statusText.innerText = "Not Streaming";
                    if (pulseIcon) {
                        pulseIcon.style.background = '#94a3b8';
                        pulseIcon.style.boxShadow = 'none';
                        pulseIcon.style.animation = 'none';
                    }
                }
            }
        } catch (e) {
            console.error('Failed to load radio status/list', e);
        }
    };

    window.playSelectedRadio = async function() {
        const select = document.getElementById('radio-station-select');
        if (!select) return;
        
        const stationIdx = parseInt(select.value);
        const stationName = select.options[select.selectedIndex]?.text || "Selected Station";
        
        try {
            if (window.addLog) window.addLog('Sound', `라디오 채널 스트리밍 요청: ${stationName}`);
            const response = await fetch('/api/radio/play', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ station_idx: stationIdx })
            });
            const data = await response.json();
            if (data.status === 'success') {
                if (window.addLog) window.addLog('Sound', `라디오 스트리밍 연결 완료: ${stationName}`);
                window.loadRadioStatusAndList();
            } else {
                alert('라디오 재생 실패: ' + data.message);
            }
        } catch (e) {
            console.error('Failed to play radio', e);
        }
    };

    window.playCustomRadio = async function() {
        const urlInput = document.getElementById('radio-custom-url');
        const nameInput = document.getElementById('radio-custom-name');
        if (!urlInput || !nameInput) return;
        
        const customUrl = urlInput.value.trim();
        const customName = nameInput.value.trim() || "Custom Radio Channel";
        
        if (!customUrl) {
            alert('라디오 스트림 URL(HTTP/HTTPS)을 입력해 주세요!');
            return;
        }
        
        try {
            if (window.addLog) window.addLog('Sound', `커스텀 라디오 연결 요청: ${customName}`);
            const response = await fetch('/api/radio/play', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ custom_url: customUrl, custom_name: customName })
            });
            const data = await response.json();
            if (data.status === 'success') {
                if (window.addLog) window.addLog('Sound', `커스텀 라디오 연결 완료: ${customName}`);
                window.loadRadioStatusAndList();
            } else {
                alert('커스텀 라디오 재생 실패: ' + data.message);
            }
        } catch (e) {
            console.error('Failed to play custom radio', e);
        }
    };

    window.stopRadioStreaming = async function() {
        try {
            const response = await fetch('/api/radio/stop', { method: 'POST' });
            const data = await response.json();
            if (data.status === 'success') {
                if (window.addLog) window.addLog('Sound', '라디오 스트리밍이 즉시 정지되었습니다.');
                window.loadRadioStatusAndList();
            }
        } catch (e) {
            console.error('Failed to stop radio streaming', e);
        }
    };

    // Start periodic tracking
    setInterval(loadRadioStatusAndList, 1000);
    setTimeout(loadRadioStatusAndList, 500);

    window.playYoutubeAudio = async function() {
        const input = document.getElementById('youtube-url-input');
        const btn = document.getElementById('youtube-play-btn');
        if (!input || !btn) return;
        
        const url = input.value.trim();
        if (!url) {
            alert('유튜브 영상 주소(URL)를 입력해 주세요!');
            return;
        }
        
        const originalText = btn.innerHTML;
        try {
            // UI 피드백 설정 (비활성화 및 텍스트 전환)
            btn.disabled = true;
            btn.style.background = '#64748b';
            btn.innerHTML = '⏳ DOWNLOADING AUDIO...';
            
            if (window.addLog) window.addLog('Sound', `유튜브 오디오 추출 요청 중: ${url}`);
            
            const response = await fetch('/api/youtube/play', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ url: url })
            });
            
            const data = await response.json();
            if (data.status === 'success') {
                if (window.addLog) window.addLog('Sound', `유튜브 오디오 연주 시작`);
            } else {
                alert('유튜브 오디오 스트리밍 실패: ' + data.message);
            }
        } catch (e) {
            console.error('Failed to play YouTube audio', e);
            alert('유튜브 처리 중 통신 오류가 발생했습니다.');
        } finally {
            // UI 복구
            btn.disabled = false;
            btn.style.background = '#ff0000';
            btn.innerHTML = originalText;
        }
    };

    window.switchSubTab = function(subTabId) {
        // 모든 서브탭 버튼들 클래스 리셋
        document.querySelectorAll('.subtab-btn').forEach(btn => {
            btn.classList.remove('active-subtab');
        });
        
        // 클릭한 서브탭 버튼 활성화
        const activeBtn = document.getElementById('subtab-btn-' + subTabId);
        if (activeBtn) activeBtn.classList.add('active-subtab');
        
        // 모든 서브탭 콘텐츠 숨김
        document.querySelectorAll('.subtab-content').forEach(content => {
            content.style.display = 'none';
        });
        
        // 선택한 서브탭 콘텐츠만 노출
        const activeContent = document.getElementById('subtab-content-' + subTabId);
        if (activeContent) {
            if (subTabId === 'chat') {
                activeContent.style.display = 'flex'; // 채팅방은 flex 레이아웃 유지!
            } else {
                activeContent.style.display = 'block';
            }
        }
        
        // 탭이 바뀔 때 스크롤 포커스 갱신 (채팅의 경우 하단으로!)
        if (subTabId === 'chat') {
            const chatContainer = document.getElementById('chat-display-main');
            if (chatContainer) chatContainer.scrollTop = chatContainer.scrollHeight;
        } else if (subTabId === 'music') {
            // 음악 리스트 새로고침
            window.refreshMusicList();
        } else if (subTabId === 'radio') {
            // 라디오 목록 및 상태 새로고침
            window.loadRadioStatusAndList();
        } else if (subTabId === 'mirror') {
            // PC 사운드 가로채기 상태 새로고침
            window.updateAudioMirrorStatus();
        }
    };

    window.updateAudioMirrorStatus = async function() {
        try {
            const res = await fetch('/api/audio/mirror/status');
            const data = await res.json();
            if (data.status === 'success') {
                const isPlaying = data.status_data.is_playing;
                const container = document.getElementById('mirror-wave-container');
                const btn = document.getElementById('mirror-toggle-btn');
                const icon = document.getElementById('mirror-icon-display');
                
                if (isPlaying) {
                    if (container) {
                        container.style.background = 'rgba(0, 242, 254, 0.12)';
                        container.style.border = '2px solid rgba(0, 242, 254, 0.6)';
                        container.style.boxShadow = '0 0 25px rgba(0, 242, 254, 0.35)';
                        container.style.transform = 'scale(1.05)';
                    }
                    if (icon) {
                        icon.textContent = '🌊';
                        icon.style.animation = 'spin 4s linear infinite';
                    }
                    if (btn) {
                        btn.style.background = '#ef4444';
                        btn.style.boxShadow = '0 4px 12px rgba(239, 68, 68, 0.2)';
                        btn.innerHTML = '⏹ STOP AUDIO MIRRORING';
                    }
                } else {
                    if (container) {
                        container.style.background = 'rgba(255, 255, 255, 0.02)';
                        container.style.border = '1px solid rgba(255, 255, 255, 0.1)';
                        container.style.boxShadow = 'none';
                        container.style.transform = 'scale(1.0)';
                    }
                    if (icon) {
                        icon.textContent = '🎙️';
                        icon.style.animation = 'none';
                    }
                    if (btn) {
                        btn.style.background = 'var(--primary)';
                        btn.style.boxShadow = '0 4px 12px rgba(0, 242, 254, 0.2)';
                        btn.innerHTML = '▶ START AUDIO MIRRORING';
                    }
                }
            }
        } catch (e) {
            console.error('Failed to update audio mirror status', e);
        }
    };

    window.toggleAudioMirror = async function() {
        const btn = document.getElementById('mirror-toggle-btn');
        if (!btn) return;
        const originalText = btn.innerHTML;
        btn.disabled = true;
        btn.innerHTML = '⏳ PROCESSING...';
        
        try {
            const resStatus = await fetch('/api/audio/mirror/status');
            const dataStatus = await resStatus.json();
            const isPlaying = dataStatus.status_data.is_playing;
            
            const endpoint = isPlaying ? '/api/audio/mirror/stop' : '/api/audio/mirror/start';
            const res = await fetch(endpoint, { method: 'POST' });
            const data = await res.json();
            
            if (data.status === 'success') {
                if (window.addLog) window.addLog('Sound', isPlaying ? 'PC 사운드 가로채기 중단됨' : 'PC 사운드 가로채기 시작됨');
            } else {
                alert('에러: ' + data.message);
            }
        } catch (e) {
            console.error('Failed to toggle audio mirror', e);
            alert('미러링 제어 통신 실패');
        } finally {
            btn.disabled = false;
            await window.updateAudioMirrorStatus();
        }
    };

    window.syncGlobalVolume = function(val) {
        // 글로벌 표시값 갱신
        const globalVolSpan = document.getElementById('global-media-volume-val');
        if (globalVolSpan) globalVolSpan.textContent = val;
        
        // 기존 우측 HMI 볼륨 슬라이더 값 동기화
        const volumeSlider = document.getElementById('mcu-volume');
        if (volumeSlider) {
            volumeSlider.value = val;
            const volValSpan = document.getElementById('mcu-volume-val');
            if (volValSpan) volValSpan.textContent = val;
        }
        
        // 설정 전송 및 저장
        if (typeof window.saveSoundMCUConfig === 'function') {
            window.saveSoundMCUConfig();
        }
    };

    // Auto-refresh music list on startup
    setTimeout(refreshMusicList, 800);
    setTimeout(window.updateAudioMirrorStatus, 1500);

});
