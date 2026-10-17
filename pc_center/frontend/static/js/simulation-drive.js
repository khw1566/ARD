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
            
            // drive_update supplies the accepted command state; do not add it twice.
        });
        document.getElementById(`move-bw-btn-${axis}`)?.addEventListener('click', () => {
            const steps = parseInt(stepsInput.value || 200);
            const gearRatio = parseFloat(document.getElementById(`gear_ratio_${axis}`)?.value || 1.0);
            const motorSteps = Math.round(steps * gearRatio);
            sendMotorCmd(`${prefix}r-${motorSteps}`);
            
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
    let railBaseGroup, carriageGroup, leadScrewMesh, binMeshes = [];
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
        const container = document.getElementById('sorting-three-container') || document.getElementById('three-container');
        if (!container || scene) return;

        scene = new THREE.Scene();
        clock = new THREE.Clock();
        
        const width = container.clientWidth || 400;
        const height = container.clientHeight || 300;
        
        // 실물 사진(분류구조.jpg)과 일치하는 3/4 아이소메트릭 원거리 전체 조망 시점
        camera = new THREE.PerspectiveCamera(44, width / height, 0.1, 1000);
        camera.position.set(38, 34, 42);
        camera.lookAt(0, -1.5, 0);

        // --- Main Renderer ---
        renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
        renderer.setPixelRatio(window.devicePixelRatio);
        renderer.setSize(width, height);
        renderer.setClearColor(0x000000, 0); 
        container.appendChild(renderer.domElement);

        // --- Mini Camera Array ---
        const miniContainer = document.getElementById('mini-three-container');
        if (miniContainer) {
            miniCamera = new THREE.PerspectiveCamera(44, (miniContainer.clientWidth || 200) / 180, 0.1, 1000);
            miniCamera.position.set(38, 32, 40);
            miniCamera.lookAt(0, -1.5, 0);
        }

        // Lighting (현장 조명 및 기구 텍스처를 돋보이게 하는 다중 라이팅)
        const ambientLight = new THREE.AmbientLight(0xffffff, 1.4);
        scene.add(ambientLight);
        
        const dirLight = new THREE.DirectionalLight(0xffffff, 1.0);
        dirLight.position.set(20, 35, 20);
        scene.add(dirLight);

        const pointLight = new THREE.PointLight(0x38bdf8, 1.5, 100);
        pointLight.position.set(-15, 20, 15);
        scene.add(pointLight);

        // Groups
        railBaseGroup = new THREE.Group();
        scene.add(railBaseGroup);

        carriageGroup = new THREE.Group();
        scene.add(carriageGroup);

        // 상부 차동 구동계 (90도 회전 적용: 모터 2기 전후 배치, 덤핑 축이 좌우 박스 방향과 일치)
        const differentialGroup = new THREE.Group();
        differentialGroup.rotation.y = Math.PI / 2;
        carriageGroup.add(differentialGroup);
        
        yawGroup = new THREE.Group();
        yawGroup.position.y = 3.5; // Rotate at the actual shaft, not the world origin.
        carriageGroup.add(yawGroup);
        carrierGroup = new THREE.Group();
        yawGroup.add(carrierGroup); // Tilt in the already selected discharge direction.

        // Materials: 실물 '분류구조.jpg'와 완벽 일치 (White SLA/PLA 3D Printing Body)
        const matBodyWhite = new THREE.MeshStandardMaterial({ color: 0xf8fafc, roughness: 0.35, metalness: 0.1 });
        const matBevelBeige = new THREE.MeshStandardMaterial({ color: 0xd4a373, roughness: 0.4, metalness: 0.15 }); // 실물 기어 베이지톤
        const matMotorBody = new THREE.MeshStandardMaterial({ color: 0x1e293b, roughness: 0.5, metalness: 0.6 }); // NEMA17 블랙 바디
        const matMotorHeatsink = new THREE.MeshStandardMaterial({ color: 0x0f172a, roughness: 0.7, metalness: 0.8 }); // 블랙 방열판
        const matLinearRod = new THREE.MeshStandardMaterial({ color: 0xe2e8f0, roughness: 0.15, metalness: 0.9 }); // 크롬 연마봉
        const matLeadScrew = new THREE.MeshStandardMaterial({ color: 0x94a3b8, roughness: 0.3, metalness: 0.85 }); // T8 스크류
        const matTray = new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.25, metalness: 0.05 }); // 상단 원형 트레이
        const matBrassNut = new THREE.MeshStandardMaterial({ color: 0xf59e0b, roughness: 0.3, metalness: 0.8 }); // 황동 너트

        // Helper for outlines
        const addOutlines = (mesh, color = 0x64748b) => {
            const edges = new THREE.EdgesGeometry(mesh.geometry);
            const line = new THREE.LineSegments(edges, new THREE.LineBasicMaterial({ color: color, transparent: true, opacity: 0.4 }));
            mesh.add(line);
        };

        // --- 0. Linear Rail Structure (실물 사진 기반 Z축 평행 이송계) ---
        const railLength = 46; // 전체 이송 레일 길이
        const railSpacing = 12; // 듀얼 가이드 샤프트 간격 (X축 방향)
        const railHeight = -5.0;

        // 양측 크롬 리니어 연마봉 2조 (Z축 방향)
        const rodGeom = new THREE.CylinderGeometry(0.45, 0.45, railLength, 24);
        const rodLeft = new THREE.Mesh(rodGeom, matLinearRod);
        rodLeft.rotation.x = Math.PI / 2;
        rodLeft.position.set(-railSpacing / 2, railHeight, 0);
        addOutlines(rodLeft, 0x94a3b8);
        railBaseGroup.add(rodLeft);

        const rodRight = new THREE.Mesh(rodGeom, matLinearRod);
        rodRight.rotation.x = Math.PI / 2;
        rodRight.position.set(railSpacing / 2, railHeight, 0);
        addOutlines(rodRight, 0x94a3b8);
        railBaseGroup.add(rodRight);

        // 중앙 T8 리드스크류 (기하학 자체를 Z축 방향으로 눕혀 순수 Z축 자전 구현)
        const screwGeom = new THREE.CylinderGeometry(0.42, 0.42, railLength, 12);
        screwGeom.rotateX(Math.PI / 2); // 버텍스 좌표 자체를 Z축 방향으로 정렬
        leadScrewMesh = new THREE.Mesh(screwGeom, matLeadScrew);
        leadScrewMesh.position.set(0, railHeight, 0);
        
        // 스크류 회전 각인 에지 라인 (자전 시 회전 동작이 명확히 보이도록 처리)
        const screwEdges = new THREE.EdgesGeometry(screwGeom);
        const screwLines = new THREE.LineSegments(screwEdges, new THREE.LineBasicMaterial({
            color: 0x475569,
            transparent: true,
            opacity: 0.55
        }));
        leadScrewMesh.add(screwLines);
        railBaseGroup.add(leadScrewMesh);

        // 실물 프론트 엔드 지지 마운트 (White 3D Printed Bracket)
        const endSupportGeom = new THREE.BoxGeometry(railSpacing + 5.5, 3.5, 2.5);
        const frontEndSupport = new THREE.Mesh(endSupportGeom, matBodyWhite);
        frontEndSupport.position.set(0, railHeight + 0.5, railLength / 2);
        addOutlines(frontEndSupport, 0x94a3b8);
        railBaseGroup.add(frontEndSupport);

        // 실물 리어 엔드 마운트 + Z축 NEMA17 구동 모터
        const backEndSupport = new THREE.Mesh(endSupportGeom, matBodyWhite);
        backEndSupport.position.set(0, railHeight + 0.5, -railLength / 2);
        addOutlines(backEndSupport, 0x94a3b8);
        railBaseGroup.add(backEndSupport);

        // Z축 스테핑 모터 (실물 사진처럼 후방 중앙에 직결)
        const zMotor = new THREE.Mesh(new THREE.BoxGeometry(4.2, 4.2, 4.5), matMotorBody);
        zMotor.position.set(0, railHeight, -railLength / 2 - 2.8);
        addOutlines(zMotor, 0x38bdf8);
        railBaseGroup.add(zMotor);

        // 방열판 (Heatsink)
        const zMotorHeatsink = new THREE.Mesh(new THREE.BoxGeometry(4.0, 4.0, 1.2), matMotorHeatsink);
        zMotorHeatsink.position.set(0, railHeight, -railLength / 2 - 5.5);
        railBaseGroup.add(zMotorHeatsink);

        // --- Linear Carriage Slider (실물 메인 이송 브래킷 - White Body) ---
        // 4개의 LM 베어링 블록이 가이드봉을 감싸며 슬라이딩
        const carriageBaseGeom = new THREE.BoxGeometry(railSpacing + 5, 2.0, 18);
        const carriageBase = new THREE.Mesh(carriageBaseGeom, matBodyWhite);
        carriageBase.position.set(0, railHeight + 1.2, 0);
        addOutlines(carriageBase, 0x38bdf8);
        carriageGroup.add(carriageBase);

        // LM 베어링 블록 4개 (검은색 클램프)
        const bearingGeom = new THREE.BoxGeometry(2.4, 2.0, 3.6);
        const bPositions = [
            [-railSpacing / 2, railHeight, 5.0],
            [-railSpacing / 2, railHeight, -5.0],
            [railSpacing / 2, railHeight, 5.0],
            [railSpacing / 2, railHeight, -5.0]
        ];
        bPositions.forEach(pos => {
            const bMesh = new THREE.Mesh(bearingGeom, matMotorHeatsink);
            bMesh.position.set(...pos);
            addOutlines(bMesh, 0x94a3b8);
            carriageGroup.add(bMesh);
        });

        // 중앙 T8 황동 너트 플랜지
        const nutGeom = new THREE.CylinderGeometry(1.2, 1.2, 1.6, 16);
        nutGeom.rotateX(Math.PI / 2);
        const nutMesh = new THREE.Mesh(nutGeom, matBrassNut);
        nutMesh.position.set(0, railHeight, 0);
        carriageGroup.add(nutMesh);

        // --- 4-Compartment Sorting Bins (상단이 완전히 열려있는 투명 아크릴 수거 박스 4칸) ---
        // 레일 좌측: Bin 1 (금속/캔) & Bin 2 (유리/기타)
        // 레일 우측: Bin 3 (종이/팩) & Bin 4 (플라스틱/페트)
        const binWidth = 11;
        const binHeight = 6.0;
        const binDepth = 15;
        const binY = -4.5;
        const binOffsetX = 17.5;
        const binOffsetZ = 10.5;

        const binConfigs = [
            { name: "Metal (금속/캔)", color: 0xf59e0b, pos: [-binOffsetX, binY, binOffsetZ], yaw: 90, z: binOffsetZ },
            { name: "Others (유리/기타)", color: 0x94a3b8, pos: [-binOffsetX, binY, -binOffsetZ], yaw: 90, z: -binOffsetZ },
            { name: "Paper (종이/팩)", color: 0x10b981, pos: [binOffsetX, binY, binOffsetZ], yaw: 270, z: binOffsetZ },
            { name: "Plastic (플라스틱)", color: 0x3b82f6, pos: [binOffsetX, binY, -binOffsetZ], yaw: 270, z: -binOffsetZ }
        ];

        binMeshes = [];
        binConfigs.forEach(cfg => {
            const bGroup = new THREE.Group();
            
            // 반투명 아크릴 재질 (내부가 훤히 비치도록 DoubleSide & depthWrite: false 적용)
            const bMat = new THREE.MeshStandardMaterial({
                color: cfg.color,
                transparent: true,
                opacity: 0.32,
                roughness: 0.25,
                metalness: 0.1,
                side: THREE.DoubleSide,
                depthWrite: false
            });

            // 상단 개방형(Open Top) 5면체 바스켓 구조: 4면 벽체 + 1면 하단 바닥판 (상판 뚜껑 없음)
            const wallThickness = 0.35;
            const halfW = binWidth / 2;
            const halfD = binDepth / 2;

            // 1. 좌우 측벽 (Left / Right Walls)
            const sideWallGeom = new THREE.BoxGeometry(wallThickness, binHeight, binDepth);
            const leftWall = new THREE.Mesh(sideWallGeom, bMat);
            leftWall.position.set(-halfW + wallThickness / 2, 0, 0);
            bGroup.add(leftWall);

            const rightWall = new THREE.Mesh(sideWallGeom, bMat);
            rightWall.position.set(halfW - wallThickness / 2, 0, 0);
            bGroup.add(rightWall);

            // 2. 전후 측벽 (Front / Back Walls)
            const frontBackGeom = new THREE.BoxGeometry(binWidth - wallThickness * 2, binHeight, wallThickness);
            const frontWall = new THREE.Mesh(frontBackGeom, bMat);
            frontWall.position.set(0, 0, halfD - wallThickness / 2);
            bGroup.add(frontWall);

            const backWall = new THREE.Mesh(frontBackGeom, bMat);
            backWall.position.set(0, 0, -halfD + wallThickness / 2);
            bGroup.add(backWall);

            // 3. 하단 바닥판 (Bottom Plate)
            const bottomGeom = new THREE.BoxGeometry(binWidth, wallThickness, binDepth);
            const bottomMesh = new THREE.Mesh(bottomGeom, bMat);
            bottomMesh.position.set(0, -binHeight / 2 + wallThickness / 2, 0);
            bGroup.add(bottomMesh);

            // 상단 개방 립 테두리 라인 (Top Open Rim - 상단 입구가 열려있음을 직관적으로 강조)
            const rimPoints = [
                new THREE.Vector3(-halfW, binHeight / 2, -halfD),
                new THREE.Vector3(halfW, binHeight / 2, -halfD),
                new THREE.Vector3(halfW, binHeight / 2, halfD),
                new THREE.Vector3(-halfW, binHeight / 2, halfD),
                new THREE.Vector3(-halfW, binHeight / 2, -halfD)
            ];
            const rimGeom = new THREE.BufferGeometry().setFromPoints(rimPoints);
            const rimLine = new THREE.Line(rimGeom, new THREE.LineBasicMaterial({
                color: cfg.color,
                transparent: true,
                opacity: 0.9,
                linewidth: 2
            }));
            bGroup.add(rimLine);

            // 바닥 및 외곽 윤곽 라인
            const outerEdges = new THREE.EdgesGeometry(new THREE.BoxGeometry(binWidth, binHeight, binDepth));
            const wireframe = new THREE.LineSegments(outerEdges, new THREE.LineBasicMaterial({
                color: cfg.color,
                transparent: true,
                opacity: 0.35
            }));
            bGroup.add(wireframe);

            bGroup.position.set(...cfg.pos);
            railBaseGroup.add(bGroup);
            binMeshes.push({ group: bGroup, config: cfg, mesh: leftWall, mat: bMat });
        });

        // 1. 차동 모터 2기 및 실물 수직 마운트 (White Upright Brackets + Heatsinks)
        // 실물 기구 사진처럼 상단 트레이 밑면과의 간섭을 완전히 방지하도록 높이와 위치 최적화
        const uprightMountGeom = new THREE.BoxGeometry(2.0, 5.8, 7.5);
        const uprightLeft = new THREE.Mesh(uprightMountGeom, matBodyWhite);
        uprightLeft.position.set(-5.6, 2.1, 0);
        addOutlines(uprightLeft, 0x94a3b8);
        differentialGroup.add(uprightLeft);

        const uprightRight = new THREE.Mesh(uprightMountGeom, matBodyWhite);
        uprightRight.position.set(5.6, 2.1, 0);
        addOutlines(uprightRight, 0x94a3b8);
        differentialGroup.add(uprightRight);

        // NEMA17 스테핑 모터 2기 (X축 좌우 대칭 배치)
        const motorBoxGeom = new THREE.BoxGeometry(4.2, 4.2, 4.2);
        motorX = new THREE.Group();
        const mXFrame = new THREE.Mesh(motorBoxGeom, matMotorBody);
        addOutlines(mXFrame, 0x94a3b8);
        const mXHeat = new THREE.Mesh(new THREE.BoxGeometry(1.0, 3.8, 3.8), matMotorHeatsink);
        mXHeat.position.x = -2.5;
        motorX.add(mXFrame); motorX.add(mXHeat);
        motorX.position.set(-8.2, 3.5, 0);
        differentialGroup.add(motorX);

        motorY = new THREE.Group();
        const mYFrame = new THREE.Mesh(motorBoxGeom, matMotorBody);
        addOutlines(mYFrame, 0x94a3b8);
        const mYHeat = new THREE.Mesh(new THREE.BoxGeometry(1.0, 3.8, 3.8), matMotorHeatsink);
        mYHeat.position.x = 2.5;
        motorY.add(mYFrame); motorY.add(mYHeat);
        motorY.position.set(8.2, 3.5, 0);
        differentialGroup.add(motorY);

        // 2. 실물 베이지톤 입력 베벨 기어 2기 (Module 1.5 Bevel Gears)
        // 모터 높이에 맞추고 직경을 실물 비율(반지름 2.3)로 조정하여 상단 트레이 겹침 완전 방지
        const bevelGearGeom = new THREE.CylinderGeometry(1.2, 2.3, 1.4, 28);
        gearX = new THREE.Mesh(bevelGearGeom, matBevelBeige);
        addOutlines(gearX, 0xb08968);
        gearX.rotation.z = -Math.PI / 2;
        gearX.position.set(-3.2, 3.5, 0);
        differentialGroup.add(gearX);

        gearY = new THREE.Mesh(bevelGearGeom, matBevelBeige);
        addOutlines(gearY, 0xb08968);
        gearY.rotation.z = Math.PI / 2;
        gearY.position.set(3.2, 3.5, 0);
        differentialGroup.add(gearY);

        // 3. 중심 회전축 (Center Transverse Shaft)
        const hShaft = new THREE.Mesh(new THREE.CylinderGeometry(0.4, 0.4, 10, 16), matLinearRod);
        hShaft.rotation.z = Math.PI / 2;
        hShaft.position.set(0, 3.5, 0);
        addOutlines(hShaft, 0xffffff);
        differentialGroup.add(hShaft);

        // 4. 종동 베벨 기어 (Spider / Output Gear - Bevel Beige)
        bottomSpider = new THREE.Mesh(bevelGearGeom, matBevelBeige);
        addOutlines(bottomSpider, 0xb08968);
        bottomSpider.position.set(0, 0, 0);
        carrierGroup.add(bottomSpider);

        // 5. 상단 수직 스핀들 및 턴테이블 트레이 (White Turntable Spindle & Dish Tray)
        // 스핀들이 기어 윗면 위로 충분히 솟아올라 트레이와 기어 간 간섭을 원천 차단
        const spindleGeom = new THREE.CylinderGeometry(1.8, 2.0, 2.6, 24);
        const spindle = new THREE.Mesh(spindleGeom, matBodyWhite);
        spindle.position.set(0, 2.3, 0);
        addOutlines(spindle, 0x94a3b8);
        carrierGroup.add(spindle);

        // 실물 원형 투입/선별 트레이 (가장자리 턱이 있는 접시 형태, Y=7.5에 안전 배치)
        const diskGeom = new THREE.CylinderGeometry(8.5, 8.2, 0.8, 36);
        disk = new THREE.Mesh(diskGeom, matTray);
        addOutlines(disk, 0x38bdf8);
        disk.position.set(0, 4.0, 0);
        carrierGroup.add(disk);

        // Grid/Floor
        const grid = new THREE.GridHelper(60, 24, 0x334155, 0x1e293b);
        grid.position.y = -7.5;
        scene.add(grid);

        window.addEventListener('resize', onWindowResize);

        // --- 마우스 인터랙티브 궤도 회전 및 휠 줌 (Orbit & Zoom) 컨트롤 ---
        let isOrbiting = false;
        let prevMouse = { x: 0, y: 0 };
        const dom = renderer.domElement;
        
        dom.style.cursor = 'grab';
        dom.addEventListener('mousedown', (e) => {
            isOrbiting = true;
            prevMouse = { x: e.clientX, y: e.clientY };
            dom.style.cursor = 'grabbing';
        });

        window.addEventListener('mouseup', () => {
            isOrbiting = false;
            if (dom) dom.style.cursor = 'grab';
        });

        window.addEventListener('mousemove', (e) => {
            if (!isOrbiting || !camera) return;
            const deltaX = (e.clientX - prevMouse.x) * 0.007;
            const deltaY = (e.clientY - prevMouse.y) * 0.007;
            prevMouse = { x: e.clientX, y: e.clientY };

            const target = new THREE.Vector3(0, -1.5, 0);
            const offset = camera.position.clone().sub(target);
            
            let radius = offset.length();
            let theta = Math.atan2(offset.x, offset.z) + deltaX;
            let phi = Math.acos(THREE.MathUtils.clamp(offset.y / radius, -1, 1)) - deltaY;
            phi = THREE.MathUtils.clamp(phi, 0.15, Math.PI / 2 - 0.02);

            offset.x = radius * Math.sin(phi) * Math.sin(theta);
            offset.y = radius * Math.cos(phi);
            offset.z = radius * Math.sin(phi) * Math.cos(theta);

            camera.position.copy(target).add(offset);
            camera.lookAt(target);
        });

        dom.addEventListener('wheel', (e) => {
            e.preventDefault();
            if (!camera) return;
            const target = new THREE.Vector3(0, -1.5, 0);
            const offset = camera.position.clone().sub(target);
            let radius = offset.length();
            radius += e.deltaY * 0.06;
            radius = THREE.MathUtils.clamp(radius, 20, 140);
            offset.setLength(radius);
            camera.position.copy(target).add(offset);
            camera.lookAt(target);
        }, { passive: false });
        
        // Initial setup for the correct tab
        setTimeout(onWindowResize, 10);
    };

    const onWindowResize = () => {
        if (!renderer) return;
        
        const activeTab = document.querySelector('.tab-btn.active') || document.querySelector('.tab-content.active');
        const activeTabId = activeTab ? activeTab.id || activeTab.dataset?.tab : '';
        const isTxTab = activeTabId === 'imagesendermcu-tab' || activeTabId === 'imagesendermcu-box';
        const isSortingTab = activeTabId === 'sorting-tab' || activeTabId === 'sorting-control-box';
        
        if (isSortingTab) {
            // Render inside Sorting Dashboard Container
            const sortContainer = document.getElementById('sorting-three-container');
            if (sortContainer && sortContainer.clientWidth > 0 && sortContainer.clientHeight > 0 && camera) {
                if (renderer.domElement.parentElement !== sortContainer) {
                    sortContainer.appendChild(renderer.domElement);
                }
                camera.aspect = sortContainer.clientWidth / sortContainer.clientHeight;
                camera.updateProjectionMatrix();
                renderer.setSize(sortContainer.clientWidth, sortContainer.clientHeight);
            }
        } else if (isTxTab) {
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
        const blend = Math.min(delta * 5, 1);
        
        // Lerp gear angles towards targets (smooth animation)
        gearAngleX = THREE.MathUtils.lerp(gearAngleX, targetAngleX, blend);
        gearAngleY = THREE.MathUtils.lerp(gearAngleY, targetAngleY, blend);
        
        // 미세 부동소수점 오차로 인한 그래프 깨짐 현상(1e-21 레벨) 방지용 스냅 로직 추가
        if (Math.abs(gearAngleX - targetAngleX) < 1e-4) gearAngleX = targetAngleX;
        if (Math.abs(gearAngleY - targetAngleY) < 1e-4) gearAngleY = targetAngleY;
        
        // Input gears rotate around X axis
        if (gearX) gearX.rotation.x = gearAngleX;
        if (gearY) gearY.rotation.x = gearAngleY;
        
        // Z axis (Rail) logic - smooth transition
        gearAngleZ = THREE.MathUtils.lerp(gearAngleZ, targetAngleZ, blend);
        if (Math.abs(gearAngleZ - targetAngleZ) < 1e-4) gearAngleZ = targetAngleZ;

        // Lead screw spins with motor Z (Z축 길이방향 순수 축 자전)
        if (leadScrewMesh) leadScrewMesh.rotation.z = -gearAngleZ * 4;

        // Carriage slider moves physically along Z rail track (-16 to +16 units)
        // 1000 steps = 1800 deg = 30mm travel => map to 3D units
        const carriageZPos = (gearAngleZ / (Math.PI * 10)) * 10.5;
        if (carriageGroup) carriageGroup.position.z = THREE.MathUtils.clamp(carriageZPos, -18, 18);

        // Mechanism Math
        // Report: opposite motor signs tilt the tray (Roll).
        const rollDelta = (gearAngleX - gearAngleY) / 2;
        if (carrierGroup) carrierGroup.rotation.x = rollDelta;

        // Same motor signs select the discharge bearing (Yaw).
        const yawDelta = (gearAngleX + gearAngleY) / 2;
        // Clockwise bearings: 90 degrees faces the bins at negative world X.
        if (yawGroup) yawGroup.rotation.y = -yawDelta;

        // Predict State
        predictedState.mx = THREE.MathUtils.radToDeg(gearAngleX);
        predictedState.my = THREE.MathUtils.radToDeg(gearAngleY);
        predictedState.roll = THREE.MathUtils.radToDeg(rollDelta);
        predictedState.yaw = THREE.MathUtils.radToDeg(yawDelta);
        
        predictedState.mz = THREE.MathUtils.radToDeg(gearAngleZ);
        // Rail 이동 범위: 145mm ~ 175mm (30mm 가동 범위)
        // 1000 steps * 1.8 deg = 1800 deg.
        predictedState.z_mm = 145.0 + (predictedState.mz / 1800.0) * 30.0;

        // Dynamic Bin Highlight Check (타겟 수거함에 캐리지와 선회각이 정렬되었을 때 은은하게 하이라이트)
        if (binMeshes && binMeshes.length > 0) {
            const currentYawDeg = ((predictedState.yaw % 360) + 360) % 360;
            binMeshes.forEach(b => {
                const zDist = Math.abs((carriageGroup ? carriageGroup.position.z : 0) - b.config.z);
                const isNearZ = zDist < 4.0;
                // Check if yaw matches bin direction (approx within 30 deg)
                const yawDiff = Math.abs(currentYawDeg - b.config.yaw);
                const isMatchingYaw = yawDiff < 35 || Math.abs(yawDiff - 360) < 35;
                const targetMat = b.mat || (b.mesh ? b.mesh.material : null);
                if (targetMat) {
                    if (isNearZ && isMatchingYaw && Math.abs(predictedState.roll) > 5) {
                        targetMat.opacity = 0.75;
                        targetMat.emissive = new THREE.Color(b.config.color);
                        targetMat.emissiveIntensity = 0.35;
                    } else {
                        targetMat.opacity = 0.38;
                        targetMat.emissive = new THREE.Color(0x000000);
                        targetMat.emissiveIntensity = 0;
                    }
                }
            });
        }

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
        
        // Render the server's drive_update rather than a second local increment.
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

    // Window global exports for Sorting Dashboard synchronization
    window.startSim = startSim;
    window.stopSim = stopSim;
    window.onWindowResize = onWindowResize;

    window.setSortingAngles = (yawDeg, rollDeg, railZMm = 0) => {
        // yaw: rotation around Y (speedX = +rad, speedY = +rad)
        // roll: tilt around X (speedX = +rad, speedY = -rad)
        const yawRad = (yawDeg * Math.PI) / 180.0;
        const rollRad = (rollDeg * Math.PI) / 180.0;
        
        targetAngleX = rollRad + yawRad;
        targetAngleY = yawRad - rollRad;

        // Open-loop rail displacement is relative to the sorting start center.
        const normalizedZMm = railZMm;

        // ±15mm 가동 범위를 3D 이송 스크류 회전 각도(targetAngleZ)에 선형 매핑
        // 15mm 이동 시 약 10.5 3D 단위 이동
        targetAngleZ = (normalizedZMm / 15.0) * (Math.PI * 10);
    };

    window.resetSortingAngles = () => {
        targetAngleX = 0;
        targetAngleY = 0;
        targetAngleZ = 0;
    };


