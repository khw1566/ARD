// ==========================================
// ARD Control Center Core Module (main.js)
// ==========================================

// Global State
window.currentLang = localStorage.getItem('ard_lang') || 'ko';
window.currentMotorState = { mx_deg: 0, my_deg: 0, mz_deg: 0, tof_dist: 0, tof_status: 'UNKNOWN' };
window.currentRobot3AxisState = { x_deg: 0, y_mm: 0, z_mm: 0, grab: 0, queue: [], active_cmd: null, execution_percent: 0, execution_elapsed: 0 };

document.addEventListener('DOMContentLoaded', () => {
    // Inject VAD Voice indicator styles dynamically
    const style = document.createElement('style');
    style.innerHTML = `
        .voice-indicator {
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 4px;
            width: 50px;
            height: 24px;
            background: rgba(16, 185, 129, 0.15);
            border: 1px solid rgba(16, 185, 129, 0.3);
            border-radius: 12px;
            padding: 0 8px;
        }
        .voice-bar {
            width: 3px;
            height: 10px;
            background: var(--success);
            border-radius: 1.5px;
            animation: bounce-wave 0.8s ease-in-out infinite alternate;
        }
        .voice-bar:nth-child(2) { animation-delay: 0.2s; height: 14px; }
        .voice-bar:nth-child(3) { animation-delay: 0.4s; height: 8px; }
        @keyframes bounce-wave {
            0% { transform: scaleY(0.5); }
            100% { transform: scaleY(1.5); }
        }
        .pulse-dot {
            width: 6px;
            height: 6px;
            background: var(--text);
            border-radius: 50%;
            display: inline-block;
            animation: pulse-dot-anim 1.2s infinite ease-in-out;
        }
        @keyframes pulse-dot-anim {
            0%, 100% { transform: scale(0.6); opacity: 0.4; }
            50% { transform: scale(1.2); opacity: 1; }
        }
        .pulse-mic {
            animation: pulse-red 1.2s infinite alternate;
        }
        @keyframes pulse-red {
            0% { box-shadow: 0 0 4px rgba(239, 68, 68, 0.3); }
            100% { box-shadow: 0 0 16px rgba(239, 68, 68, 0.7), 0 0 0 6px rgba(239, 68, 68, 0.2); }
        }
    `;
    document.head.appendChild(style);

    // Initial language apply
    updateLanguage(window.currentLang);

    // Boot Splash handling
    handleStartup();

    // Setup global listeners
    setupGlobalControls();

    // Setup SSE Status Event Stream
    setupSSEListener();
});

// --- Startup Splash Handler ---
function handleStartup() {
    const splash = document.getElementById('startup-overlay');
    if (!splash) return;

    let pollInterval = setInterval(async () => {
        try {
            const res = await fetch('/api/startup_status');
            const data = await res.json();
            
            const progressBar = document.getElementById('startup-progress');
            const statusText = document.getElementById('startup-status-text');
            const descText = document.getElementById('startup-desc-text');
            
            if (data && data.progress !== undefined) {
                if (progressBar) progressBar.style.width = `${data.progress}%`;
                if (statusText) statusText.textContent = data.text || 'Initializing...';
                if (descText) {
                    descText.textContent = data.id === 'ready' 
                        ? 'All startup sequence complete. Control Center active.' 
                        : `Step [${data.id.toUpperCase()}] is executing...`;
                }
                
                if (data.id === 'ready' || data.progress >= 100) {
                    clearInterval(pollInterval);
                    setTimeout(() => {
                        splash.style.opacity = '0';
                        splash.style.pointerEvents = 'none';
                        addLog('System', 'All startup sequence complete. Control Center active.');
                    }, 1000);
                }
            }
        } catch (e) {
            console.error('Error fetching startup status', e);
        }
    }, 400);
}

// --- Global UI Action setups ---
function setupGlobalControls() {
    // Language buttons
    document.getElementById('lang-ko-btn')?.addEventListener('click', () => updateLanguage('ko'));
    document.getElementById('lang-en-btn')?.addEventListener('click', () => updateLanguage('en'));

    // Server shutdown
    document.getElementById('server-shutdown-btn')?.addEventListener('click', async () => {
        const msg = window.currentLang === 'ko' ? '정말로 PC 센터 제어 서버를 종료하시겠습니까?' : 'Are you sure you want to SHUTDOWN the server?';
        if (confirm(msg)) {
            addLog('System', 'Server shutdown initiated by user.');
            try {
                await fetch('/api/shutdown', { method: 'POST' });
                alert(window.currentLang === 'ko' ? '서버가 종료되었습니다. 브라우저 창을 닫으셔도 됩니다.' : 'Server has shut down. You can now close this tab.');
            } catch (e) {
                console.error(e);
            }
        }
    });

    // Logging buttons
    document.getElementById('clear-log-btn')?.addEventListener('click', () => {
        const list = document.getElementById('log-list');
        if (list) list.innerHTML = '';
    });

    // Config form submit
    document.getElementById('config-form')?.addEventListener('submit', async (e) => {
        e.preventDefault();
        const transmitter_ip = document.getElementById('transmitter_ip')?.value || '';
        const receiver_ip = document.getElementById('receiver_ip')?.value || '';
        const model_name = document.getElementById('gemini-model')?.value || 'gemini-2.5-flash';

        try {
            const res = await fetch('/api/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    transmitter_ip,
                    receiver_ip,
                    model_name
                })
            });
            if (res.ok) {
                showSuccessCheck();
                addLog('System', `Global config saved: HMI(${receiver_ip}), Cam(${transmitter_ip}), AI(${model_name})`);
            }
        } catch (err) {
            addLog('Error', 'Failed to save config: ' + err.message);
        }
    });

    // Pricing Modal
    const pricingModal = document.getElementById('pricing-modal');
    document.getElementById('open-pricing-modal')?.addEventListener('click', () => {
        if (pricingModal) pricingModal.style.display = 'block';
    });
    document.getElementById('close-modal')?.addEventListener('click', () => {
        if (pricingModal) pricingModal.style.display = 'none';
    });

    // Global volume synchronization slider (next to header indicator)
    document.getElementById('global-media-volume')?.addEventListener('input', (e) => {
        if (window.syncGlobalVolume) window.syncGlobalVolume(e.target.value);
    });

    // Active default tab switch
    switchTab('dashboard-tab');
}

// --- Success UI Overlay checkmark ---
function showSuccessCheck() {
    const overlay = document.getElementById('success-checkmark');
    if (!overlay) return;
    overlay.classList.add('active');
    setTimeout(() => {
        overlay.classList.remove('active');
    }, 1500);
}
window.showSuccessCheck = showSuccessCheck;

// --- Custom Tab Switching Engine ---
function switchTab(tabId) {
    // Deactivate all
    document.querySelectorAll('.tab-btn').forEach(btn => btn.classList.remove('active'));
    document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));

    // Find tab elements
    const targetBtn = document.querySelector(`.tab-btn[data-tab="${tabId}"]`);
    const targetContent = document.getElementById(tabId);

    if (targetBtn && targetContent) {
        targetBtn.classList.add('active');
        targetContent.classList.add('active');
    }

    // Left column panels to toggle
    const simView = document.getElementById('simulation-view');
    const robotView = document.getElementById('robot3axis-main-view');
    const imgContainer = document.getElementById('image-container');
    const voiceContainer = document.getElementById('voice-media-container');

    // Default: Reset all to none
    if (simView) simView.style.display = 'none';
    if (robotView) robotView.style.display = 'none';
    if (imgContainer) imgContainer.style.display = 'none';
    if (voiceContainer) voiceContainer.style.display = 'none';

    // Route displays by selected Tab
    if (tabId === 'drivemcu-tab') {
        if (simView) simView.style.display = 'flex';
        if (window.startSim) window.startSim();
        if (window.stopRobot3AxisSim) window.stopRobot3AxisSim();
    } else if (tabId === 'robot3axismcu-tab') {
        if (robotView) robotView.style.display = 'flex';
        if (window.startRobot3AxisSim) window.startRobot3AxisSim();
        if (window.stopSim) window.stopSim();
    } else if (tabId === 'imagesendermcu-tab') {
        if (imgContainer) imgContainer.style.display = 'block';
        if (window.stopSim) window.stopSim();
        if (window.stopRobot3AxisSim) window.stopRobot3AxisSim();
    } else if (tabId === 'soundmcu-tab') {
        if (voiceContainer) voiceContainer.style.display = 'flex';
        // Auto-switch to chat room on first sound tab click
        if (window.switchSubTab) window.switchSubTab('chat');
        if (window.stopSim) window.stopSim();
        if (window.stopRobot3AxisSim) window.stopRobot3AxisSim();
    } else {
        // ControlMCU, main dashboard, or custom tabs: show both Image view & Media panel simultaneously!
        if (imgContainer) imgContainer.style.display = 'block';
        if (voiceContainer) voiceContainer.style.display = 'flex';
        if (window.switchSubTab) window.switchSubTab('chat');
        if (window.stopSim) window.stopSim();
        if (window.stopRobot3AxisSim) window.stopRobot3AxisSim();
    }

    // Load AI Persona configurations on startup/selection of sound tab
    if (tabId === 'controlmcu-tab' && window.loadPersonaFile) {
        window.loadPersonaFile();
        if (window.updateSoundMCUUI) window.updateSoundMCUUI();
    }

    // Trigger Three.js render resize to adjust layout aspect ratio
    window.dispatchEvent(new Event('resize'));
}
window.switchTab = switchTab;

// Bind data-tab buttons click
document.querySelectorAll('.tab-btn').forEach(btn => {
    btn.addEventListener('click', (e) => {
        const tabId = e.currentTarget.dataset.tab;
        switchTab(tabId);
    });
});

// --- System Logger ---
function addLog(source, message, timestamp = null) {
    const logList = document.getElementById('log-list');
    if (!logList) return;

    const time = timestamp || new Date().toLocaleTimeString();
    const tr = document.createElement('tr');
    tr.style.cssText = 'border-bottom: 1px solid rgba(255,255,255,0.02); transition: background 0.2s;';
    tr.innerHTML = `
        <td style="padding: 6px 12px; color: var(--text-dim); font-size: 0.75rem; font-family: monospace; white-space: nowrap;">${time}</td>
        <td style="padding: 6px 12px; font-weight: bold; font-size: 0.75rem; white-space: nowrap;">
            <span style="background: ${source === 'System' ? 'rgba(56, 189, 248, 0.15)' : source === 'AI' ? 'rgba(168, 85, 247, 0.15)' : 'rgba(16, 185, 129, 0.15)'};
                         color: ${source === 'System' ? '#38bdf8' : source === 'AI' ? '#c084fc' : '#34d399'};
                         padding: 2px 6px; border-radius: 4px; border: 1px solid rgba(255,255,255,0.05);">${source}</span>
        </td>
        <td style="padding: 6px 12px; font-size: 0.8rem; word-break: break-all; color: #e2e8f0;">${message}</td>
    `;

    tr.addEventListener('mouseover', () => { tr.style.background = 'rgba(255,255,255,0.02)'; });
    tr.addEventListener('mouseout', () => { tr.style.background = 'transparent'; });

    logList.insertBefore(tr, logList.firstChild);

    // Keep log buffer up to 100 items
    if (logList.children.length > 100) {
        logList.removeChild(logList.lastChild);
    }
}
window.addLog = addLog;

// --- i18n Localization Engine ---
function updateLanguage(lang) {
    window.currentLang = lang;
    localStorage.setItem('ard_lang', lang);

    document.querySelectorAll('.lang-btn').forEach(btn => btn.classList.remove('active'));
    document.getElementById(`lang-${lang}-btn`)?.classList.add('active');

    // Safe lookups inside global translations.js
    let dict = {};
    if (window.translations && window.translations[lang]) {
        dict = window.translations[lang];
    } else if (typeof translations !== 'undefined' && translations[lang]) {
        dict = translations[lang];
    } else {
        return;
    }

    // Apply translations on items having data-i18n attribute
    document.querySelectorAll('[data-i18n]').forEach(el => {
        const key = el.dataset.i18n;
        if (dict[key]) {
            if (el.tagName === 'INPUT' && (el.type === 'button' || el.type === 'submit')) {
                el.value = dict[key];
            } else {
                el.textContent = dict[key];
            }
        }
    });

    // Apply placeholders
    document.querySelectorAll('[data-i18n-placeholder]').forEach(el => {
        const key = el.dataset.i18nPlaceholder;
        if (dict[key]) el.placeholder = dict[key];
    });

    // Custom non-standard translation items
    const warnMsg = document.getElementById('cam-warning-msg');
    if (warnMsg && window.dispatchEvent) {
        // Trigger model constraints in camera module to force warning string re-translation
        const camSelect = document.getElementById('camera_model');
        if (camSelect) camSelect.dispatchEvent(new Event('change'));
    }
}
window.updateLanguage = updateLanguage;

// --- SSE Event Stream Status Synchronizer ---
function setupSSEListener() {
    const sse = new EventSource('/api/status/stream');
    let sseSyncTimer = null;

    sse.onmessage = (event) => {
        const syncInd = document.getElementById('sync-indicator');
        if (syncInd) {
            syncInd.style.opacity = '1';
            clearTimeout(sseSyncTimer);
            sseSyncTimer = setTimeout(() => {
                syncInd.style.opacity = '0';
            }, 800);
        }

        try {
            const data = JSON.parse(event.data);
            if (data.type === 'status') {
                const stats = data.status || data.data;
                if (stats) {
                    // Update Drive MCU state cache
                    if (stats.drivemcu) {
                        window.currentMotorState = window.currentMotorState || {};
                        window.currentMotorState.tof_dist = stats.drivemcu.tof_dist;
                        window.currentMotorState.tof_status = stats.drivemcu.tof_status;

                        // Environment sensor updates
                        if (stats.drivemcu.env_temp !== undefined) {
                            const tEl = document.getElementById('env-temp');
                            const hEl = document.getElementById('env-humid');
                            const pEl = document.getElementById('env-press');
                            if (tEl) tEl.textContent = stats.drivemcu.env_temp.toFixed(1);
                            if (hEl) hEl.textContent = stats.drivemcu.env_humid.toFixed(1);
                            if (pEl) pEl.textContent = Math.round(stats.drivemcu.env_press);
                        }
                    }

                    // Feed online/offline statuses
                    Object.keys(stats).forEach(dev => {
                        updateDeviceUI(dev, stats[dev]);

                        // Camera transmitter auto synchronization values
                        if ((dev === 'transmitter' || dev === 'imagesendermcu') && stats[dev].config) {
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
                            if (stats[dev].model_name) {
                                const modelEl = document.getElementById('gemini-model');
                                if (modelEl) modelEl.value = stats[dev].model_name;
                            }
                        } 
                        // Robotic arm MCU state feed
                        else if (dev === 'robot3axismcu' && stats[dev].state_data) {
                            window.currentRobot3AxisState = stats[dev].state_data;
                            if (window.updateRobot3AxisHUD) {
                                window.updateRobot3AxisHUD();
                            }
                        } 
                        // Control MCU joystick & mode mappings
                        else if (dev === 'controlmcu') {
                            const cData = stats[dev];
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
            } 
            // Voice Activity Detection indicator
            else if (data.type === 'user_speaking') {
                const indicator = document.getElementById('user-speaking-indicator');
                if (indicator) indicator.style.display = data.speaking ? 'flex' : 'none';

                if (data.speaking) {
                    const mainChat = document.getElementById('chat-display-main');
                    if (mainChat && !document.getElementById('voice-loading-bubble')) {
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
            } 
            // Standard / Voice response update
            else if (data.type === 'update') {
                if (data.analysis && data.analysis.type === 'voice') {
                    const voiceLoading = document.getElementById('voice-loading-bubble');
                    if (voiceLoading) voiceLoading.remove();

                    addChatMessage(data.analysis.role, data.analysis.text, data.timings, data.turn_id);

                    // PC Voice output autoplay back if enabled
                    const audioUrl = data.analysis.audio_url || data.audio_url;
                    const pcAudioToggle = document.getElementById('pc-audio-output-toggle');
                    if (audioUrl && pcAudioToggle?.checked && data.analysis.role === 'AI') {
                        const audio = new Audio(audioUrl);
                        audio.play().catch(e => console.error("PC voice playback failed:", e));
                    }
                } else if (data.turn_id && data.timings) {
                    // Timing updates mapping to existing turn bubbles
                    let retries = 0;
                    const maxRetries = 30;
                    const updateTimingWithRetry = () => {
                        const msgId = `msg-${data.turn_id}-ai`;
                        if (document.getElementById(msgId)) {
                            addChatMessage('AI', null, data.timings, data.turn_id);
                        } else if (retries < maxRetries) {
                            retries++;
                            setTimeout(updateTimingWithRetry, 100);
                        }
                    };
                    updateTimingWithRetry();
                } else {
                    // Visual object analysis image frame update
                    updateUI(data);
                }
            } 
            // Logs feed
            else if (data.type === 'log') {
                addLog(data.source, data.message, data.timestamp);
            } 
            // Robot 3Axis position telemetry updates
            else if (data.type === 'robot3axis_update') {
                window.currentRobot3AxisState = data.state;
                if (window.updateRobot3AxisHUD) window.updateRobot3AxisHUD();

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
            } 
            // Drive motor telemetry & configurations updates
            else if (data.type === 'drive_update') {
                window.currentMotorState = data.state;
                if (window.syncDriveSim) window.syncDriveSim();

                if (data.settings) {
                    const s = data.settings;
                    const i2cSelect = document.getElementById('i2c-clock-speed');
                    if (i2cSelect && s.i2c_clock_hz !== undefined) {
                        i2cSelect.value = String(s.i2c_clock_hz);
                    }
                    const txIntervalEl = document.getElementById('tof-tx-interval');
                    if (txIntervalEl && s.hb_interval_ms !== undefined) {
                        txIntervalEl.value = s.hb_interval_ms;
                    }
                    const pidToggle = document.getElementById('rail-pid-toggle');
                    if (pidToggle && s.pid_mode !== undefined && pidToggle.checked !== s.pid_mode) {
                        pidToggle.checked = s.pid_mode;
                        pidToggle.dispatchEvent(new Event('change', { bubbles: true }));
                    }
                    if (s.pid_target_tof !== undefined) {
                        const pidSlider = document.getElementById('pid-target-slider');
                        const pidVal = document.getElementById('pid-target-val');
                        if (pidSlider) pidSlider.value = s.pid_target_tof;
                        if (pidVal) pidVal.textContent = s.pid_target_tof;
                    }
                }
            }
        } catch (err) {
            console.error("SSE parse error", err);
        }
    };

    sse.onerror = () => {
        addLog('System', 'SSE connection lost. Reconnecting in background...');
    };
}

// --- Device connection panel layout updator ---
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
            const dict = window.translations?.[window.currentLang] || translations?.[window.currentLang];
            if (dict) onlineText = isOnline ? dict['online'] : dict['offline'];
        } catch (e) {}
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

    // IP, Temperature, RSSI updates
    const ipDisplay = document.getElementById(`${device}-ip-display`);
    const rssiText = document.getElementById(`${device}-rssi`);
    const tempText = document.getElementById(`${device}-temp`);
    const sIpDisp = document.getElementById('server-ip-display');
    const costInfo = document.getElementById('cost-info');

    if (sIpDisp && data.server_ip) sIpDisp.textContent = data.server_ip;
    if (rssiText) rssiText.textContent = isOnline ? `RSSI: ${data.rssi}dBm` : 'RSSI: -dBm';

    if (tempText) {
        let tempLabel = "Temp";
        try {
            const dict = window.translations?.[window.currentLang] || translations?.[window.currentLang];
            if (dict) tempLabel = dict['temp_label'];
        } catch (e) {}
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

    // Signal bars & Temperature bar fills
    const signalContainer = document.getElementById(`${device}-signal-visual`);
    const tempFill = document.getElementById(`${device}-temp-fill`);
    if (signalContainer && tempFill) {
        const bars = signalContainer.querySelectorAll('.sig-bar');
        if (!isOnline) {
            bars.forEach(b => b.className = 'sig-bar');
            tempFill.style.width = '0%';
            tempFill.className = 'temp-fill';
        } else {
            let activeBars = 0;
            let sigClass = 'active';
            if (data.rssi > -45) activeBars = 4;
            else if (data.rssi > -65) activeBars = 3;
            else if (data.rssi > -80) { activeBars = 2; sigClass = 'sig-mid active'; }
            else { activeBars = 1; sigClass = 'sig-weak active'; }

            bars.forEach((b, i) => {
                b.className = i < activeBars ? `sig-bar ${sigClass}` : 'sig-bar';
            });

            const tempPercent = Math.min(Math.max(data.temp, 0), 100);
            tempFill.style.width = tempPercent + '%';
            if (data.temp > 50) tempFill.className = 'temp-fill temp-hot';
            else if (data.temp > 35) tempFill.className = 'temp-fill temp-warm';
            else tempFill.className = 'temp-fill';
        }
    }

    // Gemini API Token Cost pricing
    if (costInfo && data.total_cost !== undefined) {
        const krw = data.total_cost_krw.toLocaleString();
        const usd = data.total_cost.toFixed(4);
        costInfo.textContent = `${krw}원 ($${usd})`;
    }

    // GPS tracking maps sync
    if (device === 'drivemcu') {
        const gpsDisp = document.getElementById('gps-coords-display');
        const mapBtn = document.getElementById('gps-map-btn');
        if (gpsDisp && mapBtn) {
            if (isOnline && data.gps_lat !== undefined && data.gps_lat !== 0.0) {
                gpsDisp.textContent = `GPS: ${data.gps_lat.toFixed(5)}, ${data.gps_lng.toFixed(5)}`;
                mapBtn.style.display = 'inline-block';
                mapBtn.href = `https://www.google.com/maps/search/?api=1&query=${data.gps_lat},${data.gps_lng}`;
            } else {
                gpsDisp.textContent = window.currentLang === 'ko' ? 'GPS: 신호 대기 중...' : 'Waiting for GPS...';
                mapBtn.style.display = 'none';
                mapBtn.href = '#';
            }
        }
    }
}

// --- Image overlay drawing for Gemini analysis ---
function updateUI(data) {
    const imgElement = document.getElementById('latest-image');
    const overlay = document.getElementById('coordinate-overlay');
    const noImage = document.getElementById('no-image');

    if (data.image_b64) {
        imgElement.src = `data:image/jpeg;base64,${data.image_b64}`;

        const hudMiniImg = document.getElementById('hud-mini-image');
        const hudPlaceholder = document.getElementById('hud-mini-placeholder');
        if (hudMiniImg) {
            hudMiniImg.src = imgElement.src;
            hudMiniImg.style.display = 'block';
            if (hudPlaceholder) hudPlaceholder.style.display = 'none';
        }

        imgElement.style.display = 'block';
        if (noImage) noImage.style.display = 'none';
    }

    if (data.analysis && data.analysis.x !== undefined && data.analysis.object) {
        if (data.analysis.object === 'Analyzing...') return;

        if (overlay) {
            const px = data.analysis.x * 100;
            const py = data.analysis.y * 100;
            overlay.style.display = 'block';
            overlay.style.left = px + '%';
            overlay.style.top = py + '%';

            const lx = document.getElementById('target-line-x');
            const ly = document.getElementById('target-line-y');
            if (lx) { lx.style.display = 'block'; lx.style.top = py + '%'; }
            if (ly) { ly.style.display = 'block'; ly.style.left = px + '%'; }
        }

        const objName = document.getElementById('object-name');
        const objType = document.getElementById('object-type');
        const objCoords = document.getElementById('object-coords');
        const processTime = document.getElementById('process-time');
        if (objName) objName.textContent = data.analysis.object || 'Unknown';
        if (objType) objType.textContent = data.analysis.type || '-';
        if (objCoords) objCoords.textContent = `X: ${data.analysis.x.toFixed(2)}, Y: ${data.analysis.y.toFixed(2)}`;
        if (processTime) processTime.textContent = data.metadata ? data.metadata.timestamp : new Date().toLocaleTimeString();

        const matEl = document.getElementById('object-material');
        if (matEl) {
            const cat = data.analysis.category || 'Others';
            const matNames = {
                Plastic: window.currentLang === 'ko' ? '플라스틱' : 'Plastic',
                Metal: window.currentLang === 'ko' ? '금속' : 'Metal',
                Glass: window.currentLang === 'ko' ? '유리' : 'Glass',
                Paper: window.currentLang === 'ko' ? '종이' : 'Paper',
                Others: window.currentLang === 'ko' ? '기타' : 'Others'
            };
            matEl.className = `mat-badge mat-${cat.toLowerCase()}`;
            matEl.textContent = matNames[cat] || cat;
        }

        // HUD panel sync
        const hudObj = document.getElementById('hud-obj-name');
        const hudType = document.getElementById('hud-obj-type');
        const hudMat = document.getElementById('hud-obj-material');
        if (hudObj) hudObj.textContent = data.analysis.object || '-';
        if (hudType) hudType.textContent = data.analysis.type || '-';
        if (hudMat) {
            const cat = data.analysis.category || 'Others';
            const matNames = {
                Plastic: window.currentLang === 'ko' ? '플라스틱' : 'Plastic',
                Metal: window.currentLang === 'ko' ? '금속' : 'Metal',
                Glass: window.currentLang === 'ko' ? '유리' : 'Glass',
                Paper: window.currentLang === 'ko' ? '종이' : 'Paper',
                Others: window.currentLang === 'ko' ? '기타' : 'Others'
            };
            hudMat.className = `mat-badge mat-${cat.toLowerCase()}`;
            hudMat.textContent = matNames[cat] || cat;
        }
    }

    if (data.metadata) {
        const sensorInfo = document.getElementById('sensor-info');
        const resInfo = document.getElementById('res-info');
        const sizeInfo = document.getElementById('size-info');
        const durationInfo = document.getElementById('duration-info');
        if (sensorInfo) sensorInfo.textContent = data.metadata.sensor;
        if (resInfo) resInfo.textContent = data.metadata.resolution || '-';
        if (sizeInfo) sizeInfo.textContent = data.metadata.size_kb + ' KB';
        if (durationInfo) durationInfo.textContent = data.metadata.duration_ms + ' ms';
    }
}

// --- Chat bubble element creator ---
function addChatMessage(role, text, timings = null, turnId = null) {
    const mainChat = document.getElementById('chat-display-main');
    if (!mainChat) return;

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

    if (!role) return;

    const placeholder = document.getElementById('image-sensor-placeholder');
    if (placeholder) placeholder.remove();

    const bubble = document.createElement('div');
    bubble.className = `chat-bubble chat-bubble-${role.toLowerCase()}`;
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
window.addChatMessage = addChatMessage;

// --- Global helper to queue motions convenient shortcut ---
window.applyPreset = function(x, y, z) {
    const inputX = document.getElementById('ptp-x');
    const inputY = document.getElementById('ptp-y');
    const inputZ = document.getElementById('ptp-z');
    const moveBtn = document.getElementById('robot3axis-ptp-move');

    if (inputX) inputX.value = x;
    if (inputY) inputY.value = y;
    if (inputZ) inputZ.value = z;

    if (moveBtn) {
        [inputX, inputY, inputZ].forEach(el => {
            if (el) {
                el.style.background = 'rgba(16, 185, 129, 0.1)';
                setTimeout(() => el.style.background = '', 500);
            }
        });
        moveBtn.click();
        addLog('System', `Preset applied shortcut: (${x}, ${y}, ${z})`);
    }
};
