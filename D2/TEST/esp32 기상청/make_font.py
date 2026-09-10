import os
from PIL import Image, ImageFont, ImageDraw

def create_gfx_font(ttf_path, font_size, out_path, subset="0123456789%.-: /C℃월일시분초도습풍향속강수량맑구름많흐림비눈안옴서제울산원광주ms데이터수신중음내일모레지금"):
    try:
        font = ImageFont.truetype(ttf_path, font_size)
    except IOError:
        print(f"Error: 폰트를 열 수 없습니다 -> {ttf_path}")
        return

    # 필요한 문자들만 뽑아서 고유 번호(아스키/유니코드) 정렬
    chars = sorted(list(set(c for c in subset)))
    
    # Adafruit_GFX Font struct
    bitmap_data = []      # 픽셀 비트맵 데이터(uint8_t)
    glyphs_data = []      # 각 글자에 대한 포인터 데이터(GFXglyph)
    
    bitmap_offset = 0
    font_name = os.path.splitext(os.path.basename(ttf_path))[0]
    font_name = "".join(c if c.isalnum() else "_" for c in font_name).strip("_")

    c_header = f"// Generated Font: {font_name}_{font_size}\n"
    c_header += f"#pragma once\n\n"
    c_header += f"#include <pgmspace.h>\n\n"
    
    for c in chars:
        # 단일 문자 바운딩 박스를 계산
        try:
            bbox = font.getbbox(c)
        except AttributeError:
            bbox = font.getmask(c).getbbox()
            
        if not bbox:
            bbox = (0,0,0,0)
            
        w = max(1, bbox[2] - bbox[0])
        h = max(1, bbox[3] - bbox[1])
        xOffset = bbox[0]
        yOffset = bbox[1] - font_size
        
        # 실제 글자 그리기
        img = Image.new('L', (w, -yOffset+h+5), 0)
        draw = ImageDraw.Draw(img)
        draw.text((-xOffset, -bbox[1]), c, font=font, fill=255)
        
        # 비트맵 변환 (가로로 8픽셀씩 묶음, 패딩 안함 -> GFX 규칙)
        pixels = list(img.getdata())
        byte_val = 0
        bits_count = 0
        p_bytes = []
        
        for p in pixels:
            byte_val <<= 1
            if p > 127: byte_val |= 1
            bits_count += 1
            if bits_count == 8:
                p_bytes.append(byte_val)
                byte_val = 0
                bits_count = 0
        if bits_count > 0: # 남은 비트 채우기
            byte_val <<= (8 - bits_count)
            p_bytes.append(byte_val)
            
        # 글립 데이터 저장 (Code, bitmapOffset, w, h, xAdvance, xOffset, yOffset)
        advance = font.getbbox(c)[2]
        
        # 유니코드 값 저장 (UTF-8 인코딩이 아닌 원래 코드)
        code = ord(c)
        
        # 문자 하나 비트맵 담기
        bitmap_data.extend(p_bytes)
        glyphs_data.append((c, code, bitmap_offset, w, h, advance, xOffset, yOffset))
        bitmap_offset += len(p_bytes)

    # C 배열 문자열화
    c_header += f"const uint8_t {font_name}_{font_size}ptBitmaps[] PROGMEM = {{\n"
    lines = []
    for i in range(0, len(bitmap_data), 12):
        chunk = bitmap_data[i:i+12]
        lines.append("  " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    c_header += "\n".join(lines) + "\n};\n\n"
    
    # 여기서 GFX 호환은 보통 연속된 아스키 문자를 요구하므로,
    # 우리는 커스텀하게 자체 렌더링 함수에 넣을 수 있는 구조체를 정의합니다.
    c_header += "#ifndef CUSTOM_GFX_GLYPH_DEFINED\n"
    c_header += "#define CUSTOM_GFX_GLYPH_DEFINED\n"
    c_header += "typedef struct {\n"
    c_header += "  uint32_t code;\n"
    c_header += "  uint16_t bitmapOffset;\n"
    c_header += "  uint8_t  width, height;\n"
    c_header += "  uint8_t  xAdvance;\n"
    c_header += "  int8_t   xOffset, yOffset;\n"
    c_header += "} CustomGFXGlyph;\n"
    c_header += "#endif\n\n"

    c_header += f"const CustomGFXGlyph {font_name}_{font_size}ptGlyphs[] PROGMEM = {{\n"
    for g in glyphs_data:
        c, code, b_off, w, h, xAdv, xOff, yOff = g
        c_header += f"  {{ 0x{code:04X}, {b_off:5d}, {w:3d}, {h:3d}, {xAdv:3d}, {xOff:3d}, {yOff:3d} }}, // '{c}'\n"
    c_header += "};\n\n"
    
    c_header += f"const int {font_name}_{font_size}pt_NumGlyphs = {len(glyphs_data)};\n"
    
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(c_header)
    
    print(f"Generated: {out_path} ({len(glyphs_data)} glyphs, {len(bitmap_data)} bytes bitmap)")

if __name__ == '__main__':
    # 1. 작은 폰트(상세 정보용)
    create_gfx_font('HancomSans-SemiBold_0.ttf', 16, 'src/HancomSans16.h')
    # 2. 큰 폰트(온도, 시계용)
    create_gfx_font('HancomSans-SemiBold_0.ttf', 40, 'src/HancomSans40.h')
    # 3. 중간 폰트(현재 날씨 텍스트)
    create_gfx_font('HancomSans-SemiBold_0.ttf', 24, 'src/HancomSans24.h')
