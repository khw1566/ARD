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
        if (window.BoardUI && !window.BoardUI.isTabEnabled('robot3axismcu-tab')) return;
        
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
    let r3NextTimer = null;
    let r3QueueGeneration = 0;

    function addR3Command(name, estTime, apiData) {
        if (window.BoardUI && !window.BoardUI.isTabEnabled('robot3axismcu-tab')) return;
        const cmdId = Date.now() + Math.random();
        r3CommandQueue.push({ id: cmdId, name, estTime, apiData });
        updateR3QueueUI();
        if (!r3IsExecuting) {
            processNextR3Command();
        }
    }

    async function processNextR3Command() {
        if (window.BoardUI && !window.BoardUI.isTabEnabled('robot3axismcu-tab')) {
            window.onRobotBoardDisabled();
            return;
        }
        const generation = r3QueueGeneration;
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

        // A disabled board must not resume an old queue after re-enabling.
        if (generation !== r3QueueGeneration) return;
        // Wait for estimated time before next command
        r3NextTimer = setTimeout(() => {
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


    window.onRobotBoardDisabled = function() {
        r3QueueGeneration++;
        r3CommandQueue = [];
        r3IsExecuting = false;
        clearTimeout(r3NextTimer);
        clearTimeout(r3StatusTimer);
        clearInterval(r3StatusInterval);
        if (isSequenceRunning) stopSequence();
        document.getElementById('robot3axis-execution-ring')?.classList.remove('active');
        updateR3QueueUI();
    };
