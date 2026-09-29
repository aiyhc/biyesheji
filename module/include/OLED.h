#ifndef __OLED_H
#define __OLED_H

#include <stdint.h>

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String);

/**
 * @brief  OLED显示一行定长字符串（不足部分用空格补齐，超长截断）
 *
 * 支持 ASCII 与汉字混排：汉字需已在 OLED_ChineseFont 字库中（占 2 个字符位），
 * 字库中没有的汉字会显示为空白。SSD1306 不会自动清除旧字符，直接覆盖显示
 * 短字符串会留下残影，本函数把不足一行的部分用空格填满，适合反复刷新的场合。
 *
 * @param  Line 行位置，范围：1~4
 * @param  Column 起始列位置，范围：1~16
 * @param  String 要显示的字符串（最长显示到本行末尾）
 */
void OLED_ShowStringPad(uint8_t Line, uint8_t Column, const char *String);

/**
 * @brief  OLED显示一个 16x16 图标
 *
 * @param  Line 行位置，范围：1~4
 * @param  Column 起始列位置，范围：1~8（图标占 2 个字符位）
 * @param  Icon 图标点阵（32 字节，排列同汉字字模）；传 NULL 表示清除该区域
 */
void OLED_ShowIcon(uint8_t Line, uint8_t Column, const uint8_t *Icon);

/**
 * @brief  OLED显示一个 16x16 汉字
 *
 * @param  Line 行位置，范围：1~4
 * @param  Column 起始列位置，范围：1~8（汉字占 2 个字符位）
 * @param  Index 汉字在字库 OLED_ChineseFont 中的序号
 */
void OLED_ShowChinese(uint8_t Line, uint8_t Column, uint8_t Index);

/**
 * @brief  OLED显示汉字/ASCII 混排字符串（UTF-8 编码，不补空格）
 *
 * 汉字按字库逐个查找，不在字库中的汉字显示为空白；超出本行宽度的部分被丢弃。
 * 若需要清掉一行中的残留字符，请使用 OLED_ShowStringPad()。
 *
 * @param  Line 行位置，范围：1~4
 * @param  Column 起始列位置，范围：1~16
 * @param  String 要显示的 UTF-8 字符串
 */
void OLED_ShowChineseStr(uint8_t Line, uint8_t Column, const char *String);
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length);
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);

#endif
