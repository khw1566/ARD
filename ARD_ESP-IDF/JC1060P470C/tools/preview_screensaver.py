"""Approximate preview of main/ui_screensaver.cpp (pictogram screensaver) with Pillow.

Mirrors the C++ geometry and timeline line by line. It is NOT the LVGL renderer:
fonts (IBM Plex Sans KR instead of Montserrat/Nanum Gothic) and anti-aliasing differ.
Usage: python preview_screensaver.py <font.ttf> <out_dir>
"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

W, H, BAR = 1024, 600, 72
SS = 2  # supersampling
SLIDE_MS, TAP_MS, STAGE_Y = 7000, 700, 76
BG, INK, TEAL, DIM, RED = 0x101E2C, 0xF2F6FA, 0x46D6BE, 0x455565, 0xE5484D
RED_PRESSED, AMBER, GREEN, GREY = 0x8E1B16, 0xF5B83D, 0x1E9E50, 0x5F6B76
CIRCLE = 10000
TIP_X, HAND_LINE = 27, 0x2A3B4C
KO = {
    "Explore the robot": "로봇을 직접 체험해 보세요",
    "Touch the screen to begin. Choose a tab to explore the controls.": "화면을 터치해 시작하세요. 원하는 탭에서 제어 기능을 살펴보세요.",
    "Move with the joystick": "조이스틱으로 움직여 보세요",
    "In Drive, press RE-ARM, then hold the joystick to move. Release to stop.": "구동 탭에서 재가동을 누른 뒤 조이스틱을 누르고 움직이세요. 손을 떼면 멈춥니다.",
    "See how sorting works": "자동 선별 과정을 살펴보세요",
    "In Main, check the sorting sequence and camera before starting.": "메인 탭에서 선별 순서와 카메라를 확인한 뒤 시작하세요.",
    "Stay clear of moving parts": "움직이는 장치에서 손을 떼세요",
    "Keep hands away from the robot. Press E-STOP if you need to stop it.": "로봇 가까이에 손을 넣지 마세요. 멈춰야 할 때는 비상정지를 누르세요.",
    "Help animation only. The robot does not move with this animation.": "도움말 애니메이션입니다. 이 동작에 따라 실제 로봇이 움직이지는 않습니다.",
    "E-STOP": "비상정지", "RE-ARM": "재가동",
}
SLIDES = [
    ("Explore the robot", "Touch the screen to begin. Choose a tab to explore the controls."),
    ("Move with the joystick", "In Drive, press RE-ARM, then hold the joystick to move. Release to stop."),
    ("See how sorting works", "In Main, check the sorting sequence and camera before starting."),
    ("Stay clear of moving parts", "Keep hands away from the robot. Press E-STOP if you need to stop it."),
]
RIGHT_CHEVRON = [(0, 0), (12, 14), (0, 28)]
LEFT_CHEVRON = [(12, 0), (0, 14), (12, 28)]
CHECK = [(0, 12), (9, 21), (24, 3)]
SLASH = [(0, 0), (84, 84)]
TRIANGLE = [(4, 62), (38, 4), (72, 62), (4, 62)]
UP_ARROW = [(18, 44), (18, 4), (4, 18), (18, 4), (32, 18)]


class Obj:
    def __init__(self, parent, x, y, w, h, kind='rect'):
        self.parent, self.x, self.y, self.w, self.h, self.kind = parent, x, y, w, h, kind
        self.radius, self.bg, self.bg_opa, self.border, self.border_color, self.border_opa = 0, BG, 255, 0, INK, 255
        self.hidden, self.children = False, []
        self.points, self.line_w, self.line_color, self.text, self.font, self.color, self.align = None, 0, INK, '', 20, INK, 'center'
        if parent:
            parent.children.append(self)

    def origin(self):
        if not self.parent:
            return 0, 0
        px, py = self.parent.origin()
        return px + self.parent.x + self.parent.border, py + self.parent.y + self.parent.border


def shape(parent, x, y, w, h, radius, color):
    o = Obj(parent, x, y, w, h)
    o.radius, o.bg = radius, color
    return o


def group(parent, x, y, w, h):
    o = shape(parent, x, y, w, h, 0, BG)
    o.bg_opa = 0
    return o


def ring(parent, x, y, w, h, border, radius, color):
    o = group(parent, x, y, w, h)
    o.radius, o.border, o.border_color = radius, border, color
    return o


def stroke(parent, points, x, y, width, color):
    o = Obj(parent, x, y, 0, 0, 'line')
    o.points, o.line_w, o.line_color = points, width, color
    return o


def caption(parent, text, x, y, w, font, color, align='center'):
    o = Obj(parent, x, y, w, 0, 'label')
    o.text, o.font, o.color, o.align = text, font, color, align
    return o


def ease(t, t0, t1, a, b):
    if t <= t0:
        return a
    if t >= t1:
        return b
    p = (t - t0) * 1024 // (t1 - t0)
    s = p * p // 1024 * (3072 - 2 * p) // 1024
    return a + int((b - a) * s / 1024)


def linear(t, t0, t1, a, b):
    if t <= t0:
        return a
    if t >= t1:
        return b
    return a + int((b - a) * (t - t0) / (t1 - t0))


def within(t, a, b):
    return a <= t < b


def tap_age(t, tap):
    return t - tap if within(t, tap, tap + TAP_MS) else -1


def follow(path, t):
    x, y = path[0][1], path[0][2]
    for i in range(1, len(path)):
        if t < path[i - 1][0]:
            break
        x = ease(t, path[i - 1][0], path[i][0], path[i - 1][1], path[i][1])
        y = ease(t, path[i - 1][0], path[i][0], path[i - 1][2], path[i][2])
    return x, y


class Saver:
    def __init__(self):
        self.screen = shape(None, 0, 0, W, H, 0, 0xFFFFFF)
        bar = shape(self.screen, 0, 0, W, BAR, 0, 0x1F2A36)
        caption(bar, 'ARD', 12, 20, 70, 28, INK, 'left')
        b = shape(bar, 526, 8, 128, 56, 8, 0x0B6E99); caption(b, '화면 보호기', 0, 17, 128, 16, INK)
        b = shape(bar, 1024 - 20 - 180 - 120 - 8, 8, 120, 56, 8, GREY); caption(b, '재가동', 0, 16, 120, 20, INK)
        b = shape(bar, 1024 - 20 - 180, 6, 180, 60, 8, 0xB3261E); caption(b, '비상정지', 0, 14, 180, 28, INK)
        cover = self.cover = shape(self.screen, 0, BAR, W, H - BAR, 0, BG)
        self.heading = caption(cover, '', 232, 22, W - 464, 28, INK)
        caption(cover, 'E-STOP', 760, 16, 136, 20, RED, 'right')
        self.arrow = stroke(cover, UP_ARROW, 904, 4, 6, RED)
        self.build_touch(); self.build_drive(); self.build_sort(); self.build_safety()
        self.ripple = ring(cover, 0, 0, 24, 24, 5, CIRCLE, TEAL)
        self.hand = group(cover, 0, 0, 108, 150)

        def part(x, y, w, h, r):
            o = shape(self.hand, x, y, w, h, r, INK)
            o.border, o.border_color = 3, HAND_LINE
        part(12, 64, 92, 62, 20)
        for i in range(3): part(38 + i * 22, 50, 24, 36, 12)
        part(14, 0, 26, 88, 13)
        shape(self.hand, 20, 6, 14, 14, 5, 0xD5E1EB); shape(self.hand, 18, 46, 18, 3, 1, HAND_LINE)
        part(0, 80, 46, 24, 12)
        shape(self.hand, 20, 124, 76, 24, 6, TEAL)
        self.help = caption(cover, '', 48, 398, W - 96, 20, 0xD5E1EB)
        self.dots = [shape(cover, (W - 110) // 2 + i * 32, 460, 14, 14, CIRCLE, DIM) for i in range(4)]
        self.progress = shape(cover, (W - 720) // 2, 482, 1, 4, 2, TEAL)
        caption(cover, 'Help animation only. The robot does not move with this animation.', 48, 496, W - 96, 16, 0xA7BCCB)

    def stage(self):
        root = group(self.cover, 0, STAGE_Y, W, 316)
        root.hidden = True
        return root

    def gantry(self, parent, x, chevrons=False):
        shape(parent, x - 10, 262, 384, 8, 4, DIM); shape(parent, x, 56, 14, 206, 4, INK)
        shape(parent, x + 350, 56, 14, 206, 4, INK); shape(parent, x, 56, 364, 16, 6, INK)
        c = group(parent, x + 117, 72, 130, 110)
        shape(c, 30, 0, 70, 46, 10, TEAL); shape(c, 59, 46, 12, 50, 4, INK); shape(c, 45, 92, 40, 12, 6, INK)
        if chevrons:
            return c, stroke(c, RIGHT_CHEVRON, 110, 9, 6, TEAL), stroke(c, LEFT_CHEVRON, 8, 9, 6, TEAL)
        return c

    def build_touch(self):
        r = self.touch_root = self.stage()
        ring(r, 362, 16, 300, 200, 8, 18, INK); shape(r, 482, 216, 60, 20, 0, INK); shape(r, 432, 234, 160, 12, 6, INK)
        tabs = [shape(r, 386 + i * 88, 40, 76, 28, 6, DIM) for i in range(3)]
        self.tab = tabs[1]
        self.content = shape(r, 386, 82, 252, 110, 8, DIM)

    def touch_frame(self, t):
        path = [(0, 760, 176), (1100, 512, 136), (2400, 512, 136), (3300, 512, 54), (5200, 512, 54), (6300, 760, 176)]
        x, y = follow(path, t)
        self.content.bg = TEAL if t >= 1300 else DIM
        self.tab.bg = TEAL if t >= 3500 else DIM
        return True, x, y, within(t, 1200, 1450) or within(t, 3400, 3650), tap_age(t, 1200) if t < 3400 else tap_age(t, 3400)

    def build_drive(self):
        r = self.drive_root = self.stage()
        self.rearm = shape(r, 70, 128, 150, 60, 12, GREY); caption(self.rearm, 'RE-ARM', 0, 17, 150, 20, INK)
        ring(r, 290, 84, 150, 150, 8, CIRCLE, INK); shape(r, 361, 108, 8, 102, 4, DIM); shape(r, 314, 155, 102, 8, 4, DIM)
        self.knob = shape(r, 333, 127, 64, 64, CIRCLE, TEAL)
        self.carriage, self.right, self.left = self.gantry(r, 580, True)

    def drive_frame(self, t):
        self.rearm.bg = GREEN if t >= 800 else GREY
        knob = ease(t, 2000, 2400, 0, 40)
        if t >= 4200: knob = ease(t, 4200, 4700, 40, -40)
        if t >= 5600: knob = ease(t, 5600, 5900, -40, 0)
        self.knob.x = 333 + knob
        c = linear(t, 2200, 4300, 0, 110)
        if t >= 4300: c = linear(t, 4300, 5600, 110, -50)
        self.carriage.x = 580 + 117 + c
        self.right.hidden = not (within(t, 2200, 4300) and (t // 250) % 2 == 0)
        self.left.hidden = not (within(t, 4500, 5600) and (t // 250) % 2 == 0)
        path = [(0, 145, 250), (600, 145, 158), (1200, 145, 158), (2000, 365, 159), (5600, 365, 159), (6500, 365, 250)]
        x, y = follow(path, t)
        if within(t, 2000, 5600): x = 365 + knob
        if 5600 <= t < 6500: x = ease(t, 5600, 5900, 365 - 40, 365)
        return True, x, y, within(t, 700, 950) or within(t, 2000, 5600), tap_age(t, 700)

    def build_sort(self):
        r = self.sort_root = self.stage()
        shape(r, 150, 18, 140, 4, 2, DIM)
        self.steps = []
        for i in range(3):
            s = shape(r, 130 + i * 70, 0, 40, 40, CIRCLE, DIM); caption(s, str(i + 1), 0, 9, 40, 20, INK)
            self.steps.append(s)
        self.beam = shape(r, 487, 130, 50, 76, 6, TEAL); self.beam.bg_opa = 76
        shape(r, 492, 52, 40, 16, 4, INK); shape(r, 462, 64, 100, 66, 12, INK)
        self.lens = shape(r, 489, 74, 46, 46, CIRCLE, DIM); self.lens.border, self.lens.border_color = 7, BG
        shape(r, 130, 250, 590, 12, 6, INK)
        for i in range(5): shape(r, 150 + i * 130, 266, 18, 18, CIRCLE, DIM)
        walls = [(0, 0, 10, 90), (100, 0, 10, 90), (0, 80, 110, 10)]
        self.bin = []
        for wx, wy, ww, wh in walls:
            self.bin.append(shape(r, 760 + wx, 196 + wy, ww, wh, 4, DIM)); shape(r, 890 + wx, 196 + wy, ww, wh, 4, DIM)
        self.item = shape(r, 150, 206, 44, 44, CIRCLE, AMBER)
        self.badge = shape(r, 0, 0, 40, 40, CIRCLE, GREEN); stroke(self.badge, CHECK, 8, 9, 5, INK)

    def sort_frame(self, t):
        step = 0 if t < 1500 else 1 if t < 3200 else 2
        for i, s in enumerate(self.steps): s.bg = TEAL if i == step else DIM
        self.beam.hidden = not (within(t, 1600, 2800) and (t // 200) % 3 != 0)
        self.lens.bg = TEAL if within(t, 1700, 1850) or within(t, 2300, 2450) else DIM
        x, y = ease(t, 0, 1500, 150, 490), 206
        if t >= 3300: x = ease(t, 3300, 4000, 490, 720)
        if t >= 4000:
            x = ease(t, 4000, 4500, 720, 793)
            y = ease(t, 4000, 4300, 206, 150) if t < 4300 else ease(t, 4300, 4700, 150, 232)
        self.item.x, self.item.y = x, y
        self.badge.hidden = t < 2800
        self.badge.x, self.badge.y = x + 30, y - 28
        for wall in self.bin: wall.bg = TEAL if t >= 4700 else DIM
        return False, 0, 0, False, -1

    def build_safety(self):
        r = self.safety_root = self.stage()
        self.s_carriage = self.gantry(r, 110)
        stroke(r, TRIANGLE, 516, 0, 7, AMBER); caption(r, '!', 516, 22, 76, 28, AMBER)
        ring(r, 770, 70, 150, 150, 14, CIRCLE, AMBER)
        self.estop = shape(r, 789, 89, 112, 112, CIRCLE, RED); caption(self.estop, 'E-STOP', 0, 44, 112, 20, INK)
        self.no_sign = ring(r, 510, 125, 130, 130, 12, CIRCLE, RED); stroke(self.no_sign, SLASH, 11, 11, 12, RED)

    @staticmethod
    def sway(t):
        p = t % 2400
        return ease(p, 0, 1200, -100, 100) if p < 1200 else ease(p, 1200, 2400, 100, -100)

    def safety_frame(self, t):
        stop_at = 4300
        self.s_carriage.x = 110 + 117 + self.sway(min(t, stop_at))
        self.no_sign.hidden = not (within(t, 1200, 2800) and (t // 300) % 4 != 3)
        self.estop.bg = RED_PRESSED if t >= stop_at else RED
        path = [(0, 800, 180), (1300, 562, 150), (2000, 562, 150), (2800, 690, 180), (3000, 690, 180),
                (4000, 845, 140), (5600, 845, 140), (6600, 960, 190)]
        x, y = follow(path, t)
        return True, x, y, within(t, 4200, 4500), tap_age(t, 4200)

    def animate(self, elapsed):
        slide, t = (elapsed // SLIDE_MS) % 4, elapsed % SLIDE_MS
        self.heading.text, self.help.text = SLIDES[slide]
        for i, (d, root) in enumerate(zip(self.dots, [self.touch_root, self.drive_root, self.sort_root, self.safety_root])):
            d.bg, root.hidden = (TEAL if i == slide else DIM), i != slide
        visible, x, y, pressed, age = [self.touch_frame, self.drive_frame, self.sort_frame, self.safety_frame][slide](t)
        self.hand.hidden = not visible
        tip_y = STAGE_Y + y + (8 if pressed else 0)
        if visible: self.hand.x, self.hand.y = x - TIP_X, tip_y
        self.ripple.hidden = not (visible and age >= 0)
        if visible and age >= 0:
            size = 24 + age * 130 // TAP_MS
            self.ripple.w = self.ripple.h = size
            self.ripple.x, self.ripple.y = x - size // 2, tip_y - size // 2
            self.ripple.border_opa = 255 - age * 255 // TAP_MS
        bob = elapsed % 1200
        self.arrow.y = 4 + (bob if bob < 600 else 1200 - bob) * 8 // 600
        self.progress.w = 1 + t * 719 // SLIDE_MS


def rgba(c, a=255):
    return ((c >> 16) & 255, (c >> 8) & 255, c & 255, a)


def render(saver, font_path):
    img = Image.new('RGBA', (W * SS, H * SS))
    fonts = {}

    def font(size):
        if size not in fonts:
            fonts[size] = ImageFont.truetype(font_path, size * SS)
        return fonts[size]

    def draw(o):
        if o.hidden:
            return
        ox, oy = o.origin()
        x, y = (ox + o.x) * SS, (oy + o.y) * SS
        translucent = o.kind == 'rect' and (0 < o.bg_opa < 255 or (o.border and o.border_opa < 255))
        layer = Image.new('RGBA', img.size) if translucent else img
        d = ImageDraw.Draw(layer)
        if o.kind == 'rect':
            w, h = o.w * SS, o.h * SS
            r = min(o.radius * SS, min(w, h) // 2)
            if o.bg_opa:
                d.rounded_rectangle([x, y, x + w - 1, y + h - 1], r, fill=rgba(o.bg, o.bg_opa))
            if o.border:
                d.rounded_rectangle([x, y, x + w - 1, y + h - 1], r, outline=rgba(o.border_color, o.border_opa), width=o.border * SS)
        elif o.kind == 'line':
            pts = [(x + px * SS, y + py * SS) for px, py in o.points]
            lw = o.line_w * SS
            d.line(pts, fill=rgba(o.line_color), width=lw, joint='curve')
            for px, py in pts:
                d.ellipse([px - lw / 2, py - lw / 2, px + lw / 2, py + lw / 2], fill=rgba(o.line_color))
        elif o.kind == 'label' and o.text:
            text = KO.get(o.text, o.text)
            f = font(o.font)
            # Simple word wrap within w.
            lines, cur = [], ''
            for word in text.split(' '):
                trial = (cur + ' ' + word).strip()
                if d.textlength(trial, font=f) <= o.w * SS or not cur:
                    cur = trial
                else:
                    lines.append(cur); cur = word
            lines.append(cur)
            for i, ln in enumerate(lines):
                tw = d.textlength(ln, font=f)
                lx = x if o.align == 'left' else x + (o.w * SS - tw) / 2 if o.align == 'center' else x + o.w * SS - tw
                d.text((lx, y + i * o.font * 1.25 * SS), ln, font=f, fill=rgba(o.color))
        if translucent:
            img.alpha_composite(layer)
        for c in o.children:
            draw(c)

    draw(saver.screen)
    return img.resize((W, H), Image.LANCZOS).convert('RGB')


def main():
    font_path, out = sys.argv[1], Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    saver = Saver()
    # Key frames: 3 per scene for the contact sheet.
    keys = [(0, 1350), (0, 3550), (1, 900), (1, 3200), (1, 5000), (2, 2000), (2, 3000), (2, 5200), (3, 1700), (3, 4450)]
    thumbs = []
    for slide, t in keys:
        saver.animate(slide * SLIDE_MS + t)
        frame = render(saver, font_path)
        frame.save(out / f'scene{slide + 1}_{t:04d}ms.png')
        thumbs.append(frame)
    tw, th = W // 2, H // 2
    sheet = Image.new('RGB', (tw * 2, th * ((len(thumbs) + 1) // 2)), 'white')
    for i, f in enumerate(thumbs):
        sheet.paste(f.resize((tw, th), Image.LANCZOS), ((i % 2) * tw, (i // 2) * th))
    sheet.save(out / 'contact_sheet.png')
    # Animated preview: full 28 s loop at 10 fps, half size.
    frames = []
    for ms in range(0, SLIDE_MS * 4, 100):
        saver.animate(ms)
        frames.append(render(saver, font_path).resize((tw, th), Image.LANCZOS).convert('P', palette=Image.ADAPTIVE))
    frames[0].save(out / 'screensaver_preview.gif', save_all=True, append_images=frames[1:], duration=100, loop=0)


if __name__ == '__main__':
    main()
