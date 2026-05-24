// --- ARD Control Center: Control & Sound MCU Tab Module ---
document.addEventListener('DOMContentLoaded', () => {
    // --- Sub-tab Switching within Voice & Media Container ---
    window.switchSubTab = function(subtabId) {
        document.querySelectorAll('.subtab-content').forEach(el => el.style.display = 'none');
        document.querySelectorAll('.subtab-btn').forEach(btn => btn.classList.remove('active-subtab'));

        const targetContent = document.getElementById(`subtab-content-${subtabId}`);
        const targetBtn = document.getElementById(`subtab-btn-${subtabId}`);

        if (targetContent) targetContent.style.display = (subtabId === 'chat') ? 'flex' : 'block';
        if (targetBtn) targetBtn.classList.add('active-subtab');

        if (subtabId === 'music') {
            refreshMusicList();
            startMusicStatusPoller();
        } else {
            stopMusicStatusPoller();
        }

        if (subtabId === 'radio') {
            loadRadioStations();
            startRadioStatusPoller();
        } else {
            stopRadioStatusPoller();
        }
    };

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
                if (status) { 
                    status.textContent = '✓ 파일 로드됨'; 
                    status.style.color = 'var(--success)'; 
                    status.style.display = 'inline'; 
                }
            }
        } catch (e) {
            if (editor) editor.value = '// 파일 로드 실패: 서버를 확인해 주세요.';
            console.error('Failed to load persona file', e);
        }
    }
    window.loadPersonaFile = loadPersonaFile;

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
                if (status) { 
                    status.textContent = '✅ 저장 완료 — AI에 즉시 반영됨'; 
                    status.style.color = 'var(--success)'; 
                    status.style.display = 'inline'; 
                }
            } else {
                if (status) { 
                    status.textContent = '❌ 저장 실패'; 
                    status.style.color = 'var(--danger)'; 
                    status.style.display = 'inline'; 
                }
            }
        } catch (e) {
            if (status) { 
                status.textContent = '❌ 오류'; 
                status.style.color = 'var(--danger)'; 
                status.style.display = 'inline'; 
            }
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
                
                if (pitchSlider) { 
                    pitchSlider.value = conf.tts_pitch || 0; 
                    const pVal = document.getElementById('pitch-val');
                    if (pVal) pVal.textContent = pitchSlider.value; 
                }
                if (rateSlider) { 
                    rateSlider.value = conf.tts_rate || 0; 
                    const rVal = document.getElementById('rate-val');
                    if (rVal) rVal.textContent = rateSlider.value; 
                }
                
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
    window.updateSoundMCUUI = updateSoundMCUUI;

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
            if (window.addLog) window.addLog('System', `Sound 설정 저장됨: STT(${sttEngine}) / TTS(${engine}) / Vol(${volume}) / EmotionDegree(${styleDegree})`);
        } catch (e) {
            console.error('Failed to save SoundMCU config', e);
        }
    }
    window.saveSoundMCUConfig = saveSoundMCUConfig;

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

        if (pitchSlider) {
            const pVal = document.getElementById('pitch-val');
            if (pVal) pVal.textContent = pitchSlider.value;
        }
        if (rateSlider) {
            const rVal = document.getElementById('rate-val');
            if (rVal) rVal.textContent = rateSlider.value;
        }
        
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
                if (btn) btn.style.background = 'var(--danger)';
                if (btnText) btnText.textContent = 'STOP MIC';
                if (indicator) {
                    indicator.style.background = 'var(--danger)';
                    indicator.classList.add('pulse-mic');
                }
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
    window.togglePCMic = togglePCMic;

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

    // --- Radio Station Custom Logic ---
    async function loadRadioStations() {
        const select = document.getElementById('radio-station-select');
        if (!select || select.children.length > 0) return;
        try {
            const res = await fetch('/api/soundmcu/radio/stations');
            const data = await res.json();
            if (data.status === 'success') {
                select.innerHTML = '';
                data.stations.forEach(station => {
                    const opt = document.createElement('option');
                    opt.value = station.url;
                    opt.textContent = `${station.name} (${station.bitrate}kbps, ${station.genre})`;
                    select.appendChild(opt);
                });
            }
        } catch (e) {
            console.error("Failed to load radio stations", e);
        }
    }

    window.playSelectedRadio = async function() {
        const select = document.getElementById('radio-station-select');
        if (!select) return;
        const url = select.value;
        const name = select.options[select.selectedIndex].text.split(' (')[0];
        
        try {
            const res = await fetch('/api/soundmcu/radio/play', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ url: url, name: name })
            });
            const data = await res.json();
            if (data.status === 'success') {
                if (window.addLog) window.addLog('Radio', `라디오 스트리밍 개시: ${name}`);
            }
        } catch(e) {
            if (window.addLog) window.addLog('Error', '라디오 스트리밍 요청 실패: ' + e.message);
        }
    };

    window.playCustomRadio = async function() {
        const nameInput = document.getElementById('radio-custom-name');
        const urlInput = document.getElementById('radio-custom-url');
        if (!urlInput || !urlInput.value) {
            alert('Custom Stream URL을 입력해 주세요.');
            return;
        }
        const name = nameInput?.value || '사용자 정의 채널';
        const url = urlInput.value;
        
        try {
            const res = await fetch('/api/soundmcu/radio/play', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ url: url, name: name })
            });
            const data = await res.json();
            if (data.status === 'success') {
                if (window.addLog) window.addLog('Radio', `라디오 스트리밍 개시: ${name}`);
            }
        } catch(e) {
            if (window.addLog) window.addLog('Error', '커스텀 라디오 스트리밍 요청 실패: ' + e.message);
        }
    };

    window.stopRadioStreaming = async function() {
        try {
            const res = await fetch('/api/soundmcu/radio/stop', { method: 'POST' });
            const data = await res.json();
            if (data.status === 'success') {
                if (window.addLog) window.addLog('Radio', '라디오 스트리밍 정지 완료');
            }
        } catch(e) {
            console.error(e);
        }
    };

    let radioPoller = null;
    function startRadioStatusPoller() {
        if (radioPoller) clearInterval(radioPoller);
        radioPoller = setInterval(async () => {
            try {
                const res = await fetch('/api/soundmcu/radio/status');
                const data = await res.json();
                const textEl = document.getElementById('radio-status-text');
                const indicator = document.getElementById('radio-pulse-icon');
                if (data.status === 'success') {
                    if (data.active) {
                        if (textEl) textEl.textContent = `Streaming: ${data.name}`;
                        if (indicator) {
                            indicator.style.background = 'var(--success)';
                            indicator.style.boxShadow = `0 0 10px var(--success)`;
                        }
                    } else {
                        if (textEl) textEl.textContent = 'Not Streaming';
                        if (indicator) {
                            indicator.style.background = '#94a3b8';
                            indicator.style.boxShadow = 'none';
                        }
                    }
                }
            } catch (e) {
                console.error(e);
            }
        }, 1000);
    }

    function stopRadioStatusPoller() {
        if (radioPoller) {
            clearInterval(radioPoller);
            radioPoller = null;
        }
    }

    // --- Music Library & Custom Playback Controller ---
    let musicPoller = null;
    let isUserSeeking = false;

    window.refreshMusicList = async function() {
        const container = document.getElementById('music-list-container');
        if (!container) return;
        container.innerHTML = '<div style="color: var(--text-dim); font-size: 0.8rem; text-align: center; padding-top: 200px;"><div class="voice-loading" style="margin: 0 auto 10px auto;"></div>Refreshing Music...</div>';
        
        try {
            const res = await fetch('/api/soundmcu/music/list');
            const data = await res.json();
            if (data.status === 'success') {
                container.innerHTML = '';
                if (data.files.length === 0) {
                    container.innerHTML = '<div style="color: var(--text-dim); text-align: center; padding-top: 200px; font-style: italic;">No music files (.mp3, .wav) found in music/ folder.</div>';
                    return;
                }
                
                data.files.forEach((file, idx) => {
                    const row = document.createElement('div');
                    row.style.cssText = `
                        display: flex; 
                        justify-content: space-between; 
                        align-items: center; 
                        padding: 10px 15px; 
                        border-bottom: 1px solid rgba(255,255,255,0.03); 
                        cursor: pointer; 
                        transition: all 0.15s ease;
                        font-size: 0.85rem;
                        color: #cbd5e1;
                    `;
                    row.innerHTML = `
                        <div style="display: flex; align-items: center; gap: 10px; min-width: 0; flex: 1;">
                            <span style="color: #64748b; font-family: monospace; font-size: 0.75rem;">${(idx+1).toString().padStart(2, '0')}</span>
                            <span class="music-title" style="white-space: nowrap; overflow: hidden; text-overflow: ellipsis; font-weight: 500;">${file.filename}</span>
                        </div>
                        <div style="display: flex; align-items: center; gap: 15px; font-size: 0.75rem; color: #64748b; flex-shrink: 0;">
                            <span>${file.size}</span>
                            <button class="music-play-sub-btn" style="background: rgba(16, 185, 129, 0.1); border: 1px solid rgba(16, 185, 129, 0.2); color: var(--success); cursor: pointer; padding: 3px 10px; border-radius: 4px; font-size: 0.7rem; font-weight: bold; transition: all 0.2s;">PLAY</button>
                        </div>
                    `;
                    
                    row.addEventListener('mouseover', () => { row.style.background = 'rgba(255,255,255,0.03)'; });
                    row.addEventListener('mouseout', () => { row.style.background = 'transparent'; });
                    
                    const triggerPlay = () => {
                        playMusicFile(file.filename);
                    };
                    row.addEventListener('click', (e) => {
                        if (e.target.tagName !== 'BUTTON') triggerPlay();
                    });
                    row.querySelector('.music-play-sub-btn').addEventListener('click', (e) => {
                        e.stopPropagation();
                        triggerPlay();
                    });
                    
                    container.appendChild(row);
                });
            }
        } catch(e) {
            console.error(e);
            container.innerHTML = '<div style="color: #f87171; text-align: center; padding-top: 200px;">Failed to refresh music library.</div>';
        }
    };

    async function playMusicFile(filename) {
        try {
            const res = await fetch('/api/soundmcu/music/play', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ filename: filename })
            });
            const data = await res.json();
            if (data.status === 'success') {
                if (window.addLog) window.addLog('Music', `음악 파일 재생 요청: ${filename}`);
            }
        } catch (e) {
            if (window.addLog) window.addLog('Error', '재생 실패: ' + e.message);
        }
    }

    window.toggleMusicPlay = async function() {
        try {
            const res = await fetch('/api/soundmcu/music/toggle', { method: 'POST' });
            const data = await res.json();
            if (data.status === 'success') {
                if (window.addLog) window.addLog('Music', `음악 ${data.playing ? '재생' : '일시정지'} 토글 완료`);
            }
        } catch(e) {
            console.error(e);
        }
    };

    window.stopMusicPlayer = async function() {
        try {
            const res = await fetch('/api/soundmcu/music/stop', { method: 'POST' });
            const data = await res.json();
            if (data.status === 'success') {
                if (window.addLog) window.addLog('Music', '음악 재생 정지');
            }
        } catch(e) {
            console.error(e);
        }
    };

    window.seekMusicPlayer = async function(percent) {
        isUserSeeking = true;
        try {
            const res = await fetch('/api/soundmcu/music/seek', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ percent: parseFloat(percent) })
            });
            if (res.ok) {
                setTimeout(() => { isUserSeeking = false; }, 300);
            }
        } catch (e) {
            isUserSeeking = false;
            console.error(e);
        }
    };

    const formatTime = (seconds) => {
        if (isNaN(seconds) || seconds === null) return '00:00';
        const mins = Math.floor(seconds / 60);
        const secs = Math.floor(seconds % 60);
        return `${mins.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
    };

    function startMusicStatusPoller() {
        if (musicPoller) clearInterval(musicPoller);
        musicPoller = setInterval(async () => {
            try {
                const res = await fetch('/api/soundmcu/music/status');
                const data = await res.json();
                if (data.status === 'success') {
                    const cdArt = document.getElementById('music-cd-art');
                    const playBtn = document.getElementById('music-play-btn');
                    const titleText = document.getElementById('music-current-title');
                    const statusText = document.getElementById('music-current-status');
                    
                    if (data.playing) {
                        if (cdArt) { 
                            cdArt.className = 'music-art-playing'; 
                        }
                        if (playBtn) playBtn.textContent = '⏸';
                        if (statusText) statusText.textContent = 'Playing...';
                    } else if (data.paused) {
                        if (cdArt) { 
                            cdArt.className = 'music-art-stopped'; 
                        }
                        if (playBtn) playBtn.textContent = '▶';
                        if (statusText) statusText.textContent = 'Paused';
                    } else {
                        if (cdArt) { 
                            cdArt.className = 'music-art-stopped'; 
                        }
                        if (playBtn) playBtn.textContent = '▶';
                        if (statusText) statusText.textContent = 'Select a song to start playing';
                    }

                    if (titleText) {
                        titleText.textContent = data.filename || 'No Song Loaded';
                    }

                    const elapsedText = document.getElementById('music-time-elapsed');
                    const totalText = document.getElementById('music-time-total');
                    const seekBar = document.getElementById('music-seek-bar');

                    if (elapsedText) elapsedText.textContent = formatTime(data.elapsed);
                    if (totalText) totalText.textContent = formatTime(data.duration);
                    
                    if (seekBar && !isUserSeeking) {
                        if (data.duration && data.duration > 0) {
                            seekBar.value = (data.elapsed / data.duration) * 100;
                        } else {
                            seekBar.value = 0;
                        }
                    }
                }
            } catch(e) {
                console.error(e);
            }
        }, 800);
    }

    function stopMusicStatusPoller() {
        if (musicPoller) {
            clearInterval(musicPoller);
            musicPoller = null;
        }
    }

    // --- 3-Band Parametric EQ Controller ---
    window.updateEQValues = function() {
        const bass = document.getElementById('eq-bass');
        const mid = document.getElementById('eq-mid');
        const treble = document.getElementById('eq-treble');

        const formatDB = (val) => {
            const v = parseFloat(val);
            return (v >= 0 ? '+' : '') + v.toFixed(1) + 'dB';
        };

        if (bass) document.getElementById('eq-bass-val').textContent = formatDB(bass.value);
        if (mid) document.getElementById('eq-mid-val').textContent = formatDB(mid.value);
        if (treble) document.getElementById('eq-treble-val').textContent = formatDB(treble.value);
    };

    window.sendEQConfig = async function() {
        const bass = parseFloat(document.getElementById('eq-bass')?.value || 0);
        const mid = parseFloat(document.getElementById('eq-mid')?.value || 0);
        const treble = parseFloat(document.getElementById('eq-treble')?.value || 0);

        try {
            const res = await fetch('/api/soundmcu/eq', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ bass: bass, mid: mid, treble: treble })
            });
            const data = await res.json();
            if (data.status === 'success') {
                if (window.addLog) window.addLog('Music', `EQ 설정 적용됨: Bass(${bass}dB) / Mid(${mid}dB) / Treble(${treble}dB)`);
            }
        } catch (e) {
            console.error(e);
        }
    };

    window.applyEQPreset = function(preset) {
        const bass = document.getElementById('eq-bass');
        const mid = document.getElementById('eq-mid');
        const treble = document.getElementById('eq-treble');

        if (preset === 'flat') {
            if (bass) bass.value = 0;
            if (mid) mid.value = 0;
            if (treble) treble.value = 0;
        } else if (preset === 'bass') {
            if (bass) bass.value = 6;
            if (mid) mid.value = -1.5;
            if (treble) treble.value = 1;
        } else if (preset === 'vocal') {
            if (bass) bass.value = -2;
            if (mid) mid.value = 5;
            if (treble) treble.value = 2;
        }

        window.updateEQValues();
        window.sendEQConfig();
    };

    // --- YouTube Audio PCM Streamer ---
    window.playYoutubeAudio = async function() {
        const urlInput = document.getElementById('youtube-url-input');
        if (!urlInput || !urlInput.value) {
            alert('유튜브 영상 주소(URL)를 입력해 주세요.');
            return;
        }
        const url = urlInput.value;
        const btn = document.getElementById('youtube-play-btn');
        if (btn) {
            btn.disabled = true;
            btn.textContent = 'EXTRACTING AUDIO...';
        }
        
        try {
            if (window.addLog) window.addLog('Music', `유튜브 오디오 스트림 추출 개시: ${url}`);
            const res = await fetch('/api/soundmcu/youtube/play', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ url: url })
            });
            const data = await res.json();
            if (data.status === 'success') {
                if (window.addLog) window.addLog('Music', `유튜브 실시간 스트리밍 개시 성공! [${data.title}]`);
                urlInput.value = '';
            } else {
                if (window.addLog) window.addLog('Error', '유튜브 스트리밍 실패: ' + data.message);
            }
        } catch(e) {
            if (window.addLog) window.addLog('Error', '유튜브 스트리밍 요청 실패: ' + e.message);
        } finally {
            if (btn) {
                btn.disabled = false;
                btn.textContent = '▶ STREAM YOUTUBE AUDIO';
            }
        }
    };

    // --- PC Audio Desktop Mirror (WASAPI Loopback) ---
    let isAudioMirroring = false;

    window.toggleAudioMirror = async function() {
        const btn = document.getElementById('mirror-toggle-btn');
        const container = document.getElementById('mirror-wave-container');
        const icon = document.getElementById('mirror-icon-display');

        if (isAudioMirroring) {
            // STOP
            try {
                const res = await fetch('/api/soundmcu/mirror/stop', { method: 'POST' });
                const data = await res.json();
                if (data.status === 'success') {
                    isAudioMirroring = false;
                    if (btn) {
                        btn.style.background = 'var(--primary)';
                        btn.textContent = '▶ START AUDIO MIRRORING';
                        btn.style.boxShadow = '0 4px 12px rgba(0, 242, 254, 0.2)';
                    }
                    if (container) {
                        container.style.border = '2px solid rgba(0, 242, 254, 0.15)';
                        container.style.boxShadow = 'none';
                    }
                    if (icon) {
                        icon.textContent = '🎙️';
                        icon.classList.remove('pulse-mic');
                    }
                    if (window.addLog) window.addLog('Sound', 'Desktop Audio Loopback mirroring stopped.');
                }
            } catch (e) {
                console.error(e);
            }
        } else {
            // START
            try {
                if (window.addLog) window.addLog('Sound', 'Starting PC Desktop Audio Mirroring (WASAPI Loopback)...');
                const res = await fetch('/api/soundmcu/mirror/start', { method: 'POST' });
                const data = await res.json();
                if (data.status === 'success') {
                    isAudioMirroring = true;
                    if (btn) {
                        btn.style.background = 'var(--danger)';
                        btn.textContent = '⏹ STOP AUDIO MIRRORING';
                        btn.style.boxShadow = '0 4px 12px rgba(239, 68, 68, 0.3)';
                    }
                    if (container) {
                        container.style.border = '2px solid var(--danger)';
                        container.style.boxShadow = '0 0 15px rgba(239, 68, 68, 0.4)';
                    }
                    if (icon) {
                        icon.textContent = '🔊';
                        icon.classList.add('pulse-mic');
                    }
                    if (window.addLog) window.addLog('Sound', 'WASAPI Loopback mirroring started. Check the SoundMCU speaker!');
                } else {
                    if (window.addLog) window.addLog('Error', 'Failed to start mirroring: ' + data.message);
                }
            } catch (e) {
                console.error(e);
            }
        }
    };

    window.syncGlobalVolume = function(val) {
        const mcuSlider = document.getElementById('mcu-volume');
        const mcuSpan = document.getElementById('mcu-volume-val');
        const globalVolSpan = document.getElementById('global-media-volume-val');
        
        if (mcuSlider) mcuSlider.value = val;
        if (mcuSpan) mcuSpan.textContent = val;
        if (globalVolSpan) globalVolSpan.textContent = val;
        
        // Throttled config save
        if (window.volSaveTimer) clearTimeout(window.volSaveTimer);
        window.volSaveTimer = setTimeout(() => {
            saveSoundMCUConfig();
        }, 500);
    };

    document.getElementById('mcu-volume')?.addEventListener('input', (e) => {
        window.syncGlobalVolume(e.target.value);
    });

    document.getElementById('voice-mode-toggle')?.addEventListener('change', saveSoundMCUConfig);
    document.getElementById('voice-sensitivity')?.addEventListener('change', saveSoundMCUConfig);
    document.getElementById('emotion-lab-degree')?.addEventListener('change', saveSoundMCUConfig);
    document.getElementById('stt-engine-select')?.addEventListener('change', saveSoundMCUConfig);

    document.getElementById('voice-sensitivity')?.addEventListener('input', (e) => {
        const valSpan = document.getElementById('sensitivity-val');
        if (valSpan) valSpan.textContent = e.target.value;
    });

    document.getElementById('emotion-lab-degree')?.addEventListener('input', (e) => {
        const valSpan = document.getElementById('emotion-lab-degree-val');
        if (valSpan) valSpan.textContent = parseFloat(e.target.value).toFixed(1);
    });

    // Chat Send Logic
    const sendChatMessage = async () => {
        const input = document.getElementById('chat-input');
        if (!input || !input.value.trim()) return;
        const msg = input.value.trim();
        input.value = '';
        
        try {
            await fetch('/api/soundmcu/chat', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ message: msg })
            });
        } catch (e) {
            console.error("Failed to send chat", e);
        }
    };

    document.getElementById('send-chat-btn')?.addEventListener('click', sendChatMessage);
    document.getElementById('chat-input')?.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') sendChatMessage();
    });
});
