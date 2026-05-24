// --- ARD Control Center: Robot3Axis Tab Module ---
document.addEventListener('DOMContentLoaded', () => {
    let r3Scene, r3Camera, r3Renderer, r3Clock;
    let r3Base, r3Link1, r3Link2, r3GripperL, r3GripperR;
    let r3AnimationId;
    let isR3SimRunning = false;

    let currentR3X = 0, currentR3Y = 0, currentR3Z = 0;
    let targetR3X = 0, targetR3Y = 0, targetR3Z = 0;

    let robotChart = null;
    let sequenceSteps = [];

    // --- Chart.js Setup ---
    const initRobotChart = () => {
        const chartEl = document.getElementById('robot3axis-chart');
        if (!chartEl) return;
        const ctx = chartEl.getContext('2d');
        
        robotChart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: Array(30).fill(''),
                datasets: [
                    {
                        label: 'X (Rot °)',
                        data: Array(30).fill(0),
                        borderColor: '#f43f5e',
                        borderWidth: 2,
                        fill: false,
                        tension: 0.3,
                        pointRadius: 0
                    },
                    {
                        label: 'Y (Vert mm)',
                        data: Array(30).fill(0),
                        borderColor: '#3b82f6',
                        borderWidth: 2,
                        fill: false,
                        tension: 0.3,
                        pointRadius: 0
                    },
                    {
                        label: 'Z (Horiz mm)',
                        data: Array(30).fill(0),
                        borderColor: '#10b981',
                        borderWidth: 2,
                        fill: false,
                        tension: 0.3,
                        pointRadius: 0
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                animation: false,
                scales: {
                    x: { display: false },
                    y: { 
                        grid: { color: 'rgba(255,255,255,0.05)' },
                        ticks: { color: '#94a3b8', font: { size: 9 } }
                    }
                },
                plugins: {
                    legend: {
                        labels: { color: '#f8fafc', font: { size: 9 }, boxWidth: 8 }
                    }
                }
            }
        });
    };

    const updateRobotChart = (x, y, z) => {
        if (!robotChart) initRobotChart();
        if (!robotChart) return;
        const data = robotChart.data;
        data.datasets[0].data.shift(); data.datasets[0].data.push(x);
        data.datasets[1].data.shift(); data.datasets[1].data.push(y);
        data.datasets[2].data.shift(); data.datasets[2].data.push(z);
        robotChart.update();
    };

    // --- Three.js Robot 3D Simulation Setup ---
    const initRobot3AxisSim = () => {
        const container = document.getElementById('robot3axis-three-container');
        if (!container || r3Scene) return;

        r3Scene = new THREE.Scene();
        r3Clock = new THREE.Clock();

        r3Camera = new THREE.PerspectiveCamera(45, container.clientWidth / container.clientHeight, 0.1, 1000);
        r3Camera.position.set(12, 10, 15);
        r3Camera.lookAt(0, 3, 0);

        r3Renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
        r3Renderer.setPixelRatio(window.devicePixelRatio);
        r3Renderer.setSize(container.clientWidth, container.clientHeight);
        r3Renderer.setClearColor(0x000000, 0);
        container.appendChild(r3Renderer.domElement);

        // Lighting
        const ambLight = new THREE.AmbientLight(0xffffff, 1.3);
        r3Scene.add(ambLight);

        const dLight = new THREE.DirectionalLight(0x38bdf8, 1.5);
        dLight.position.set(5, 10, 5);
        r3Scene.add(dLight);

        // Materials
        const matBase = new THREE.MeshPhongMaterial({ color: 0x1e293b, shininess: 30 });
        const matLink1 = new THREE.MeshPhongMaterial({ color: 0x3b82f6, shininess: 50 }); // Y axis blue
        const matLink2 = new THREE.MeshPhongMaterial({ color: 0x10b981, shininess: 50 }); // Z axis green
        const matGripper = new THREE.MeshPhongMaterial({ color: 0xf43f5e, shininess: 40 }); // Gripper red

        const addOutlines = (mesh, color = 0xffffff) => {
            const edges = new THREE.EdgesGeometry(mesh.geometry);
            const line = new THREE.LineSegments(edges, new THREE.LineBasicMaterial({ color: color, transparent: true, opacity: 0.4 }));
            mesh.add(line);
        };

        // --- Build Robotic Arm Assemblies ---
        // 1. Base (Rotates on Y axis)
        r3Base = new THREE.Group();
        const baseMesh = new THREE.Mesh(new THREE.CylinderGeometry(2, 2.3, 0.8, 24), matBase);
        addOutlines(baseMesh, 0x64748b);
        r3Base.add(baseMesh);
        r3Scene.add(r3Base);

        // Vertical Guide Column
        const colMesh = new THREE.Mesh(new THREE.CylinderGeometry(0.4, 0.4, 8, 16), matBase);
        colMesh.position.y = 4;
        addOutlines(colMesh, 0x64748b);
        r3Base.add(colMesh);

        // 2. Link 1 (Vertical Elevator - moves up/down)
        r3Link1 = new THREE.Group();
        const elevatorMesh = new THREE.Mesh(new THREE.BoxGeometry(1.2, 1, 1.2), matLink1);
        addOutlines(elevatorMesh, 0xffffff);
        r3Link1.add(elevatorMesh);
        r3Base.add(r3Link1); // child of base

        // 3. Link 2 (Horizontal Extender - moves forward/backward)
        r3Link2 = new THREE.Group();
        const armMesh = new THREE.Mesh(new THREE.BoxGeometry(0.5, 0.4, 4), matLink2);
        armMesh.position.z = -1; // offset
        addOutlines(armMesh, 0xffffff);
        r3Link2.add(armMesh);
        r3Link1.add(r3Link2); // child of elevator (Link1)

        // 4. End Effector / Gripper
        const gripperBase = new THREE.Mesh(new THREE.BoxGeometry(0.8, 0.5, 0.3), matGripper);
        gripperBase.position.z = -3;
        addOutlines(gripperBase, 0xffffff);
        r3Link2.add(gripperBase);

        r3GripperL = new THREE.Mesh(new THREE.BoxGeometry(0.15, 0.4, 0.8), matGripper);
        r3GripperL.position.set(-0.3, 0, -3.4);
        addOutlines(r3GripperL, 0xffffff);
        r3Link2.add(r3GripperL);

        r3GripperR = new THREE.Mesh(new THREE.BoxGeometry(0.15, 0.4, 0.8), matGripper);
        r3GripperR.position.set(0.3, 0, -3.4);
        addOutlines(r3GripperR, 0xffffff);
        r3Link2.add(r3GripperR);

        // Grid/Floor
        const grid = new THREE.GridHelper(30, 15, 0x334155, 0x1e293b);
        grid.position.y = -0.4;
        r3Scene.add(grid);

        window.addEventListener('resize', onR3Resize);
    };

    const onR3Resize = () => {
        const container = document.getElementById('robot3axis-three-container');
        if (!container || !r3Renderer || !r3Camera) return;
        r3Camera.aspect = container.clientWidth / container.clientHeight;
        r3Camera.updateProjectionMatrix();
        r3Renderer.setSize(container.clientWidth, container.clientHeight);
    };

    const animateRobot3AxisSim = () => {
        r3AnimationId = requestAnimationFrame(animateRobot3AxisSim);
        if (!r3Clock) return;
        const delta = r3Clock.getDelta();

        // Smooth Lerp positions
        currentR3X = THREE.MathUtils.lerp(currentR3X, targetR3X, delta * 5);
        currentR3Y = THREE.MathUtils.lerp(currentR3Y, targetR3Y, delta * 5);
        currentR3Z = THREE.MathUtils.lerp(currentR3Z, targetR3Z, delta * 5);

        // Snap values if extremely close
        if (Math.abs(currentR3X - targetR3X) < 1e-4) currentR3X = targetR3X;
        if (Math.abs(currentR3Y - targetR3Y) < 1e-4) currentR3Y = targetR3Y;
        if (Math.abs(currentR3Z - targetR3Z) < 1e-4) currentR3Z = targetR3Z;

        // Apply transformations
        // Base Rotation (X Axis of robot corresponds to Y rotation in 3D scene)
        if (r3Base) {
            r3Base.rotation.y = THREE.MathUtils.degToRad(currentR3X);
        }

        // Elevator (Y Axis of robot corresponds to vertical Y position in 3D scene)
        // Let's map 0-100mm robot Y to 1-7 units vertical height in 3D
        if (r3Link1) {
            r3Link1.position.y = 1 + (currentR3Y / 100) * 6;
        }

        // Extender (Z Axis of robot corresponds to horizontal Z position in 3D scene)
        // Let's map 0-100mm robot Z to 0-2 units forward slide (offset)
        if (r3Link2) {
            r3Link2.position.z = -(currentR3Z / 100) * 2;
        }

        // Render scene
        if (r3Renderer && r3Scene && r3Camera) {
            r3Renderer.render(r3Scene, r3Camera);
        }

        // Chart Update throttle (~10Hz)
        const now = performance.now();
        if (!window.lastRobotChartUpdate || now - window.lastRobotChartUpdate > 100) {
            updateRobotChart(currentR3X, currentR3Y, currentR3Z);
            window.lastRobotChartUpdate = now;
        }
    };

    window.startRobot3AxisSim = () => {
        if (!r3Scene) {
            initRobot3AxisSim();
            initRobotChart();
        }
        if (!isR3SimRunning) {
            isR3SimRunning = true;
            r3Clock?.start();
            animateRobot3AxisSim();
            setTimeout(onR3Resize, 20);
        }
    };

    window.stopRobot3AxisSim = () => {
        isR3SimRunning = false;
        cancelAnimationFrame(r3AnimationId);
    };

    // --- SSE Status Sync & HUD update ---
    window.updateRobot3AxisHUD = () => {
        if (!window.currentRobot3AxisState) return;
        const s = window.currentRobot3AxisState;

        // Update targets for Sim
        targetR3X = s.x_deg;
        targetR3Y = s.y_mm;
        targetR3Z = s.z_mm;

        // Update HUD
        const hX = document.getElementById('hud-r3-x');
        const hY = document.getElementById('hud-r3-y');
        const hZ = document.getElementById('hud-r3-z');
        if (hX) hX.textContent = s.x_deg.toFixed(1);
        if (hY) hY.textContent = s.y_mm.toFixed(1);
        if (hZ) hZ.textContent = s.z_mm.toFixed(1);

        // Update Gripper state slider/text
        const grabToggle = document.getElementById('robot3axis-grab-toggle');
        const grabText = document.getElementById('grab-status-text');
        
        if (grabToggle && grabText) {
            const isGrabbed = s.grab === 1;
            grabToggle.checked = isGrabbed;
            if (isGrabbed) {
                grabText.innerHTML = 'Status: <span style="color: #ef4444; font-weight: bold;">OPENED (GRABBED)</span>';
                if (r3GripperL && r3GripperR) {
                    r3GripperL.position.x = -0.55;
                    r3GripperR.position.x = 0.55;
                }
            } else {
                grabText.innerHTML = 'Status: <span style="color: #10b981; font-weight: bold;">CLOSED (DEFAULT)</span>';
                if (r3GripperL && r3GripperR) {
                    r3GripperL.position.x = -0.3;
                    r3GripperR.position.x = 0.3;
                }
            }
        }

        // Update Command Queue UI
        updateR3QueueUI(s.queue || [], s.active_cmd || null, s.execution_percent || 0, s.execution_elapsed || 0);
    };

    // Command Queue UI Renderer
    const updateR3QueueUI = (queue, activeCmd, percent, elapsed) => {
        const container = document.getElementById('r3-command-queue');
        const ring = document.getElementById('robot3axis-execution-ring');
        if (!container) return;

        container.innerHTML = '';

        // Active Ring handler
        if (activeCmd) {
            if (ring) {
                ring.style.opacity = '1';
                ring.style.pointerEvents = 'auto';
            }
            const progRing = document.getElementById('r3-ring-progress');
            const ringTime = document.getElementById('r3-ring-time');
            const ringCmd = document.getElementById('r3-ring-cmd');
            
            if (progRing) {
                // Circumference is 2 * PI * r = 2 * 3.14159 * 45 = 282.74
                const strokeDashOffset = 282.74 - (percent / 100) * 282.74;
                progRing.style.strokeDashoffset = strokeDashOffset;
            }
            if (ringTime) ringTime.textContent = elapsed.toFixed(2) + 's';
            if (ringCmd) ringCmd.textContent = activeCmd.type || 'MOVE';

            // Insert active item into UI
            const activeItem = document.createElement('div');
            activeItem.className = 'queue-item active-executing';
            activeItem.innerHTML = `
                <div style="font-weight: bold; font-size: 0.75rem;">⚡ EXECUTING: ${activeCmd.type}</div>
                <div style="font-size: 0.65rem; opacity: 0.8; margin-top: 2px;">Coords: X(${activeCmd.x}), Y(${activeCmd.y}), Z(${activeCmd.z}) | Progress: ${percent.toFixed(1)}%</div>
            `;
            container.appendChild(activeItem);
        } else {
            if (ring) {
                ring.style.opacity = '0';
                ring.style.pointerEvents = 'none';
            }
        }

        // Queued items
        if (queue.length === 0 && !activeCmd) {
            container.innerHTML = '<div style="font-size: 0.7rem; color: var(--text-dim); text-align: center; padding: 10px; font-style: italic;">Command Queue Empty</div>';
            return;
        }

        queue.forEach((item, idx) => {
            const qItem = document.createElement('div');
            qItem.className = 'queue-item';
            qItem.innerHTML = `
                <div style="font-weight: 600; font-size: 0.7rem; color: var(--primary);">#${idx+1} [PENDING] ${item.type}</div>
                <div style="font-size: 0.65rem; color: var(--text-dim); margin-top: 1px;">X:${item.x} | Y:${item.y} | Z:${item.z} | Wait:${item.wait}s</div>
            `;
            container.appendChild(qItem);
        });
    };

    // PTP Move command trigger
    document.getElementById('robot3axis-ptp-move')?.addEventListener('click', async () => {
        const xVal = parseFloat(document.getElementById('ptp-x')?.value || 0);
        const yVal = parseFloat(document.getElementById('ptp-y')?.value || 0);
        const zVal = parseFloat(document.getElementById('ptp-z')?.value || 0);

        try {
            const res = await fetch('/api/robot3axis/command', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({
                    command: 'PTP',
                    x: xVal,
                    y: yVal,
                    z: zVal
                })
            });
            const data = await res.json();
            if (data.status === 'success') {
                if (window.addLog) window.addLog('Robot', `PTP Move command added to queue: X(${xVal}), Y(${yVal}), Z(${zVal})`);
            }
        } catch(e) {
            if (window.addLog) window.addLog('Error', 'Robot command failed: ' + e.message);
        }
    });

    document.getElementById('robot3axis-ptp-home-convenience')?.addEventListener('click', async () => {
        try {
            const res = await fetch('/api/robot3axis/command', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({
                    command: 'PTP',
                    x: 0,
                    y: 0,
                    z: 0
                })
            });
            const data = await res.json();
            if (data.status === 'success') {
                if (window.addLog) window.addLog('Robot', 'PTP Home command queued.');
            }
        } catch(e) {
            console.error(e);
        }
    });

    // Gripper grab toggle
    document.getElementById('robot3axis-grab-toggle')?.addEventListener('change', async (e) => {
        const grabState = e.target.checked ? 1 : 0;
        try {
            const res = await fetch('/api/robot3axis/command', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({
                    command: 'GRIPPER',
                    grab: grabState
                })
            });
            const data = await res.json();
            if (data.status === 'success') {
                if (window.addLog) window.addLog('Robot', `Gripper command queued: ${grabState === 1 ? 'OPEN (GRAB)' : 'CLOSE (RELEASE)'}`);
            }
        } catch(e) {
            console.error(e);
        }
    });

    // --- Sequence Control Logic ---
    const updateSequenceUI = () => {
        const list = document.getElementById('sequence-list');
        if (!list) return;

        list.innerHTML = '';
        if (sequenceSteps.length === 0) {
            list.innerHTML = `
                <tr id="empty-seq-msg">
                    <td colspan="5" style="padding: 20px; text-align: center; color: var(--text-dim); font-style: italic;">시퀀스가 비어 있습니다.</td>
                </tr>
            `;
            return;
        }

        sequenceSteps.forEach((step, idx) => {
            const row = document.createElement('tr');
            row.innerHTML = `
                <td style="padding: 8px; text-align: center; color: var(--text-dim);">${idx + 1}</td>
                <td style="padding: 8px; font-weight: 500;">X: ${step.x}° | Y: ${step.y}mm | Z: ${step.z}mm</td>
                <td style="padding: 8px; text-align: center;">${step.wait.toFixed(1)}s</td>
                <td style="padding: 8px; text-align: center; color: var(--primary); font-family: monospace;">~${step.est.toFixed(1)}s</td>
                <td style="padding: 8px; text-align: center;">
                    <button class="seq-del-btn" style="background: none; border: none; color: #ef4444; cursor: pointer; font-size: 1.1rem; padding: 0 5px;">&times;</button>
                </td>
            `;

            row.querySelector('.seq-del-btn').addEventListener('click', () => {
                sequenceSteps.splice(idx, 1);
                updateSequenceUI();
            });

            list.appendChild(row);
        });
    };

    document.getElementById('add-seq-step')?.addEventListener('click', () => {
        const xIn = document.getElementById('seq-in-x');
        const yIn = document.getElementById('seq-in-y');
        const zIn = document.getElementById('seq-in-z');
        const wIn = document.getElementById('seq-in-wait');

        if (!xIn || !yIn || !zIn || !wIn) return;

        const x = parseFloat(xIn.value || 0);
        const y = parseFloat(yIn.value || 0);
        const z = parseFloat(zIn.value || 0);
        const w = parseFloat(wIn.value || 1.0);

        // Estimate move time (0.05s per deg/mm + acceleration latency)
        let dx = 0, dy = 0, dz = 0;
        if (sequenceSteps.length > 0) {
            const prev = sequenceSteps[sequenceSteps.length - 1];
            dx = Math.abs(x - prev.x);
            dy = Math.abs(y - prev.y);
            dz = Math.abs(z - prev.z);
        } else {
            dx = Math.abs(x);
            dy = Math.abs(y);
            dz = Math.abs(z);
        }

        const estTime = Math.max(dx * 0.05, dy * 0.05, dz * 0.05) + 0.5 + w;

        sequenceSteps.push({
            x: x,
            y: y,
            z: z,
            wait: w,
            est: estTime
        });

        updateSequenceUI();
    });

    document.getElementById('clear-sequence-btn')?.addEventListener('click', () => {
        sequenceSteps = [];
        updateSequenceUI();
        if (window.addLog) window.addLog('Robot', '시퀀스 빌더가 초기화되었습니다.');
    });

    document.getElementById('run-sequence-btn')?.addEventListener('click', async () => {
        if (sequenceSteps.length === 0) {
            alert('시퀀스 단계를 최소 1개 이상 추가해 주세요.');
            return;
        }

        const btn = document.getElementById('run-sequence-btn');
        const statusBadge = document.getElementById('sequence-status-badge');
        
        if (btn) btn.disabled = true;
        if (statusBadge) {
            statusBadge.textContent = 'RUNNING';
            statusBadge.style.color = '#10b981';
            statusBadge.style.background = 'rgba(16, 185, 129, 0.1)';
            statusBadge.style.borderColor = 'rgba(16, 185, 129, 0.3)';
        }

        try {
            if (window.addLog) window.addLog('Robot', `연속 동작 시퀀스 실행 요청 중... (총 ${sequenceSteps.length}개 단계)`);
            const res = await fetch('/api/robot3axis/sequence', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ sequence: sequenceSteps })
            });
            const data = await res.json();
            if (data.status === 'success') {
                if (window.addLog) window.addLog('Robot', '시퀀스 모션 명령 리스트 큐 적재 완료!');
            }
        } catch (e) {
            if (window.addLog) window.addLog('Error', '시퀀스 실행 실패: ' + e.message);
        } finally {
            if (btn) btn.disabled = false;
            if (statusBadge) {
                statusBadge.textContent = 'READY';
                statusBadge.style.color = 'var(--text-dim)';
                statusBadge.style.background = 'rgba(255,255,255,0.05)';
                statusBadge.style.borderColor = 'rgba(255,255,255,0.1)';
            }
        }
    });
});
