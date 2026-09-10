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

