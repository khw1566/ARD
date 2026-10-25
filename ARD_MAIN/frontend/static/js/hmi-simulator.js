// HMI screen simulator for the PC centre's HMI tab.
// Mirrors ARD_ESP-IDF/HMI_MCU/main/hmi_ui.c + gfx.c pixel for pixel (same layout,
// colours, 8x8 / 16x16 fonts) and feeds it the same /api/status JSON the board polls,
// so the panel here shows what the real 240x320 display shows right now.
// Keep it in step with hmi_ui.c when the firmware screen changes.
(function () {
    'use strict';

    const W = 240, H = 320;
    const HDR_H = 28, TAB_H = 30, SUB_H = 26, CONT_Y = HDR_H + TAB_H + SUB_H, CONT_H = 160;
    const RBTN_W = 62, INFO_W = W - RBTN_W, BOT_Y = CONT_Y + CONT_H, BOT_H = H - BOT_Y;
    const NAV_W = 42, NAV_X = W - NAV_W * 2 - 4;
    const PREVIEW_W = 178, PREVIEW_H = 160;
    const POLL_MS = 3000, HOLD_MS = 4000, IMAGE_MS = 3000;

    const C = {
        BG: 0xFFFF, HDR: 0xEF7D, TABA: 0xD67A, ACC: 0x0210, ACC2: 0x0410, BTN: 0xE71C, INFO: 0xF7BE,
        PANEL: 0xEF7D, SEP: 0xD6BA, ON: 0x0500, OFF: 0xA000, DIM: 0x4208, TEXT: 0x2104, CYAN: 0x07FF,
        AMBER: 0xB400, GREY: 0xBDF7,
    };
    const A = { TL: 0, TC: 1, TR: 2, MC: 3, MR: 4, ML: 5 };

    const TAB_NAMES = ['Drive', 'Robot', 'Cam', 'Ctrl'];
    const DEV_NAMES = ['DriveMCU', 'Robot3Axis', 'CamMCU', 'ControlMCU'];
    const DEV_KEYS = ['drivemcu', 'robot3axismcu', 'imagesendermcu', 'controlmcu'];
    const PAGE_COUNT = [4, 4, 4, 1];
    const PAGE_TITLES = [
        ['Network', 'Rotate Roll/Yaw', 'Jog X/Y', 'Z Rail'],
        ['Network', 'Axis Jog', 'Gripper', 'Max Speed'],
        ['Network', 'Capture', 'Camera Config', 'AI Model'],
        ['Voice'],
    ];
    const COMMAND_PATHS = ['/api/drivemcu/command', '/api/robot3axis/command',
        '/api/imagesendermcu/command', '/api/controlmcu/command'];
    const HB_MS = [250, 500, 1000, 2000];
    const I2C_HZ = [50000, 100000, 400000], I2C_NAMES = ['LO', 'MD', 'HI'];
    const CAM_SENSORS = ['OV2640', 'OV3660', 'OV5640'];
    const CAM_RES = ['VGA', 'SVGA', 'XGA', 'SXGA', 'UXGA'];
    const CAM_RES_PX = ['640x480', '800x600', '1024x768', '1280x1024', '1600x1200'];
    const CAM_QUALITY = [5, 10, 20, 40];
    const PID_MIN = 145, PID_MAX = 175, PID_STEP = 5, Z_STEP_DELTA = 10;
    const Z_STEP_BUTTONS = false; // firmware default (CONFIG_HMI_Z_STEP_BUTTONS=n)
    const RAIL_PID = false;       // firmware default (CONFIG_HMI_RAIL_PID=n)

    // ---- fonts -------------------------------------------------------------------
    const F = window.HMI_SIM_FONTS;
    const b64 = s => Uint8Array.from(atob(s), ch => ch.charCodeAt(0));
    const ascii8 = b64(F.ascii8);
    const hangul16 = b64(F.hangul16);
    const hangulIndex = new Map(F.hangulCodes.map((code, i) => [code, i * 32]));

    // ---- framebuffer + primitives (gfx.c) -----------------------------------------
    const fb = new Uint16Array(W * H);

    function fillRect(x, y, w, h, c) {
        let x1 = x + w, y1 = y + h;
        if (x < 0) x = 0; if (y < 0) y = 0;
        if (x1 > W) x1 = W; if (y1 > H) y1 = H;
        for (let yy = y; yy < y1; yy++) fb.fill(c, yy * W + x, yy * W + Math.max(x, x1));
    }
    const hline = (x, y, w, c) => fillRect(x, y, w, 1, c);
    const vline = (x, y, h, c) => fillRect(x, y, 1, h, c);
    function rect(x, y, w, h, c) { hline(x, y, w, c); hline(x, y + h - 1, w, c); vline(x, y, h, c); vline(x + w - 1, y, h, c); }
    function isqrt(v) { let r = 0; while ((r + 1) * (r + 1) <= v) r++; return r; }
    function cornerInset(i, h, r) {
        let dy;
        if (i < r) dy = r - i; else if (i >= h - r) dy = i - (h - 1 - r); else return 0;
        return r - isqrt(r * r - dy * dy);
    }
    function fillRoundRect(x, y, w, h, r, c) {
        if (r * 2 > w) r = w >> 1; if (r * 2 > h) r = h >> 1;
        for (let i = 0; i < h; i++) { const a = cornerInset(i, h, r); hline(x + a, y + i, w - 2 * a, c); }
    }
    function roundRect(x, y, w, h, r, c) {
        if (r * 2 > w) r = w >> 1; if (r * 2 > h) r = h >> 1;
        for (let i = 0; i < h; i++) {
            const a = cornerInset(i, h, r);
            if (i === 0 || i === h - 1) { hline(x + a, y + i, w - 2 * a, c); continue; }
            const edge = i < (h >> 1) ? cornerInset(i - 1, h, r) : cornerInset(i + 1, h, r);
            const len = edge - a > 1 ? edge - a : 1;
            hline(x + a, y + i, len, c); hline(x + w - a - len, y + i, len, c);
        }
    }
    function fillCircle(cx, cy, r, c) {
        for (let dy = -r; dy <= r; dy++) { const half = isqrt(r * r - dy * dy); hline(cx - half, cy + dy, half * 2 + 1, c); }
    }
    function image(x, y, img) {
        for (let row = 0; row < img.h; row++) {
            const yy = y + row; if (yy < 0 || yy >= H) continue;
            for (let col = 0; col < img.w; col++) {
                const xx = x + col; if (xx >= 0 && xx < W) fb[yy * W + xx] = img.px[row * img.w + col];
            }
        }
    }

    const textWidth = (s, scale) => Math.max(...s.split('\n').map(l => l.length)) * 8 * scale;
    function text(x, y, s, c, scale, align) {
        const lines = s.split('\n');
        let cy = y;
        if (align >= A.MC) cy -= Math.trunc((lines.length * (8 * scale + 2) - 2) / 2);
        for (const line of lines) {
            const tw = line.length * 8 * scale;
            let dx = x;
            if (align === A.TC || align === A.MC) dx -= Math.trunc(tw / 2);
            if (align === A.TR || align === A.MR) dx -= tw;
            for (let i = 0; i < line.length; i++) {
                const g = (line.charCodeAt(i) & 0x7F) * 8;
                for (let row = 0; row < 8; row++) {
                    const bits = ascii8[g + row];
                    for (let col = 0; col < 8; col++)
                        if (bits & (1 << col)) fillRect(dx + i * 8 * scale + col * scale, cy + row * scale, scale, scale, c);
                }
            }
            cy += 8 * scale + 2;
        }
    }
    const isAscii = s => /^[\x00-\x7F]*$/.test(s);
    const adv16 = cp => cp < 0x80 ? 8 : 16;
    function utf8Width(s) { return isAscii(s) ? textWidth(s, 1) : [...s].reduce((w, ch) => w + adv16(ch.codePointAt(0)), 0); }
    function utf8Fit(s, maxPx) {
        let w = 0, out = '';
        for (const ch of s) { const a = adv16(ch.codePointAt(0)); if (w + a > maxPx) break; w += a; out += ch; }
        return out;
    }
    function glyph16(x, y, off, firstCol, cols, c) {
        for (let row = 0; row < 16; row++) {
            const bits = (hangul16[off + row * 2] << 8) | hangul16[off + row * 2 + 1];
            for (let col = 0; col < cols; col++) if (bits & (0x8000 >> (firstCol + col))) fillRect(x + col, y + row, 1, 1, c);
        }
    }
    function textUtf8(x, y, s, c, align) {
        if (isAscii(s)) { text(x, y, s, c, 1, align); return; }
        const tw = utf8Width(s);
        if (align === A.TC || align === A.MC) x -= Math.trunc(tw / 2);
        if (align === A.TR || align === A.MR) x -= tw;
        if (align >= A.MC) y -= 8;
        for (const ch of s) {
            const cp = ch.codePointAt(0);
            const off = cp > 32 ? hangulIndex.get(cp) : undefined;
            if (cp < 0x80) { if (off !== undefined) glyph16(x, y, off, 4, 8, c); x += 8; }
            else { if (off !== undefined) glyph16(x, y, off, 0, 16, c); else rect(x + 2, y + 2, 12, 12, c); x += 16; }
        }
    }

    // ---- model (hmi_model.c / hmi_net.c parse_status) --------------------------------
    function defaults() {
        return {
            wifi_ok: false, server_ok: false, wifi_rssi: -127, server: location.hostname,
            dev: DEV_KEYS.map(() => ({ online: false, rssi: 0, temp: 0, ip: '', last_seen: '--:--:--' })),
            has_tof: false, tof_mm: 0, pid_mode: false, pid_target: 145, z_step: 50, hb_ms: 1000, i2c_hz: 100000,
            robot_x_deg: 0, robot_y_mm: 0, robot_z_mm: 0, grab: '---', robot_speed: [100, 700, 1000],
            cam_sensor: '---', cam_res: '---', cam_quality: 0, ai_object: 'None', ai_category: 'None', ai_model: '---',
            sound_mode: '---', sound_status: '---', ctrl_mode: '---',
            approval: false, approval_id: '', approval_desc: '',
        };
    }
    let m = defaults();
    let holdUntil = 0;

    const obj = v => (v && typeof v === 'object' && !Array.isArray(v)) ? v : null;
    const num = (o, k, d) => (o && typeof o[k] === 'number') ? o[k] : d;
    const str = (o, k, d) => (o && typeof o[k] === 'string') ? o[k] : (o && typeof o[k] === 'number') ? String(o[k]) : d;
    const trunc = (s, n) => { const b = new TextEncoder().encode(s); return b.length <= n - 1 ? s : new TextDecoder().decode(b.slice(0, n - 1)).replace(/�$/, ''); };
    const clock = (d, secs) => d.toTimeString().slice(0, secs ? 8 : 5);

    function applyStatus(root, ok) {
        m.wifi_ok = true; // the PC sees the server; the board's own Wi-Fi is not known here
        m.wifi_rssi = -60;
        m.server_ok = ok;
        if (!root) return;
        const stamp = clock(new Date(), true);
        DEV_KEYS.forEach((k, i) => {
            const d = obj(root[k]), dev = m.dev[i];
            dev.online = !!d && d.state === 'Online';
            dev.rssi = Math.trunc(num(d, 'rssi', 0));
            dev.temp = num(d, 'temp', 0);
            dev.ip = trunc(str(d, 'ip', '---'), 16);
            if (dev.online) dev.last_seen = stamp;
        });
        const drive = obj(root.drivemcu);
        m.has_tof = !!drive && typeof drive.tof_dist === 'number';
        m.tof_mm = m.has_tof ? Math.trunc(drive.tof_dist) : 0;
        const robot = obj(obj(root.robot3axismcu)?.state_data);
        m.robot_x_deg = num(robot, 'x_deg', 0); m.robot_y_mm = num(robot, 'y_mm', 0); m.robot_z_mm = num(robot, 'z_mm', 0);
        m.grab = trunc(str(robot, 'grab_state', '---'), 12);
        const cam = obj(root.imagesendermcu), analysis = obj(cam?.last_analysis);
        m.ai_object = trunc(str(analysis, 'object', 'None'), 48);
        m.ai_category = trunc(str(analysis, 'category', 'None'), 48);
        m.ai_model = trunc(str(cam, 'model_name', '---'), 48);
        const sound = obj(root.controlmcu);
        m.sound_mode = trunc(str(sound, 'voice_mode', '---'), 16);
        m.sound_status = trunc(str(sound, 'status', '---'), 24);
        const control = obj(root.controlmcu);
        m.ctrl_mode = control && control.state === 'Online' ? trunc(str(control, 'mode', '---'), 4) : '---';
        const action = obj(obj(root.hmi_mcu)?.pending_action);
        m.approval = !!action;
        m.approval_id = action ? trunc(str(action, 'id', ''), 24) : '';
        m.approval_desc = action ? trunc(str(action, 'desc', ''), 128) : '';
        if (Date.now() < holdUntil) return;
        const ds = obj(root.drive_settings);
        if (ds) {
            if (typeof ds.pid_mode === 'boolean') m.pid_mode = ds.pid_mode;
            m.pid_target = Math.trunc(num(ds, 'pid_target_tof', m.pid_target));
            m.z_step = Math.trunc(num(ds, 'z_step', m.z_step));
            m.hb_ms = Math.trunc(num(ds, 'hb_interval_ms', m.hb_ms));
            m.i2c_hz = Math.trunc(num(ds, 'i2c_clock_hz', m.i2c_hz));
        }
        m.robot_speed = [Math.trunc(num(robot, 'x_speed', 100)), Math.trunc(num(robot, 'y_speed', 700)), Math.trunc(num(robot, 'z_speed', 1000))];
        const cfg = obj(cam?.config);
        if (cfg) {
            m.cam_sensor = trunc(str(cfg, 'camera_model', '---'), 12);
            m.cam_res = trunc(str(cfg, 'resolution', '---'), 8);
            m.cam_quality = Math.trunc(num(cfg, 'jpeg_quality', 0));
        }
    }

    // ---- UI state (hmi_ui.c) ---------------------------------------------------------------
    const ui = { tab: 0, page: [0, 0, 0, 0], camView: false, press: null, pressIndex: 0, pressUntil: 0,
        answeredId: '', answeredAt: 0 };
    let preview = null, previewError = null;

    const findInt = (list, v) => list.indexOf(v);
    const findStr = (list, v) => list.indexOf(v);
    const btn = (label, cmd, color, action, arg = 0) => ({ label, cmd, color, action, arg });

    function buildButtons(tab, page) {
        switch (tab * 4 + page) {
            case 0: {
                const hb = findInt(HB_MS, m.hb_ms), i2c = findInt(I2C_HZ, m.i2c_hz);
                return [btn(hb >= 0 ? `HB\n${HB_MS[hb]}` : `HB\n${m.hb_ms}?`, '', C.ACC, 'hb'),
                    btn(`I2C ${i2c >= 0 ? I2C_NAMES[i2c] : '??'}`, '', C.ACC, 'i2c')];
            }
            case 1: return [btn('R-FW', 'b45,-45', C.ACC, 'send'), btn('R-BW', 'b-45,45', C.ACC, 'send'),
                btn('Y-CW', 'b45,45', C.ACC2, 'send'), btn('Y-CCW', 'b-45,-45', C.ACC2, 'send')];
            case 2: return [btn('X+50', 'b50,0', C.CYAN, 'send'), btn('X-50', 'b-50,0', C.CYAN, 'send'),
                btn('Y+50', 'b0,50', C.CYAN, 'send'), btn('Y-50', 'b0,-50', C.CYAN, 'send')];
            case 3: {
                const b = [RAIL_PID ? btn(m.pid_mode ? 'PID' : 'Open\nLoop', '', m.pid_mode ? C.ON : C.ACC2, 'pid')
                    : btn('PID\nlocked', '', C.GREY, 'none')];
                if (!RAIL_PID && m.pid_mode) b.push(btn('TGT\nlocked', '', C.GREY, 'none'), btn('TGT\nlocked', '', C.GREY, 'none'));
                else if (m.pid_mode) b.push(btn(`TGT\n+${PID_STEP}`, '', C.ACC, 'target', PID_STEP), btn(`TGT\n-${PID_STEP}`, '', C.ACC, 'target', -PID_STEP));
                else if (Z_STEP_BUTTONS) b.push(btn(`STP\n+${Z_STEP_DELTA}`, '', C.ACC, 'zstep', Z_STEP_DELTA), btn(`STP\n-${Z_STEP_DELTA}`, '', C.ACC, 'zstep', -Z_STEP_DELTA));
                else b.push(btn('STP\nlocked', '', C.GREY, 'none'), btn('STP\nlocked', '', C.GREY, 'none'));
                b.push(btn('HOME', 'home', C.OFF, 'send'));
                return b;
            }
            case 4: return [btn('REFRESH', 'refresh', C.ACC, 'refresh')];
            case 5: return ['X', 'Y', 'Z'].flatMap(a => [btn(`${a}+50`, `r${a.toLowerCase()}50`, C.CYAN, 'send'),
                btn(`${a}-50`, `r${a.toLowerCase()}-50`, C.CYAN, 'send')]);
            case 6: return [btn('GRAB\nOPEN', 'grab open', C.ON, 'send'), btn('GRAB\nCLOSE', 'grab close', C.OFF, 'send'),
                btn('HOME', 'home', C.ACC, 'send')];
            case 7: return ['X', 'Y', 'Z'].flatMap((a, i) => {
                const s = [10, 50, 50][i], l = a.toLowerCase();
                return [btn(`${a} SPD+`, `r${l}s${s}`, C.ACC, 'speed', i * 2), btn(`${a} SPD-`, `r${l}s-${s}`, C.ACC, 'speed', i * 2 + 1)];
            });
            case 8: return [btn('SPI 10M', 'cspi10', C.ACC2, 'send'), btn('SPI 20M', 'cspi20', C.ACC2, 'send')];
            case 9: return [btn('SHOT', 'capture', C.ACC, 'send'), btn(ui.camView ? 'HIDE' : 'VIEW', '', C.ON, 'view'),
                btn('AI\nANALYZE', 'analyze', C.ACC2, 'send'), btn('STREAM', 'stream', ui.camView ? C.OFF : C.BTN, 'send')];
            case 10: {
                const r = findStr(CAM_RES, m.cam_res);
                return [btn(`SENS\n${m.cam_sensor}`, 'c_sens', C.ACC, 'sens'),
                    btn(`${m.cam_res}\n${r >= 0 ? CAM_RES_PX[r] : ''}`, 'c_res', C.ACC, 'res'),
                    btn(`QUAL\n${m.cam_quality}`, 'c_qlt', C.ACC, 'qual')];
            }
            case 11: return [btn('AI MDL\nNEXT', 'c_mdl', C.ACC, 'refresh')];
            case 12: return [btn('VAD', 'vad', C.ON, 'send'), btn('PTT', 'ptt', C.ACC, 'send'),
                btn('MUTE', 'mute', C.OFF, 'send'), btn('PLAY', 'play', C.ON, 'send')];
        }
        return [];
    }

    // ---- drawing --------------------------------------------------------------------------
    function drawSignal(x, y, rssi, ok) {
        const bars = !ok ? 0 : rssi > -70 ? 3 : rssi > -80 ? 2 : rssi > -90 ? 1 : 0;
        for (let i = 0; i < 3; i++) { const bh = 5 + i * 3; fillRect(x + i * 6, y + (11 - bh), 4, bh, i < bars ? C.ON : C.SEP); }
    }
    function labelColor(bg) {
        const r = (bg >> 11) << 3, g = ((bg >> 5) & 63) << 2, b = (bg & 31) << 3;
        return Math.trunc((299 * r + 587 * g + 114 * b) / 1000) < 140 ? C.BG : C.TEXT;
    }
    const drawDot = (x, y, on) => fillCircle(x, y, 4, on ? C.ON : C.OFF);
    function kv(y, key, value, color) { text(8, y, key, C.DIM, 1, A.TL); text(8 + textWidth(key, 1) + 8, y, value, color, 1, A.TL); }
    function kvUtf8(y, key, value, color) {
        const x = 8 + textWidth(key, 1) + 8;
        text(8, y, key, C.DIM, 1, A.TL);
        textUtf8(x, y + 4, utf8Fit(value, INFO_W - x - 4), color, A.ML);
    }
    const f1 = v => v.toFixed(1);

    function drawHeader() {
        fillRect(0, 0, W, HDR_H, C.HDR); hline(0, HDR_H, W, C.SEP);
        text(6, 6, clock(new Date(), false), C.TEXT, 2, A.TL);
        text(W / 2, HDR_H / 2, 'ARD', C.ACC, 2, A.MC);
        drawSignal(W - 32, 6, m.wifi_rssi, m.wifi_ok);
        drawDot(W - 8, 14, m.wifi_ok && m.server_ok);
    }
    function drawTabs() {
        const tw = W / 4;
        for (let i = 0; i < 4; i++) {
            const tx = i * tw, active = i === ui.tab, bg = active ? C.TABA : C.HDR;
            fillRect(tx, HDR_H, tw, TAB_H, bg);
            if (active) fillRect(tx, HDR_H + TAB_H - 3, tw, 3, C.ACC);
            text(tx + tw / 2, HDR_H + TAB_H / 2, TAB_NAMES[i], active ? C.TEXT : C.DIM, 1, A.MC);
            fillCircle(tx + tw - 7, HDR_H + 7, 2, m.dev[i].online ? C.ON : C.SEP);
            if (i < 3) vline(tx + tw, HDR_H, TAB_H, C.SEP);
        }
        hline(0, HDR_H + TAB_H, W, C.SEP);
    }
    function drawSubbar() {
        const y = HDR_H + TAB_H;
        fillRect(0, y, W, SUB_H, C.BTN); hline(0, y + SUB_H - 1, W, C.SEP);
        text(W / 2, y + SUB_H / 2, `[ ${PAGE_TITLES[ui.tab][ui.page[ui.tab]]} ]`, C.DIM, 1, A.MC);
    }
    function infoNetwork(tab, y) {
        const d = m.dev[tab];
        kv(y + 36, 'IP:', d.ip || '---', C.TEXT);
        kv(y + 56, 'RSSI:', d.online ? `${d.rssi}dBm` : '---', C.TEXT);
        kv(y + 76, 'Temp:', d.online ? `${f1(d.temp)}C` : '---', C.TEXT);
        kv(y + 96, 'Seen:', d.last_seen, C.ACC);
    }
    function infoDrive(page, y) {
        if (page === 0) {
            infoNetwork(0, y);
            kv(y + 116, 'I2C:', `${Math.trunc(m.i2c_hz / 1000)}kHz`, C.TEXT);
            kv(y + 136, 'HB:', `${m.hb_ms}ms`, C.TEXT);
        } else if (page === 1 || page === 2) {
            kv(y + 36, 'ToF:', m.has_tof ? `${m.tof_mm}mm` : '---', C.TEXT);
            text(8, y + 66, page === 1 ? 'Roll FW/BW, Yaw CW/CCW' : 'Jog disc motors', C.DIM, 1, A.TL);
            text(8, y + 82, page === 1 ? '45 steps per press' : '50 steps per press', C.DIM, 1, A.TL);
        } else {
            text(8, y + 26, 'Z-Control |', C.TEXT, 1, A.TL);
            text(100, y + 26, m.pid_mode ? 'PID' : 'OPEN', m.pid_mode ? C.ON : C.OFF, 1, A.TL);
            const rx = 140, ry = y + 30, rh = 110;
            fillRect(rx, ry, 6, rh, C.SEP);
            const clampY = v => Math.min(Math.max(v, ry), ry + rh);
            const tof = m.has_tof ? m.tof_mm : PID_MIN;
            if (m.has_tof) fillRect(rx - 10, clampY(ry + rh - Math.trunc((tof - PID_MIN) * rh / (PID_MAX - PID_MIN))) - 1, 10, 3, C.ON);
            if (m.pid_mode) fillRect(rx + 6, clampY(ry + rh - Math.trunc((m.pid_target - PID_MIN) * rh / (PID_MAX - PID_MIN))) - 1, 10, 3, C.ACC2);
            kv(y + 50, 'ToF:', m.has_tof ? `${m.tof_mm}mm` : '---', C.ON);
            if (m.pid_mode) kv(y + 72, 'TGT:', `${m.pid_target}mm`, C.ACC2);
            else kv(y + 72, 'STP:', `${m.z_step}`, C.TEXT);
        }
    }
    function infoRobot(page, y) {
        if (page === 0) infoNetwork(1, y);
        else if (page === 1) {
            kv(y + 36, 'X:', `${f1(m.robot_x_deg)} deg`, C.TEXT);
            kv(y + 56, 'Y:', `${f1(m.robot_y_mm)} mm`, C.TEXT);
            kv(y + 76, 'Z:', `${f1(m.robot_z_mm)} mm`, C.TEXT);
            text(8, y + 110, 'Control: Axis Pos', C.ACC, 1, A.TL);
        } else if (page === 2) {
            kv(y + 36, 'Gripper:', m.grab, m.grab === 'open' ? C.ON : C.OFF);
            kv(y + 60, 'Valve:', 'Dual-Solenoid', C.TEXT);
            text(8, y + 110, 'Control: Pneumatic', C.ACC, 1, A.TL);
        } else {
            ['X Spd:', 'Y Spd:', 'Z Spd:'].forEach((k, i) => kv(y + 36 + i * 20, k, `${m.robot_speed[i]} s/s`, C.TEXT));
            text(8, y + 110, 'Control: Max Speed', C.ACC, 1, A.TL);
        }
    }
    function infoCam(page, y) {
        if (page === 0) {
            infoNetwork(2, y);
            text(8, y + 130, 'CAM System Ready', m.dev[2].online ? C.ON : C.DIM, 1, A.TL);
        } else if (page === 1) {
            if (ui.camView) {
                if (preview) image(Math.trunc((INFO_W - preview.w) / 2), y + Math.trunc((CONT_H - preview.h) / 2), preview);
                else text(INFO_W / 2, y + CONT_H / 2 - 8, previewError || 'Fetching image...', previewError ? C.OFF : C.DIM, 1, A.MC);
                return;
            }
            text(8, y + 36, 'AI Result:', C.DIM, 1, A.TL);
            kvUtf8(y + 58, 'OBJ:', m.ai_object, C.ACC);
            kvUtf8(y + 82, 'CAT:', m.ai_category, C.ACC);
            kv(y + 145, 'Last Update:', m.dev[2].last_seen, C.DIM);
        } else if (page === 2) {
            kv(y + 36, 'Sensor:', m.cam_sensor, C.TEXT);
            kv(y + 56, 'Res:', m.cam_res, C.TEXT);
            kv(y + 76, 'Quality:', `${m.cam_quality}`, C.TEXT);
            text(8, y + 110, 'Control: Cam Config', C.ACC, 1, A.TL);
        } else {
            text(8, y + 36, 'AI Model:', C.DIM, 1, A.TL);
            textUtf8(8, y + 60, utf8Fit(m.ai_model, INFO_W - 16), C.ACC, A.ML);
            text(8, y + 110, 'NEXT cycles the model', C.DIM, 1, A.TL);
        }
    }
    function infoSound(y) {
        const d = m.dev[3];
        kv(y + 36, 'IP:', d.ip || '---', C.TEXT);
        kv(y + 56, 'RSSI:', d.online ? `${d.rssi}dBm` : '---', C.TEXT);
        kv(y + 76, 'Mode:', m.sound_mode, C.ACC);
        kvUtf8(y + 96, 'State:', m.sound_status, C.TEXT);
        hline(6, y + 116, INFO_W - 12, C.SEP);
        kv(y + 128, 'Server:', m.server || '---', C.ACC);
        drawSignal(8, y + 145, m.wifi_rssi, m.wifi_ok);
    }
    function drawInfo() {
        const y = CONT_Y, page = ui.page[ui.tab];
        fillRect(0, y, INFO_W, CONT_H, C.INFO); vline(INFO_W, y, CONT_H, C.SEP);
        if (!(ui.tab === 2 && page === 1 && ui.camView)) {
            text(8, y + 10, DEV_NAMES[ui.tab], C.TEXT, 1, A.TL);
            drawDot(INFO_W - 12, y + 12, m.dev[ui.tab].online);
        }
        [infoDrive, infoRobot, infoCam][ui.tab] ? [infoDrive, infoRobot, infoCam][ui.tab](page, y) : infoSound(y);
    }
    function drawButtons() {
        const b = buildButtons(ui.tab, ui.page[ui.tab]);
        fillRect(INFO_W + 1, CONT_Y, RBTN_W - 1, CONT_H, C.BG);
        if (!b.length) return;
        const bh = Math.trunc(CONT_H / b.length);
        b.forEach((x, i) => {
            const by = CONT_Y + i * bh, pressed = ui.press === 'btn' && ui.pressIndex === i;
            const col = pressed ? C.ON : x.color;
            fillRoundRect(INFO_W + 3, by + 3, RBTN_W - 6, bh - 6, 5, col);
            roundRect(INFO_W + 2, by + 2, RBTN_W - 4, bh - 4, 6, C.SEP);
            text(INFO_W + RBTN_W / 2, by + Math.trunc(bh / 2), x.label, x.action === 'none' ? C.DIM : labelColor(col), 1, A.MC);
        });
    }
    function drawBottom() {
        const y = BOT_Y;
        fillRect(0, y, W, BOT_H, C.PANEL); hline(0, y, W, C.SEP);
        text(8, y + 25, `PAGE ${ui.page[ui.tab] + 1}/${PAGE_COUNT[ui.tab]}`, C.TEXT, 1, A.TL);
        text(8, y + 45, TAB_NAMES[ui.tab], C.ACC, 1, A.TL);
        const mode = { A: 'A(Robot X-Z)', N: 'N(Robot Y)', B: 'B(Drive)' }[m.ctrl_mode] || 'Offline';
        text(8, y + 65, `CTRL: ${mode}`, C.AMBER, 1, A.TL);
        const canPrev = ui.page[ui.tab] > 0, canNext = ui.page[ui.tab] < PAGE_COUNT[ui.tab] - 1;
        const lc = ui.press === 'prev' ? C.ON : canPrev ? C.ACC : C.BTN;
        const rc = ui.press === 'next' ? C.ON : canNext ? C.ACC : C.BTN;
        fillRoundRect(NAV_X, y + 16, NAV_W - 4, 44, 4, lc);
        roundRect(NAV_X, y + 16, NAV_W - 4, 44, 4, C.SEP);
        text(NAV_X + (NAV_W - 4) / 2, y + 38, '<', labelColor(lc), 2, A.MC);
        fillRoundRect(NAV_X + NAV_W, y + 16, NAV_W - 4, 44, 4, rc);
        roundRect(NAV_X + NAV_W, y + 16, NAV_W - 4, 44, 4, C.SEP);
        text(NAV_X + NAV_W + (NAV_W - 4) / 2, y + 38, '>', labelColor(rc), 2, A.MC);
    }
    const approvalVisible = () => m.approval && (m.approval_id !== ui.answeredId || Date.now() - ui.answeredAt > 10000);
    function drawApproval() {
        fillRect(10, 60, 220, 200, C.HDR); rect(10, 60, 220, 200, C.ACC);
        text(120, 80, 'AI Command', C.TEXT, 2, A.MC);
        let rest = m.approval_desc, y = 108;
        for (let line = 0; line < 3 && rest; line++, y += 18) {
            const fit = utf8Fit(rest, 200); if (!fit) break;
            textUtf8(120, y, fit, C.ACC, A.MC); rest = rest.slice(fit.length);
        }
        text(120, 165, 'Approve?', C.TEXT, 2, A.MC);
        fillRoundRect(20, 180, 80, 60, 5, ui.press === 'ok' ? C.ACC : C.ON); text(60, 210, 'OK', C.BG, 2, A.MC);
        fillRoundRect(140, 180, 80, 60, 5, ui.press === 'no' ? C.ACC : C.OFF); text(180, 210, 'NO', C.BG, 2, A.MC);
    }
    function scene() {
        fillRect(0, 0, W, H, C.BG);
        drawHeader();
        if (approvalVisible()) { drawApproval(); return; }
        drawTabs(); drawSubbar(); drawInfo(); drawButtons(); drawBottom();
    }

    // ---- DOM ---------------------------------------------------------------------------------
    let canvas, ctx2d, imgData, sendBox, logLine, view;
    function present() {
        scene();
        const d = imgData.data;
        for (let i = 0; i < fb.length; i++) {
            const c = fb[i], r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
            d[i * 4] = (r << 3) | (r >> 2); d[i * 4 + 1] = (g << 2) | (g >> 4); d[i * 4 + 2] = (b << 3) | (b >> 2); d[i * 4 + 3] = 255;
        }
        ctx2d.putImageData(imgData, 0, 0);
    }
    function log(msg) { if (logLine) logLine.textContent = `${clock(new Date(), true)}  ${msg}`; }

    async function postJson(path, body) {
        try {
            const r = await fetch(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
            return r.status;
        } catch (e) { return -1; }
    }
    async function sendCommand(target, cmd) {
        if (!sendBox.checked) { log(`(미전송) ${COMMAND_PATHS[target]}  "${cmd}"`); return; }
        const s = await postJson(COMMAND_PATHS[target], { command: cmd });
        log(`전송 ${COMMAND_PATHS[target]}  "${cmd}" -> HTTP ${s}`);
        setTimeout(poll, 300);
    }
    async function sendApproval(id, approve) {
        if (!sendBox.checked) { log(`(미전송) /api/hmi/approve  id=${id} ${approve ? 'OK' : 'NO'}`); return; }
        const s = await postJson('/api/hmi/approve', { id, approve });
        log(`전송 /api/hmi/approve  ${approve ? 'OK' : 'NO'} -> HTTP ${s}`);
        setTimeout(poll, 300);
    }
    function hold() { holdUntil = Date.now() + HOLD_MS; }

    function pressButton(b) {
        const t = ui.tab;
        let i;
        switch (b.action) {
            case 'send': sendCommand(t, b.cmd); break;
            case 'refresh': sendCommand(t, b.cmd); setTimeout(poll, 50); break;
            case 'hb': i = (findInt(HB_MS, m.hb_ms) + 1) % HB_MS.length; m.hb_ms = HB_MS[i]; hold(); sendCommand(t, `mzh${HB_MS[i]}`); break;
            case 'i2c': i = (findInt(I2C_HZ, m.i2c_hz) + 1) % I2C_HZ.length; m.i2c_hz = I2C_HZ[i]; hold(); sendCommand(t, `mzc${I2C_HZ[i]}`); break;
            case 'pid': m.pid_mode = !m.pid_mode; hold(); sendCommand(t, m.pid_mode ? 'mzm1' : 'mzm0'); break;
            case 'target': {
                const v = Math.min(Math.max(m.pid_target + b.arg, PID_MIN), PID_MAX);
                m.pid_target = v; hold(); sendCommand(t, `mzt${v}`); break;
            }
            case 'zstep': m.z_step = Math.max(10, m.z_step + b.arg); hold(); sendCommand(t, `mzs${b.arg > 0 ? '+' : ''}${b.arg}`); break;
            case 'speed': {
                const axis = b.arg >> 1, step = [10, 50, 50][axis];
                m.robot_speed[axis] = Math.max(100, m.robot_speed[axis] + (b.arg & 1 ? -step : step)); hold(); sendCommand(t, b.cmd); break;
            }
            case 'view': ui.camView = !ui.camView; if (ui.camView) { preview = null; previewError = null; fetchPreview(); } break;
            case 'sens': i = findStr(CAM_SENSORS, m.cam_sensor); m.cam_sensor = CAM_SENSORS[(i + 1) % 3]; hold(); sendCommand(t, b.cmd); break;
            case 'res': i = findStr(CAM_RES, m.cam_res); m.cam_res = CAM_RES[(i + 1) % 5]; hold(); sendCommand(t, b.cmd); break;
            case 'qual': i = findInt(CAM_QUALITY, m.cam_quality); m.cam_quality = CAM_QUALITY[(i + 1) % 4]; hold(); sendCommand(t, b.cmd); break;
        }
    }
    function flash(what, index = 0) {
        ui.press = what; ui.pressIndex = index;
        setTimeout(() => { ui.press = null; present(); }, 120);
    }
    function setPage(tab, page) {
        if (ui.tab === 2 && ui.camView && (tab !== 2 || page !== 1)) ui.camView = false;
        ui.tab = tab; ui.page[tab] = page;
    }
    function touch(x, y) {
        if (approvalVisible()) {
            if (y < 180 || y > 240) return;
            const ok = x >= 20 && x <= 100, no = x >= 140 && x <= 220;
            if (!ok && !no) return;
            flash(ok ? 'ok' : 'no');
            sendApproval(m.approval_id, ok);
            ui.answeredId = m.approval_id; ui.answeredAt = Date.now();
            return;
        }
        if (y < HDR_H) return;
        if (y < HDR_H + TAB_H) { const tab = Math.trunc(x / (W / 4)); if (tab !== ui.tab) setPage(tab, 0); return; }
        if (y < CONT_Y) return;
        if (y < BOT_Y) {
            if (x < INFO_W) return;
            const b = buildButtons(ui.tab, ui.page[ui.tab]);
            if (!b.length) return;
            const i = Math.trunc((y - CONT_Y) / Math.trunc(CONT_H / b.length));
            if (i < 0 || i >= b.length || b[i].action === 'none') return;
            flash('btn', i); pressButton(b[i]);
            return;
        }
        if (y < BOT_Y + 10 || y > BOT_Y + 66) return;
        const page = ui.page[ui.tab];
        if (x >= NAV_X && x < NAV_X + NAV_W - 4) { flash('prev'); if (page > 0) setPage(ui.tab, page - 1); }
        else if (x >= NAV_X + NAV_W && x <= W - 4) { flash('next'); if (page < PAGE_COUNT[ui.tab] - 1) setPage(ui.tab, page + 1); }
    }

    // ---- data ------------------------------------------------------------------------------
    async function poll() {
        try {
            const r = await fetch('/api/status', { cache: 'no-store' });
            applyStatus(r.ok ? await r.json() : null, r.ok);
        } catch (e) { applyStatus(null, false); }
        if (isVisible()) present();
    }
    // Same scaling as the firmware's ROM TJpgDec: 1/1, 1/2, 1/4 or 1/8 until it fits.
    async function fetchPreview() {
        try {
            const r = await fetch('/api/hmi/last_image', { cache: 'no-store' });
            if (r.status === 404) { preview = null; previewError = 'No image yet'; }
            else if (!r.ok) { preview = null; previewError = 'HTTP error'; }
            else {
                const blob = await r.blob();
                if (blob.size > 40 * 1024) { preview = null; previewError = 'Image too large'; }
                else {
                    const bmp = await createImageBitmap(blob);
                    let s = 0;
                    while (s < 3 && ((bmp.width >> s) > PREVIEW_W || (bmp.height >> s) > PREVIEW_H)) s++;
                    const w = (bmp.width + (1 << s) - 1) >> s, h = (bmp.height + (1 << s) - 1) >> s;
                    if (w > PREVIEW_W || h > PREVIEW_H) { preview = null; previewError = 'Decode error'; }
                    else {
                        const c = new OffscreenCanvas(w, h), cx = c.getContext('2d');
                        cx.drawImage(bmp, 0, 0, w, h);
                        const px = cx.getImageData(0, 0, w, h).data, out = new Uint16Array(w * h);
                        for (let i = 0; i < out.length; i++) out[i] = ((px[i * 4] & 0xF8) << 8) | ((px[i * 4 + 1] & 0xFC) << 3) | (px[i * 4 + 2] >> 3);
                        preview = { w, h, px: out }; previewError = null;
                    }
                }
            }
        } catch (e) { preview = null; previewError = 'HTTP error'; }
        if (isVisible()) present();
    }

    let pollTimer = null, imageTimer = null, clockTimer = null;
    const isVisible = () => view && view.style.display !== 'none';
    function start() {
        if (pollTimer) return;
        poll();
        pollTimer = setInterval(poll, POLL_MS);
        clockTimer = setInterval(present, 1000);
        imageTimer = setInterval(() => { if (ui.camView) fetchPreview(); }, IMAGE_MS);
    }
    function stop() {
        [pollTimer, imageTimer, clockTimer].forEach(clearInterval);
        pollTimer = imageTimer = clockTimer = null;
    }

    function build() {
        const anchor = document.getElementById('simulation-view');
        if (!anchor) return false;
        view = document.createElement('div');
        view.id = 'hmi-sim-view';
        view.style.cssText = 'display:none;flex:1;min-height:600px;background:#0f172a;border-radius:12px;' +
            'flex-direction:column;align-items:center;justify-content:center;gap:12px;padding:16px;box-sizing:border-box;';
        view.innerHTML =
            '<div style="color:#94a3b8;font-size:0.8rem;">HMI 화면 시뮬레이터 · ESP32-2432S028 240×320 · 실시간 /api/status</div>' +
            '<div style="padding:14px;background:#111827;border-radius:14px;box-shadow:0 8px 30px rgba(0,0,0,.45);">' +
            '<canvas width="240" height="320" style="display:block;width:480px;height:640px;max-width:80vw;max-height:70vh;' +
            'aspect-ratio:3/4;image-rendering:pixelated;cursor:pointer;border-radius:4px;"></canvas></div>' +
            '<label style="color:#cbd5e1;font-size:0.8rem;display:flex;align-items:center;gap:6px;">' +
            '<input type="checkbox"> 버튼 누르면 실제 명령 전송 (꺼두면 화면만 바뀝니다)</label>' +
            '<div style="color:#64748b;font-size:0.75rem;font-family:monospace;min-height:1em;"></div>';
        anchor.parentNode.insertBefore(view, anchor.nextSibling);
        canvas = view.querySelector('canvas');
        sendBox = view.querySelector('input');
        logLine = view.lastElementChild;
        ctx2d = canvas.getContext('2d');
        imgData = ctx2d.createImageData(W, H);
        canvas.addEventListener('click', e => {
            const r = canvas.getBoundingClientRect();
            touch(Math.trunc((e.clientX - r.left) * W / r.width), Math.trunc((e.clientY - r.top) * H / r.height));
            present();
        });
        return true;
    }

    document.addEventListener('DOMContentLoaded', () => {
        if (!F || !build()) return;
        const original = window.switchTab;
        if (typeof original !== 'function') return;
        window.switchTab = function (tabId) {
            original.apply(this, arguments);
            if (window.activeDashboardTab === 'hmi_mcu-tab' && !window.BoardUI?.isJC()) { view.style.display = 'flex'; start(); present(); }
            else { view.style.display = 'none'; stop(); }
        };
        log('명령 전송 꺼짐: 버튼은 화면만 바꿉니다');
    });
})();
