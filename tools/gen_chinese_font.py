# -*- coding: utf-8 -*-
"""
生成 OLED 用的 16x16 中文字模（SSD1306 页格式）

用法：
    python tools/gen_chinese_font.py            # 生成字模并打印预览
    python tools/gen_chinese_font.py --art      # 只打印预览，不写文件

说明：
    - 依赖 Pillow：pip install pillow
    - 字模排列与 ASCII 字库 (OLED_F8x16) 保持一致：
      每个汉字 32 字节 = 上半部分（16 列）+ 下半部分（16 列），
      每列 1 字节，bit0 在该页的最上方，bit7 在最下方。
    - 在屏幕上一个汉字占 2 个字符位（16 像素宽），一行最多显示 8 个汉字。
    - 输出文件：module/src/OLED_ChineseFont.c
      新增汉字只需修改下面的 CHARS 列表后重新运行本脚本。
"""

import argparse
import os
import sys

# 需要生成字模的汉字列表（按需增删）
CHARS = list(dict.fromkeys(
    "蓝牙已连接断开等待初始化失败调试启动界面系统中主照度温度湿度传感器未窗帘控制电机接入配置调节当前目标关闭环境数据光自动联动设置时间水位水箱设备状态雾化按键说明上一页下一页网络同步低报警未知运行语音交互" 
    "消息连接首页开机显示模式开关风扇返回菜单信息恢复出厂是否正常提醒及时加水过"
))

# 候选字体（按顺序取第一个存在的），16 像素下宋体最清晰
FONT_CANDIDATES = [
    r"C:\Windows\Fonts\simsun.ttc",
    r"C:\Windows\Fonts\simhei.ttf",
    r"C:\Windows\Fonts\msyh.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
]

FONT_SIZE = 16          # 字号（像素），中文点阵取 16
THRESHOLD = 128         # 二值化阈值
OUTPUT = os.path.join("module", "src", "OLED_ChineseFont.c")


def pick_font():
    """选择一个可用的中文字体文件"""
    for path in FONT_CANDIDATES:
        if os.path.exists(path):
            return path
    sys.exit("找不到可用的中文字体，请修改 FONT_CANDIDATES")


def render(char, font):
    """把单个汉字渲染成 16x16 的 0/1 点阵，返回 [[int]] (行优先)"""
    from PIL import Image, ImageDraw

    image = Image.new("L", (FONT_SIZE, FONT_SIZE), 0)
    draw = ImageDraw.Draw(image)
    # anchor="mm" 按字体度量居中，保证所有汉字在同一基线上对齐
    draw.text((FONT_SIZE / 2, FONT_SIZE / 2), char, font=font,
              fill=255, anchor="mm")

    pixels = image.load()
    return [[1 if pixels[x, y] >= THRESHOLD else 0 for x in range(FONT_SIZE)]
            for y in range(FONT_SIZE)]


def pack(dots):
    """把 16x16 点阵打包成 32 字节（上半 16 列 + 下半 16 列，LSB 在上）"""
    data = []
    for half in (0, 1):                     # 0=上半（行 0~7），1=下半（行 8~15）
        for x in range(FONT_SIZE):
            byte = 0
            for bit in range(8):
                if dots[half * 8 + bit][x]:
                    byte |= (1 << bit)
            data.append(byte)
    return data


def show_art(char, dots):
    """在终端打印点阵预览，便于人工检查字形"""
    print(f"  {char}")
    for row in dots:
        print("    " + "".join("##" if v else ".." for v in row))
    print()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--art", action="store_true", help="只预览，不生成文件")
    args = parser.parse_args()

    from PIL import ImageFont

    font_path = pick_font()
    font = ImageFont.truetype(font_path, FONT_SIZE, index=0)
    print(f"使用字体：{font_path}\n")

    glyphs = []
    for char in CHARS:
        dots = render(char, font)
        glyphs.append((char, pack(dots)))
        if args.art:
            show_art(char, dots)

    if args.art:
        return

    lines = [
        '#include "OLED_ChineseFont.h"',
        "",
        "/*",
        " * 本文件由 tools/gen_chinese_font.py 自动生成，请勿手工修改。",
        " * 如需增删汉字，请修改脚本中的 CHARS 列表后重新生成。",
        " */",
        "",
        f"const uint8_t OLED_ChineseFontCount = {len(glyphs)};",
        "",
        "const OLED_ChineseFont_t OLED_ChineseFont[] = {",
    ]
    for index, (char, data) in enumerate(glyphs):
        lines.append(f'\t{{ "{char}", {{')
        for offset in range(0, len(data), 8):
            row = ", ".join(f"0x{b:02X}" for b in data[offset:offset + 8])
            lines.append(f"\t\t{row},")
        lines.append(f"\t}} }}, /* {index}: {char} */")
    lines.append("};")
    lines.append("")

    with open(OUTPUT, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))

    print(f"已生成 {OUTPUT}（{len(glyphs)} 个汉字，共 {len(glyphs) * 32} 字节）")


if __name__ == "__main__":
    main()
