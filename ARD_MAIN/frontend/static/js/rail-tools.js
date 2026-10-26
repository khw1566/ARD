// Z rail measurement & leveling tools (drive tab). API: backend/api/rail_routes.py.
// The rail is ToF PID controlled and mounted mirrored since 2026-10-26: direction,
// steps/mm and the center / bin distances are measured on the rig here.
(() => {
    const $ = (id) => document.getElementById(id);
    if (!$('rail-tools-card')) return;

    const log = (msg) => { if (typeof window.addLog === 'function') window.addLog('System', `[Rail] ${msg}`); };
    const fmt = (v, digits = 1) => (typeof v === 'number' && Number.isFinite(v) ? v.toFixed(digits) : '-');
    const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
    const SLOT_NAMES = { home: '원점(홈)', center: '중앙', plus: '+측', minus: '-측' };
    const CATEGORIES = ['Metal', 'Glass', 'Others', 'Paper', 'Plastic'];
    const CATEGORY_NAMES = { Metal: '금속/캔', Glass: '유리', Others: '기타', Paper: '종이/팩', Plastic: '플라스틱' };
    const TILT_NAMES = { forward: '전진 전도', backward: '후진 전도' };
    // Same colors as the former bin guide and the 3D bins: operators tell bins apart by color.
    const CATEGORY_COLORS = {
        Metal: ['#fbbf24', 'rgba(245, 158, 11, 0.3)'], Paper: ['#4ade80', 'rgba(16, 185, 129, 0.3)'],
        Plastic: ['#60a5fa', 'rgba(59, 130, 246, 0.3)'], Glass: ['#cbd5e1', 'rgba(148, 163, 184, 0.3)'],
        Others: ['#cbd5e1', 'rgba(148, 163, 184, 0.3)'],
    };
    let destDirty = false;
    let config = null;
    let running = false;
    let pidDirty = false;
    let pidApplying = false;
    let pidLive = null;

    const request = async (url, body) => {
        const res = await fetch(url, body === undefined ? {} : {
            method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body),
        });
        let data = {};
        try { data = await res.json(); } catch (e) { /* non-JSON error page */ }
        if (data.config) applyConfig(data.config);
        if (!res.ok || data.status === 'error' || data.status === 'busy')
            throw new Error(data.message || `서버 응답 ${res.status}`);
        return data;
    };

    const show = (id, html, state) => {
        const el = $(id);
        if (!el) return;
        el.innerHTML = html;
        el.classList.toggle('ok', state === 'ok');
        el.classList.toggle('err', state === 'err');
    };

    const setRunning = (on) => {
        running = on;
        $('rail-tool-busy').hidden = !on;
        document.querySelectorAll('#rail-tools-card .rail-btn').forEach((b) => {
            if (b.id !== 'rail-cancel-btn' && b.id !== 'system-home-btn') b.disabled = on;
        });
    };

    const tool = async (resultId, startText, url, body, render) => {
        if (running) return;
        setRunning(true);
        show(resultId, startText);
        try {
            const data = await request(url, body);
            show(resultId, render(data.result), 'ok');
            return data.result;
        } catch (e) {
            show(resultId, esc(e.message), 'err');
            log(e.message);
        } finally {
            setRunning(false);
            refreshLive();
        }
    };

    function applyConfig(c) {
        config = c;
        $('rail-dir-select').value = String(c.rail_dir);
        $('rail-steps-mm-input').value = c.steps_per_mm;
        $('rail-mode-select').value = c.mode;
        $('rail-settle-s').value = c.settle_s;
        $('rail-timeout-s').value = c.timeout_s;
        for (const slot of ['center', 'plus', 'minus']) {
            $(`rail-${slot}-mm`).value = c[`${slot}_mm`];
            const state = $(`rail-${slot}-state`);
            state.textContent = c.taught[slot] ? '저장됨' : '미저장(기본값)';
            state.className = `rail-taught ${c.taught[slot] ? 'yes' : 'no'}`;
        }
        const pid = { p: c.pid_p, i: c.pid_i, d: c.pid_d };
        for (const k of Object.keys(pid)) {
            const el = $(`pid-${k}-input`);
            if (el && !pidDirty && document.activeElement !== el) el.value = pid[k];
        }
        // Sequence origin (tray level + rail ToF home_mm): one setting, no teaching.
        if ($('rail-home-mm') && document.activeElement !== $('rail-home-mm')) $('rail-home-mm').value = c.home_mm ?? c.capture_mm ?? 120;
        document.querySelectorAll('[data-home-mm]').forEach((el) => { el.textContent = `${c.home_mm ?? 120}`; });
        if (!destDirty) renderDestinations(c);
        renderBinGuide(c);
        // Manual PID targets stay inside the Z safety limits (default ToF 70..120 mm).
        const lo = c.rail_min_mm ?? 70, hi = c.rail_max_mm ?? 120;
        for (const [id, value] of [['rail-limit-min', lo], ['rail-limit-max', hi]])
            if ($(id) && document.activeElement !== $(id)) $(id).value = value;
        for (const id of ['pid-target-slider', 'rail-goto-mm', 'rail-home-mm', 'rail-center-mm', 'rail-plus-mm', 'rail-minus-mm']) {
            const el = $(id);
            if (el) { el.min = lo; el.max = hi; }
        }
        const note = $('pid-target-range-note');
        if (note) note.textContent = `Z축 안전 한계 ${lo}~${hi} mm (원점 ${c.home_mm ?? 120}, 중앙 ${c.center_mm}, +측 ${c.plus_mm}, -측 ${c.minus_mm})`;
        window.ardRailConfig = c; // the 3D bins may load later
        window.dispatchEvent(new CustomEvent('ard-rail-config', { detail: c }));
    }

    function renderLive(s) {
        pidLive = s;
        const hasGains = ['p', 'i', 'd'].every((k) => Number.isFinite(s[`pid_${k}`]));
        $('pid-gains-live').textContent = hasGains
            ? `보드 현재값: P ${s.pid_p} / I ${s.pid_i} / D ${s.pid_d} · PID ${s.pid ? 'ON' : 'OFF'}`
            : '보드 현재값: 조회 미지원 (DriveMCU 펌웨어 업데이트 필요)';
        $('pid-gains-apply').disabled = pidApplying || !hasGains || s.pid || s.busy || s.jogging || running;
        const tof = s.tof_ok ? s.tof_mm : null;
        $('rail-live-tof').textContent = tof === null ? `없음 (${s.tof_status || '-'})` : `${fmt(tof)} mm`;
        $('rail-live-target').textContent = s.rail_target !== undefined && s.rail_target !== null ? `${s.rail_target} mm` : '-';
        $('rail-live-error').textContent = tof !== null && s.pid ? `${fmt(tof - s.rail_target)} mm` : '-';
        $('rail-live-pid').textContent = s.pid ? (s.rail_settled ? 'ON · 정착' : 'ON · 이동') : 'OFF';
        $('rail-live-dir').textContent = s.rail_dir === undefined || s.rail_dir === null ? '펌웨어 구버전' : (s.rail_dir > 0 ? '+1' : '-1');
        $('rail-live-spm').textContent = fmt(s.steps_per_mm, 2);
        if (config && s.rail_dir !== undefined && s.rail_dir !== null && (s.rail_dir !== config.rail_dir || Math.abs(s.steps_per_mm - config.steps_per_mm) > 0.01))
            $('rail-live-dir').textContent += ' (PC 설정과 다름)';
    }

    async function refreshLive() {
        if (!$('rail-tools-card').offsetParent) return; // drive tab hidden
        try {
            const res = await fetch('/api/rail/live');
            const data = await res.json();
            if (!res.ok || data.status !== 'success') throw new Error(data.message || res.status);
            renderLive(data.result);
        } catch (e) {
            $('rail-live-tof').textContent = '연결 없음';
            pidLive = null;
            $('pid-gains-live').textContent = '보드 현재값: 연결 없음';
            $('pid-gains-apply').disabled = true;
        }
    }

    // --- ① ToF stability -------------------------------------------------
    $('rail-sample-btn').addEventListener('click', () => {
        const seconds = Number($('rail-sample-seconds').value) || 3;
        tool('rail-sample-result', `${seconds}초 동안 측정 중...`, '/api/rail/tof-sample', { seconds }, (r) => {
            const verdict = r.std_mm <= 1 ? '양호' : r.std_mm <= 2 ? '주의 (PID 정착이 느려질 수 있음)' : '불량 (센서 고정·반사면 확인)';
            return `평균 <b>${fmt(r.mean_mm, 2)} mm</b> · 표준편차 ${fmt(r.std_mm, 2)} mm · 범위 ${fmt(r.min_mm)}~${fmt(r.max_mm)} (폭 ${fmt(r.span_mm)}) · ${r.n}개 · <b>${verdict}</b>`;
        });
    });

    // --- ② direction & steps/mm -------------------------------------------
    $('rail-measure-btn').addEventListener('click', () => {
        const steps = parseInt($('rail-measure-steps').value, 10) || 400;
        const apply = $('rail-measure-apply').checked;
        if (!confirm(`Z축을 +${steps}스텝 이동했다가 되돌립니다. 레일 양 끝에 충분한 여유가 있나요?`)) return;
        tool('rail-measure-result', '측정 중... (이동 → 측정 → 복귀)', '/api/rail/measure-direction', { steps, apply }, (r) => {
            const dirText = r.rail_dir > 0 ? '+1 (+스텝 → 거리 증가, 원 설계와 같음)' : '-1 (+스텝 → 거리 감소, 반전됨)';
            log(`direction ${r.rail_dir}, ${r.steps_per_mm} steps/mm${r.applied ? ' (applied)' : ''}`);
            return `ToF ${fmt(r.before_mm, 2)} → ${fmt(r.after_mm, 2)} mm (Δ ${fmt(r.delta_mm, 2)} mm / ${r.steps} steps)<br>` +
                `방향 <b>${dirText}</b> · Steps/mm <b>${r.steps_per_mm}</b> · 복귀 오차 ${fmt(r.return_error_mm, 2)} mm (잡음 ${fmt(r.noise_mm, 2)} mm)<br>` +
                (r.applied ? '측정값을 저장하고 보드에 적용했습니다.' : '아래 방향·Steps/mm 칸에 옮겨 [수동 값 저장·적용]을 누르면 반영됩니다.');
        }).then((r) => {
            if (r && !r.applied) {
                $('rail-dir-select').value = String(r.rail_dir);
                $('rail-steps-mm-input').value = r.steps_per_mm;
            }
        });
    });

    $('rail-steps-mm-apply-btn').addEventListener('click', () => {
        const body = { rail_dir: Number($('rail-dir-select').value), steps_per_mm: Number($('rail-steps-mm-input').value), apply: true };
        tool('rail-measure-result', '저장·적용 중...', '/api/rail/config', body,
            () => `방향 ${body.rail_dir > 0 ? '+1' : '-1'}, Steps/mm ${body.steps_per_mm}을 저장하고 보드에 적용했습니다.`);
    });

    // --- ③ teach / goto ----------------------------------------------------
    document.querySelectorAll('#rail-tools-card .rail-teach-btn').forEach((btn) => btn.addEventListener('click', () => {
        const slot = btn.dataset.slot;
        tool('rail-goto-result', `${SLOT_NAMES[slot]} 위치 측정 중...`, '/api/rail/teach', { slot }, (r) => {
            log(`${slot} = ${r.config[`${slot}_mm`]} mm`);
            return `${SLOT_NAMES[slot]} 위치를 <b>${fmt(r.config[`${slot}_mm`])} mm</b>로 저장했습니다 (표준편차 ${fmt(r.sample.std_mm, 2)} mm).`;
        });
    }));

    const renderGoto = (r) => `목표 ${r.target_mm} mm → ToF <b>${fmt(r.tof_mm)} mm</b> (오차 ${fmt(r.error_mm)} mm) · 정착 ${fmt(r.elapsed_s, 2)} s · Z ${r.z} steps. PID가 위치를 유지 중입니다.`;
    document.querySelectorAll('#rail-tools-card .rail-goto-btn').forEach((btn) => btn.addEventListener('click', () => {
        const slot = btn.dataset.slot;
        tool('rail-goto-result', `${SLOT_NAMES[slot]}(으)로 PID 이동 중...`, '/api/rail/goto', { slot, hold: true }, renderGoto);
    }));
    $('rail-goto-mm-btn').addEventListener('click', () => {
        const mm = Number($('rail-goto-mm').value);
        if (!Number.isFinite(mm) || mm <= 0) return;
        tool('rail-goto-result', `${mm} mm로 PID 이동 중...`, '/api/rail/goto', { mm, hold: true }, renderGoto);
    });
    $('rail-save-points-btn').addEventListener('click', () => {
        const body = {}; const taught = {};
        for (const slot of ['center', 'plus', 'minus']) {
            const v = Number($(`rail-${slot}-mm`).value);
            if (Number.isFinite(v) && v > 0 && v !== config[`${slot}_mm`]) { body[`${slot}_mm`] = v; taught[slot] = true; }
        }
        const home = Number($('rail-home-mm')?.value);
        if (Number.isFinite(home) && home > 0 && home !== config.home_mm) body.home_mm = home; // not a taught slot
        if (!Object.keys(body).length) return;
        body.taught = taught;
        tool('rail-goto-result', '저장 중...', '/api/rail/config', body, () => '입력한 위치값을 저장했습니다.');
    });

    // --- ④ repeatability sweep -------------------------------------------
    $('rail-sweep-btn').addEventListener('click', () => {
        const cycles = parseInt($('rail-sweep-cycles').value, 10) || 2;
        if (!confirm(`세 저장 위치를 ${cycles}회 왕복합니다. 트레이 위에 물체가 없나요?`)) return;
        tool('rail-sweep-result', `반복 정밀도 테스트 중... (${cycles * 4 + 1}회 이동)`, '/api/rail/sweep', { cycles }, (r) => {
            const rows = ['center', 'plus', 'minus'].map((slot) => {
                const s = r.summary[slot];
                return `<tr><td>${SLOT_NAMES[slot]}</td><td>${s.target_mm}</td><td>${fmt(s.max_abs_error_mm)}</td><td>${fmt(s.tof_spread_mm)}</td><td>${s.step_spread ?? '-'}</td><td>${fmt(s.mean_settle_s, 2)}</td></tr>`;
            }).join('');
            const worst = Math.max(...Object.values(r.summary).map((s) => s.max_abs_error_mm));
            return `<table><thead><tr><th>위치</th><th>목표 mm</th><th>최대 오차 mm</th><th>ToF 편차 mm</th><th>스텝 편차</th><th>평균 정착 s</th></tr></thead><tbody>${rows}</tbody></table>` +
                `<div style="margin-top:6px;">최대 오차 ${fmt(worst)} mm (PID 정지 범위 ±2 mm). 스텝 편차가 크면 같은 ToF에서 모터 위치가 달라진 것이므로 미끄러짐·백래시·센서 흔들림을 확인하세요.</div>`;
        });
    });

    // --- ⑤ destination table (material -> rail slot + tilt) -------------------
    function renderDestinations(c) {
        const body = $('rail-dest-body');
        if (!body || !c.destinations) return;
        const option = (value, label, selected) => `<option value="${value}"${value === selected ? ' selected' : ''}>${label}</option>`;
        body.innerHTML = CATEGORIES.map((cat) => {
            const d = c.destinations[cat] || {};
            const target = c.mode === 'pid' ? ` (${c[`${d.rail}_mm`]} mm)` : '';
            return `<tr data-category="${cat}"><td style="color: ${CATEGORY_COLORS[cat][0]}; font-weight: 600;">${CATEGORY_NAMES[cat]} (${cat})</td>` +
                `<td><select class="rail-dest-rail">${['plus', 'center', 'minus'].map((s) => option(s, SLOT_NAMES[s], d.rail)).join('')}</select>${target}</td>` +
                `<td><select class="rail-dest-tilt">${['forward', 'backward'].map((t) => option(t, TILT_NAMES[t], d.tilt)).join('')}</select></td>` +
                `<td><button class="btn-primary rail-btn rail-drop-btn" type="button" data-category="${cat}" style="background: #b45309;">배출 위치 시험</button></td></tr>`;
        }).join('');
        if (document.activeElement !== $('rail-tilt-deg')) $('rail-tilt-deg').value = c.tilt_deg ?? 45;
        body.querySelectorAll('.rail-btn').forEach((b) => { b.disabled = running; });
    }

    function renderBinGuide(c) {
        const guide = $('bin-guide');
        if (!guide || !c.destinations) return;
        const cells = [];
        for (const rail of ['plus', 'minus', 'center']) {
            for (const tilt of ['forward', 'backward']) {
                const cats = CATEGORIES.filter((cat) => c.destinations[cat]?.rail === rail && c.destinations[cat]?.tilt === tilt);
                if (!cats.length && rail === 'center') continue;
                const where = c.mode === 'pid' ? `ToF ${c[`${rail}_mm`]}mm` : `${rail === 'plus' ? '+15' : rail === 'minus' ? '-15' : '0'}mm`;
                const border = cats.length ? CATEGORY_COLORS[cats[0]][1] : 'rgba(148,163,184,0.3)';
                const names = cats.map((cat) => `<span style="color: ${CATEGORY_COLORS[cat][0]};">${esc(CATEGORY_NAMES[cat])}</span>`).join(', ');
                cells.push(`<div style="background: rgba(0,0,0,0.25); padding: 8px; border-radius: 6px; border: 1px solid ${border};">` +
                    `<strong>${names || '<span style="color: #64748b;">(비어 있음)</span>'}</strong>` +
                    `<div style="color: #94a3b8; margin-top: 2px;">레일 ${SLOT_NAMES[rail]} (${where}) · ${TILT_NAMES[tilt]} ${c.tilt_deg ?? 45}°</div></div>`);
            }
        }
        guide.innerHTML = cells.join('');
    }

    $('rail-dest-body')?.addEventListener('change', () => {
        destDirty = true;
        show('rail-dest-result', '변경 사항이 저장되지 않았습니다. [배출 위치 표 저장]을 누르세요.');
    });
    $('rail-tilt-deg')?.addEventListener('input', () => { destDirty = true; });
    $('rail-dest-save-btn')?.addEventListener('click', async () => {
        const destinations = {};
        document.querySelectorAll('#rail-dest-body tr').forEach((row) => {
            destinations[row.dataset.category] = {
                rail: row.querySelector('.rail-dest-rail').value, tilt: row.querySelector('.rail-dest-tilt').value,
            };
        });
        try {
            destDirty = false;
            await request('/api/rail/config', { destinations, tilt_deg: Number($('rail-tilt-deg').value) });
            show('rail-dest-result', '배출 위치 표를 저장했습니다. 다음 선별부터 적용됩니다.', 'ok');
            log('destination table saved');
        } catch (e) {
            destDirty = true;
            show('rail-dest-result', esc(e.message), 'err');
        }
    });
    $('rail-dest-body')?.addEventListener('click', (e) => {
        const btn = e.target.closest('.rail-drop-btn');
        if (!btn) return;
        if (destDirty) { show('rail-dest-result', '표를 먼저 저장한 뒤 시험하세요.', 'err'); return; }
        const cat = btn.dataset.category;
        tool('rail-dest-result', `${CATEGORY_NAMES[cat]} 배출 위치 시험 중... (레일 이동 → 전도 → 2초 → 전도 복귀 → 레일 중앙)`,
            '/api/rail/test-drop', { category: cat },
            (r) => `${CATEGORY_NAMES[cat]}: 레일 ${SLOT_NAMES[r.rail]}${r.rail_target_mm ? ` (ToF ${r.rail_target_mm} mm)` : ''} · ${TILT_NAMES[r.tilt]} ${r.tilt_deg}° (스텝 b${r.tilt_steps.join(',')}) 완료. ` +
                '물체가 의도한 배출함에 떨어졌는지 확인하고, 다르면 표를 고쳐 저장하세요.');
    });

    // --- sorting settings / cancel -----------------------------------------
    $('rail-config-save-btn').addEventListener('click', () => {
        const body = { mode: $('rail-mode-select').value, settle_s: Number($('rail-settle-s').value), timeout_s: Number($('rail-timeout-s').value),
            rail_min_mm: Number($('rail-limit-min').value), rail_max_mm: Number($('rail-limit-max').value) };
        request('/api/rail/config', body).then(() => log(`sorting rail mode: ${body.mode}`)).catch((e) => alert(e.message));
    });
    $('rail-cancel-btn').addEventListener('click', async () => {
        try { await fetch('/api/rail/cancel', { method: 'POST' }); } catch (e) { /* the board command below still runs */ }
        try {
            await fetch('/api/drivemcu/command', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ command: 'mzm0' }) });
        } catch (e) { /* offline */ }
        log('tool cancelled, PID off');
        refreshLive();
    });

    // Editing is separate from applying. Never overwrite an unsaved draft on live/config refresh.
    ['p', 'i', 'd'].forEach((k) => $(`pid-${k}-input`)?.addEventListener('input', () => {
        pidDirty = true;
        $('pid-gains-feedback').textContent = '입력값이 변경되었습니다. 적용 버튼을 눌러 보드에 반영하세요.';
    }));
    $('pid-gains-preset').addEventListener('click', () => {
        ['p', 'i', 'd'].forEach((k, i) => { $(`pid-${k}-input`).value = [0.12, 0, 0][i]; });
        pidDirty = true;
        $('pid-gains-feedback').textContent = '초기값을 입력했습니다. 적용 버튼을 누르면 반영됩니다.';
    });
    $('pid-gains-apply').disabled = true;
    $('pid-gains-apply').addEventListener('click', async () => {
        if (pidApplying || !pidLive || pidLive.pid || pidLive.busy || pidLive.jogging || running) return;
        const body = {};
        for (const k of ['p', 'i', 'd']) {
            const el = $(`pid-${k}-input`);
            if (!el.value.trim() || !el.checkValidity() || !Number.isFinite(Number(el.value))) {
                $('pid-gains-feedback').textContent = 'P·I·D는 0~10000 범위의 숫자로 입력하세요.';
                return;
            }
            body[`pid_${k}`] = Number(el.value);
        }
        pidApplying = true;
        $('pid-gains-apply').disabled = true;
        $('pid-gains-feedback').textContent = '보드에 적용하고 현재값 확인 중…';
        try {
            const data = await request('/api/rail/pid', body);
            pidDirty = false;
            applyConfig(data.config);
            $('pid-gains-feedback').textContent = '보드 현재값 확인 및 PC 저장 완료. 재부팅 후에는 적용 버튼으로 다시 반영하세요.';
        } catch (e) {
            $('pid-gains-feedback').textContent = `적용 실패: ${e.message}`;
        } finally {
            pidApplying = false;
            await refreshLive();
        }
    });

    request('/api/rail/config').catch((e) => log(e.message));
    setInterval(refreshLive, 1500);
    // The PC pushes the board's status about every 300 ms (SSE drive_live); the poll stays as a fallback.
    window.addEventListener('ard-drive-live', (e) => {
        if ($('rail-tools-card').offsetParent && e.detail.live) renderLive(e.detail);
    });
})();
