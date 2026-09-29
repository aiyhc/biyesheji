#pragma once

#include <stdint.h>

/**
 * @brief 一个 16x16 的汉字字模
 *
 * data 共 32 字节，排列方式与 ASCII 字库 OLED_F8x16 一致：
 *   - data[0..15] ：上半部分（第 1 页）16 列的像素
 *   - data[16..31]：下半部分（第 2 页）16 列的像素
 * 每个字节代表一列中自上而下的 8 个像素，bit0 在最上方、bit7 在最下方。
 * 屏幕上一个汉字占 2 个字符位（16 像素宽），即一行最多显示 8 个汉字。
 */
typedef struct {
	const char    *name;	/* 该汉字对应的 UTF-8 字符串（3 字节 + '\0'） */
	const uint8_t  data[32];	/* 32 字节点阵数据 */
} OLED_ChineseFont_t;

/* 中文字库表（定义在 OLED_ChineseFont.c，由脚本 tools/gen_chinese_font.py 生成） */
extern const OLED_ChineseFont_t OLED_ChineseFont[];

/* 字库中汉字的个数 */
extern const uint8_t OLED_ChineseFontCount;
