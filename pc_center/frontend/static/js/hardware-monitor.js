    // --- 제어 PC 하드웨어 & 네트워크 실시간 모니터링 (Task Manager Style Charts) ---
    const hwModal = document.getElementById('hw-modal');
    const openHwBtn = document.getElementById('hw-modal-btn');
    const closeHwBtn = document.getElementById('close-hw-modal');
    const refreshHwBtn = document.getElementById('hw-refresh-btn');
    let hwInterval = null;

    let chartCpu = null;
    let chartRam = null;
    let chartGpu = null;
    let chartNet = null;

    function initHwCharts() {
        if (chartCpu) return; // 이미 초기화됨

        const commonChartOptions = (unit = '%', max = 100) => ({
            responsive: true,
            maintainAspectRatio: false,
            animation: false,
            interaction: { intersect: false, mode: 'index' },
            plugins: {
                legend: { display: false },
                tooltip: {
                    callbacks: {
                        label: (ctx) => `${ctx.dataset.label || ''}: ${ctx.parsed.y}${unit}`
                    }
                }
            },
            scales: {
                x: {
                    display: false
                },
                y: {
                    min: 0,
                    max: max,
                    grid: { color: 'rgba(255, 255, 255, 0.05)' },
                    ticks: {
                        color: '#94a3b8',
                        font: { size: 9 },
                        callback: (v) => v + unit
                    }
                }
            }
        });

        // 1. CPU Chart
        const ctxCpu = document.getElementById('hw-chart-cpu')?.getContext('2d');
        if (ctxCpu) {
            chartCpu = new Chart(ctxCpu, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [{
                        label: 'CPU',
                        data: [],
                        borderColor: '#38bdf8',
                        backgroundColor: 'rgba(56, 189, 248, 0.15)',
                        borderWidth: 2,
                        pointRadius: 0,
                        fill: true,
                        tension: 0.3
                    }]
                },
                options: commonChartOptions('%', 100)
            });
        }

        // 2. RAM Chart
        const ctxRam = document.getElementById('hw-chart-ram')?.getContext('2d');
        if (ctxRam) {
            chartRam = new Chart(ctxRam, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [{
                        label: 'RAM',
                        data: [],
                        borderColor: '#818cf8',
                        backgroundColor: 'rgba(129, 140, 248, 0.15)',
                        borderWidth: 2,
                        pointRadius: 0,
                        fill: true,
                        tension: 0.3
                    }]
                },
                options: commonChartOptions('%', 100)
            });
        }

        // 3. GPU Chart
        const ctxGpu = document.getElementById('hw-chart-gpu')?.getContext('2d');
        if (ctxGpu) {
            chartGpu = new Chart(ctxGpu, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [{
                        label: 'GPU',
                        data: [],
                        borderColor: '#34d399',
                        backgroundColor: 'rgba(52, 211, 153, 0.15)',
                        borderWidth: 2,
                        pointRadius: 0,
                        fill: true,
                        tension: 0.3
                    }]
                },
                options: commonChartOptions('%', 100)
            });
        }

        // 4. Network Chart
        const ctxNet = document.getElementById('hw-chart-net')?.getContext('2d');
        if (ctxNet) {
            const netOptions = commonChartOptions(' KB/s', undefined);
            netOptions.plugins.legend = {
                display: true,
                position: 'top',
                labels: { color: '#94a3b8', boxWidth: 10, font: { size: 9 } }
            };
            chartNet = new Chart(ctxNet, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [
                        {
                            label: 'Tx (Up)',
                            data: [],
                            borderColor: '#fbbf24',
                            backgroundColor: 'rgba(251, 191, 36, 0.1)',
                            borderWidth: 1.5,
                            pointRadius: 0,
                            fill: true,
                            tension: 0.3
                        },
                        {
                            label: 'Rx (Down)',
                            data: [],
                            borderColor: '#38bdf8',
                            backgroundColor: 'rgba(56, 189, 248, 0.1)',
                            borderWidth: 1.5,
                            pointRadius: 0,
                            fill: true,
                            tension: 0.3
                        }
                    ]
                },
                options: netOptions
            });
        }
    }

    async function loadHardwareData() {
        if (!hwModal || hwModal.style.display === 'none') return;
        try {
            const res = await fetch('/api/hardware/status');
            if (!res.ok) return;
            const data = await res.json();
            renderHardwareDashboard(data);
        } catch (e) {
            console.error('Hardware fetch error:', e);
        }
    }

    function renderHardwareDashboard(data) {
        initHwCharts();

        const latest = data.latest || {};
        const info = data.hardware_info || {};
        const history = data.history || {};

        // 1. Top KPI Summary Cards
        const elCpuVal = document.getElementById('hw-cpu-val');
        const elCpuSub = document.getElementById('hw-cpu-sub');
        const elRamVal = document.getElementById('hw-ram-val');
        const elRamSub = document.getElementById('hw-ram-sub');
        const elGpuVal = document.getElementById('hw-gpu-val');
        const elGpuSub = document.getElementById('hw-gpu-sub');
        const elNetVal = document.getElementById('hw-net-val');
        const elNetSub = document.getElementById('hw-net-sub');

        if (elCpuVal) elCpuVal.textContent = `${latest.cpu_percent || 0}%`;
        if (elCpuSub) elCpuSub.textContent = `${latest.cpu_freq_mhz || 0} MHz | ${info.cpu_cores_logical || 0} Cores (${info.cpu_cores_physical || 0}P)`;
        if (elRamVal) elRamVal.textContent = `${latest.ram_percent || 0}%`;
        if (elRamSub) elRamSub.textContent = `${latest.ram_used_gb || 0} / ${latest.ram_total_gb || 0} GB`;
        if (elGpuVal) elGpuVal.textContent = `${latest.gpu_percent || 0}%`;
        if (elGpuSub) elGpuSub.textContent = latest.gpu_name || '-';
        if (elNetVal) elNetVal.textContent = `↑ ${latest.net_tx_kbs || 0} KB/s | ↓ ${latest.net_rx_kbs || 0} KB/s`;
        if (elNetSub) elNetSub.textContent = (currentLang === 'ko' ? '총 누적: ' : 'Total: ') + `↑ ${latest.net_total_sent_mb || 0} MB | ↓ ${latest.net_total_recv_mb || 0} MB`;

        // Percent badges in chart headers
        const elCpuPct = document.getElementById('hw-cpu-chart-pct');
        const elRamPct = document.getElementById('hw-ram-chart-pct');
        const elGpuPct = document.getElementById('hw-gpu-chart-pct');
        if (elCpuPct) elCpuPct.textContent = `${latest.cpu_percent || 0}%`;
        if (elRamPct) elRamPct.textContent = `${latest.ram_percent || 0}%`;
        if (elGpuPct) elGpuPct.textContent = `${latest.gpu_percent || 0}%`;

        // 2. Charts Data Update (Past 60 Seconds History)
        const labels = history.labels || [];
        if (chartCpu && history.cpu) {
            chartCpu.data.labels = labels;
            chartCpu.data.datasets[0].data = history.cpu;
            chartCpu.update('none');
        }
        if (chartRam && history.ram) {
            chartRam.data.labels = labels;
            chartRam.data.datasets[0].data = history.ram;
            chartRam.update('none');
        }
        if (chartGpu && history.gpu) {
            chartGpu.data.labels = labels;
            chartGpu.data.datasets[0].data = history.gpu;
            chartGpu.update('none');
        }
        if (chartNet && history.net_tx && history.net_rx) {
            chartNet.data.labels = labels;
            chartNet.data.datasets[0].data = history.net_tx;
            chartNet.data.datasets[1].data = history.net_rx;
            chartNet.update('none');
        }

        // 3. System & Network Spec Details
        const elSpecCpu = document.getElementById('hw-spec-cpu-val');
        const elSpecRam = document.getElementById('hw-spec-ram-val');
        const elSpecGpu = document.getElementById('hw-spec-gpu-val');
        const elSpecNet = document.getElementById('hw-spec-net-val');

        if (elSpecCpu) elSpecCpu.textContent = `${info.cpu_name} (${info.cpu_cores_logical} 논리 코어, ${info.cpu_cores_physical} 물리 코어)`;
        if (elSpecRam) elSpecRam.textContent = `${info.ram_total_gb} GB Total (현재 사용: ${latest.ram_used_gb} GB, ${latest.ram_percent}%)`;
        if (elSpecGpu) elSpecGpu.textContent = `${info.gpu_name}`;
        if (elSpecNet) {
            const ifaces = latest.net_interfaces || {};
            const ifaceStrs = Object.entries(ifaces).map(([k, v]) => `${k}: ${v}`);
            elSpecNet.textContent = ifaceStrs.length > 0 ? ifaceStrs.join(' | ') : '인터페이스 감지 대기 중';
        }
    }

    if (openHwBtn && hwModal) {
        openHwBtn.onclick = () => {
            hwModal.style.display = 'block';
            setTimeout(initHwCharts, 50);
            loadHardwareData();
            if (hwInterval) clearInterval(hwInterval);
            hwInterval = setInterval(loadHardwareData, 1000); // 1초 주기 갱신
        };
    }

    if (closeHwBtn && hwModal) {
        closeHwBtn.onclick = () => {
            hwModal.style.display = 'none';
            if (hwInterval) clearInterval(hwInterval);
        };
    }

    if (refreshHwBtn) {
        refreshHwBtn.onclick = () => loadHardwareData();
    }

