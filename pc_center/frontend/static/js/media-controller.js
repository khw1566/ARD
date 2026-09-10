    let selectedMusicFile = null;

    window.refreshMusicList = async function() {
        const container = document.getElementById('music-list-container');
        if (!container) return;
        
        try {
            const response = await fetch('/api/music/list');
            const data = await response.json();
            if (data.status === 'success') {
                container.innerHTML = '';
                const localFiles = data.local_files || [];
                const ytFiles = data.youtube_files || [];
                
                if (localFiles.length === 0 && ytFiles.length === 0) {
                    container.innerHTML = '<div style="color: var(--text-dim); font-size: 0.8rem; text-align: center; padding-top: 200px;">보관함이 비어 있습니다. (MusicData 폴더)</div>';
                    return;
                }
                
                const createSectionHeader = (title) => {
                    const header = document.createElement('div');
                    header.style.color = 'var(--success)';
                    header.style.fontSize = '0.8rem';
                    header.style.fontWeight = 'bold';
                    header.style.marginTop = '15px';
                    header.style.marginBottom = '8px';
                    header.style.paddingBottom = '4px';
                    header.style.borderBottom = '1px solid rgba(16,185,129,0.3)';
                    header.innerText = title;
                    container.appendChild(header);
                };

                const createFileItem = (file, isYoutube) => {
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
                    
                    const svgIcon = isYoutube 
                        ? `<svg width="14" height="14" viewBox="0 0 24 24" fill="currentColor"><polygon points="5 3 19 12 5 21"></polygon></svg>`
                        : `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 18V5l12-2v13"></path><circle cx="6" cy="18" r="3"></circle><circle cx="18" cy="16" r="3"></circle></svg>`;
                    const displayName = isYoutube ? file.replace('Youtube/', '') : file;
                    
                    item.innerHTML = `<span class="music-icon" style="font-size: 0.95rem; color: #94a3b8; transition: color 0.2s; display: flex; align-items: center;">${svgIcon}</span> <span style="flex: 1; overflow: hidden; text-overflow: ellipsis;">${displayName}</span>`;
                    
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
                            el.style.boxShadow = 'none';
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
                };

                if (localFiles.length > 0) {
                    createSectionHeader('수동 추가 음악');
                    localFiles.forEach(f => createFileItem(f, false));
                }
                
                if (ytFiles.length > 0) {
                    createSectionHeader('유튜브 다운로드 음악');
                    ytFiles.forEach(f => createFileItem(f, true));
                }
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
                        icon.innerHTML = `<svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="#22d3ee" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M2 10s3-3 5-3 5 6 7 6 5-6 8-3"></path><path d="M2 14s3-3 5-3 5 6 7 6 5-6 8-3"></path></svg>`;
                        icon.style.animation = 'pulse 2s cubic-bezier(0.4, 0, 0.6, 1) infinite';
                    }
                    if (btn) {
                        btn.style.background = '#ef4444';
                        btn.style.boxShadow = '0 4px 12px rgba(239, 68, 68, 0.2)';
                        btn.innerHTML = `<svg width="14" height="14" viewBox="0 0 24 24" fill="currentColor" style="margin-right: 6px;"><rect x="4" y="4" width="16" height="16" rx="2"></rect></svg><span>STOP AUDIO MIRRORING</span>`;
                    }
                } else {
                    if (container) {
                        container.style.background = 'rgba(255, 255, 255, 0.02)';
                        container.style.border = '1px solid rgba(255, 255, 255, 0.1)';
                        container.style.boxShadow = 'none';
                        container.style.transform = 'scale(1.0)';
                    }
                    if (icon) {
                        icon.innerHTML = `<svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="var(--primary)" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 1a3 3 0 0 0-3 3v8a3 3 0 0 0 6 0V4a3 3 0 0 0-3-3z"></path><path d="M19 10v2a7 7 0 0 1-14 0v-2"></path><line x1="12" y1="19" x2="12" y2="23"></line><line x1="8" y1="23" x2="16" y2="23"></line></svg>`;
                        icon.style.animation = 'none';
                    }
                    if (btn) {
                        btn.style.background = 'var(--primary)';
                        btn.style.boxShadow = '0 4px 12px rgba(0, 242, 254, 0.2)';
                        btn.innerHTML = `<svg width="14" height="14" viewBox="0 0 24 24" fill="currentColor" style="margin-right: 6px;"><polygon points="5 3 19 12 5 21"></polygon></svg><span>START AUDIO MIRRORING</span>`;
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

