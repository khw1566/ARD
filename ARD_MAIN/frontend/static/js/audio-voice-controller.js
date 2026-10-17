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
                if (status) { status.textContent = '파일 로드됨'; status.style.color = 'var(--success)'; status.style.display = 'inline'; }
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
                if (status) { status.textContent = '저장 완료 — AI에 즉시 반영됨'; status.style.color = 'var(--success)'; status.style.display = 'inline'; }
            } else {
                if (status) { status.textContent = '저장 실패'; status.style.color = 'var(--danger)'; status.style.display = 'inline'; }
            }
        } catch (e) {
            if (status) { status.textContent = '오류 발생'; status.style.color = 'var(--danger)'; status.style.display = 'inline'; }
        }
        btn.disabled = false;
        btn.innerHTML = `<svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="margin-right: 4px;"><path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z"></path><polyline points="17 21 17 13 7 13 7 21"></polyline><polyline points="7 3 7 8 15 8"></polyline></svg><span>저장</span>`;
        setTimeout(() => { if (status) status.style.display = 'none'; }, 4000);
    });

    document.getElementById('reload-persona-btn')?.addEventListener('click', loadPersonaFile);

    // --- ControlMCU 음성 통합 설정 (VAD, Sensitivity, TTS) ---
    async function updateControlMCUVoiceUI() {
        try {
            const res = await fetch('/api/controlmcu/voice/status');
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
            console.error('Failed to update ControlMCU voice UI', e);
        }
    }

    async function loadTTSVoices(engine, selectedVoice = null) {
        const voiceSelect = document.getElementById('tts-voice-select');
        if (!voiceSelect) return;
        
        try {
            const res = await fetch(`/api/controlmcu/voice/voices?engine=${engine}`);
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

    async function saveControlMCUVoiceConfig() {
        window.saveControlMCUVoiceConfig = saveControlMCUVoiceConfig;
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
            await fetch('/api/controlmcu/voice/settings', {
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
            console.error('Failed to save ControlMCU voice config', e);
        }
    }

    window.stopAllAudio = async function() {
        try {
            const response = await fetch('/api/controlmcu/voice/stop', {
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
        
        saveControlMCUVoiceConfig();
    };

    // 이벤트 리스너 등록
    document.getElementById('tts-engine-select')?.addEventListener('change', async (e) => {
        const engine = e.target.value;
        toggleOpenAIKeyUI(engine);
        await loadTTSVoices(engine);
        saveControlMCUVoiceConfig();
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

    document.getElementById('tts-voice-select')?.addEventListener('change', saveControlMCUVoiceConfig);
    document.getElementById('stt-engine-select')?.addEventListener('change', saveControlMCUVoiceConfig);
    document.getElementById('voice-mode-toggle')?.addEventListener('change', saveControlMCUVoiceConfig);
    document.getElementById('voice-sensitivity')?.addEventListener('change', saveControlMCUVoiceConfig);
    document.getElementById('mcu-volume')?.addEventListener('input', (e) => { document.getElementById('mcu-volume-val').textContent = e.target.value; });
    document.getElementById('mcu-volume')?.addEventListener('change', saveControlMCUVoiceConfig);
    document.getElementById('controlmcu_ip')?.addEventListener('change', saveControlMCUVoiceConfig);
    
    document.getElementById('tts-pitch')?.addEventListener('input', (e) => { document.getElementById('pitch-val').textContent = e.target.value; });
    document.getElementById('tts-pitch')?.addEventListener('change', saveControlMCUVoiceConfig);
    document.getElementById('tts-rate')?.addEventListener('input', (e) => { document.getElementById('rate-val').textContent = e.target.value; });
    document.getElementById('tts-rate')?.addEventListener('change', saveControlMCUVoiceConfig);
    document.getElementById('tts-effect')?.addEventListener('change', saveControlMCUVoiceConfig);
    document.getElementById('openai_api_key')?.addEventListener('change', saveControlMCUVoiceConfig);
    document.getElementById('azure_api_key')?.addEventListener('change', saveControlMCUVoiceConfig);
    document.getElementById('azure_region')?.addEventListener('change', saveControlMCUVoiceConfig);
    document.getElementById('emotion-lab-degree')?.addEventListener('change', saveControlMCUVoiceConfig);

    // --- Microsoft Azure 감정 음성 연구소 (Emotional Voice Lab) 재생/정지 제어 ---
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
                statusSpan.textContent = "Azure Neural TTS 합성 및 오디오 생성 중...";
                statusSpan.style.display = "block";
            }
            if (playBtn) playBtn.disabled = true;

            // 이전 오디오 즉각 중지
            if (emotionLabAudio) {
                emotionLabAudio.pause();
                emotionLabAudio = null;
            }

            const res = await fetch('/api/controlmcu/voice/test_emotion_tts', {
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
                if (statusSpan) statusSpan.textContent = "오디오 재생 중...";
                
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
                        statusSpan.textContent = "오디오 재생 실패";
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
        if (typeof window.addChatMessage === 'function') {
            window.addChatMessage('User', text, null, turnId);
        } else if (typeof addChatMessage === 'function') {
            addChatMessage('User', text, null, turnId);
        }
        
        try {
            await fetch('/api/controlmcu/voice/chat', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ text: text, turn_id: turnId })
            });
        } catch (e) { console.error('Chat API Error:', e); }
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
                setAudioStatus('멜로디 전송 완료! MCU 출력을 확인하세요.');
                if (window.addLog) window.addLog('System', '멜로디 테스트 전송이 시작되었습니다.');
            } else {
                setAudioStatus('전송 실패: ' + result.message, true);
            }
        } catch (e) {
            setAudioStatus('네트워크 오류: ' + e.message, true);
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
                setAudioStatus('TTS 전송 완료! MCU 출력을 확인하세요.');
                if (window.addLog) window.addLog('System', `TTS 테스트 전송 시작: "${text}"`);
            } else {
                setAudioStatus('전송 실패: ' + result.message, true);
            }
        } catch (e) {
            setAudioStatus('네트워크 오류: ' + e.message, true);
        } finally {
            testTtsBtn.disabled = false;
        }
    });

