    // --- 통계 모달 (Communication & API Stats) 로직 ---
    const statsModal = document.getElementById('stats-modal');
    const openStatsBtn = document.getElementById('stats-modal-btn');
    const closeStatsBtn = document.getElementById('close-stats-modal');
    const refreshStatsBtn = document.getElementById('stats-refresh-btn');
    const resetStatsBtn = document.getElementById('stats-reset-btn');
    let statsInterval = null;

    async function loadStatsData() {
        if (!statsModal || statsModal.style.display === 'none') return;
        try {
            const res = await fetch('/api/stats');
            if (!res.ok) return;
            const data = await res.json();
            renderStatsDashboard(data);
        } catch (e) {
            console.error('Stats fetch error:', e);
        }
    }

    function renderStatsDashboard(data) {
        // 1. KPI 카드 업데이트
        const elUptime = document.getElementById('stats-kpi-uptime');
        const elStart = document.getElementById('stats-kpi-start-time');
        const elMcuTotal = document.getElementById('stats-kpi-mcu-total');
        const elMcuRate = document.getElementById('stats-kpi-mcu-rate');
        const elApiTotal = document.getElementById('stats-kpi-api-total');
        const elApiRate = document.getElementById('stats-kpi-api-rate');

        if (elUptime) elUptime.textContent = data.uptime_formatted || '00:00:00';
        if (elStart) elStart.textContent = (currentLang === 'ko' ? '시작: ' : 'Started: ') + (data.server_start_time || '-');
        if (elMcuTotal) elMcuTotal.textContent = (data.total_mcu_packets || 0).toLocaleString();
        if (elMcuRate) {
            const rate = data.overall_mcu_success_rate ?? 100;
            elMcuRate.textContent = rate + '%';
            elMcuRate.style.color = rate >= 95 ? 'var(--success)' : (rate >= 80 ? '#fbbf24' : 'var(--danger)');
        }
        if (elApiTotal) elApiTotal.textContent = (data.total_api_calls || 0).toLocaleString();
        if (elApiRate) {
            const rate = data.overall_api_success_rate ?? 100;
            elApiRate.textContent = rate + '%';
            elApiRate.style.color = rate >= 95 ? 'var(--success)' : (rate >= 80 ? '#fbbf24' : 'var(--danger)');
        }

        // 2. MCU 통신 테이블 렌더링
        const mcuTbody = document.getElementById('stats-mcu-tbody');
        if (mcuTbody && data.mcu_stats) {
            let html = '';
            for (const [key, item] of Object.entries(data.mcu_stats)) {
                const rate = item.success_rate;
                const badgeClass = rate >= 95 ? 'rate-high' : (rate >= 80 ? 'rate-mid' : 'rate-low');
                html += `
                    <tr>
                        <td style="font-weight: 600; color: var(--text-main);">${item.name}</td>
                        <td style="color: var(--primary);">${item.heartbeat.toLocaleString()}</td>
                        <td style="color: var(--secondary);">${item.control_cmd.toLocaleString()}</td>
                        <td style="color: #34d399;">${item.sensor_data.toLocaleString()}</td>
                        <td style="font-weight: bold;">${item.total_packets.toLocaleString()}</td>
                        <td style="color: ${item.error_count > 0 ? 'var(--danger)' : 'var(--text-dim)'};">${item.error_count.toLocaleString()}</td>
                        <td><span class="rate-badge ${badgeClass}">${rate}%</span></td>
                        <td style="color: var(--text-dim); font-size: 0.75rem;">${item.last_timestamp}</td>
                    </tr>
                `;
            }
            mcuTbody.innerHTML = html;
        }

        // 3. API 및 AI / MCP 통계 테이블 렌더링
        const apiTbody = document.getElementById('stats-api-tbody');
        if (apiTbody && data.api_stats) {
            let html = '';
            for (const [key, item] of Object.entries(data.api_stats)) {
                const rate = item.success_rate;
                const badgeClass = rate >= 95 ? 'rate-high' : (rate >= 80 ? 'rate-mid' : 'rate-low');
                html += `
                    <tr>
                        <td style="font-weight: 600; color: var(--text-main);">${item.name}</td>
                        <td style="font-weight: bold;">${item.call_count.toLocaleString()}</td>
                        <td style="color: var(--success);">${item.success_count.toLocaleString()}</td>
                        <td style="color: ${item.fail_count > 0 ? 'var(--danger)' : 'var(--text-dim)'};">${item.fail_count.toLocaleString()}</td>
                        <td><span class="rate-badge ${badgeClass}">${rate}%</span></td>
                        <td style="color: var(--text-dim); font-size: 0.75rem;">${item.last_timestamp}</td>
                    </tr>
                `;
            }
            apiTbody.innerHTML = html;
        }
    }

    if (openStatsBtn && statsModal) {
        openStatsBtn.onclick = () => {
            statsModal.style.display = 'block';
            loadStatsData();
            if (statsInterval) clearInterval(statsInterval);
            statsInterval = setInterval(loadStatsData, 2000);
        };
    }

    if (closeStatsBtn && statsModal) {
        closeStatsBtn.onclick = () => {
            statsModal.style.display = 'none';
            if (statsInterval) clearInterval(statsInterval);
        };
    }

    if (refreshStatsBtn) {
        refreshStatsBtn.onclick = () => loadStatsData();
    }

    if (resetStatsBtn) {
        resetStatsBtn.onclick = async () => {
            const confirmMsg = currentLang === 'ko' ? '서버 시작 이후의 모든 누적 통계를 초기화하시겠습니까?' : 'Reset all cumulative traffic statistics?';
            if (!confirm(confirmMsg)) return;
            try {
                const res = await fetch('/api/stats/reset', { method: 'POST' });
                if (res.ok) {
                    const data = await res.json();
                    renderStatsDashboard(data.stats);
                    if (window.addLog) window.addLog('System', currentLang === 'ko' ? '통계 기록이 초기화되었습니다.' : 'Statistics reset.');
                }
            } catch (e) {
                console.error('Stats reset error:', e);
            }
        };
    }

