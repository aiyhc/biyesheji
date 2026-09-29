#include "OLED.h"
#include "OLED_Font.h"
#include "OLED_ChineseFont.h"
#include <string.h>
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*引脚配置：SCL接GPIO17，SDA接GPIO18*/
#define OLED_SCL_PIN		17
#define OLED_SDA_PIN		18

/*一行可显示的字符数（8x16 字模，128/8 = 16）*/
#define OLED_LINE_WIDTH		16

#define OLED_W_SCL(x)		gpio_set_level(OLED_SCL_PIN, (x))
#define OLED_W_SDA(x)		gpio_set_level(OLED_SDA_PIN, (x))

/*I2C位延时（内部上拉阻值大、上升沿慢，延时取10us保证时序）*/
#define OLED_I2C_DELAY()	esp_rom_delay_us(10)

/*引脚初始化：开漏输出+内部上拉，与STM32的GPIO_Mode_Out_OD一致*/
void OLED_I2C_Init(void)
{
	gpio_reset_pin(OLED_SCL_PIN);
	gpio_reset_pin(OLED_SDA_PIN);

	/*开漏模式：输出1时释放总线，由内部上拉拉高；输出0时拉低*/
	gpio_set_direction(OLED_SCL_PIN, GPIO_MODE_OUTPUT_OD);
	gpio_set_direction(OLED_SDA_PIN, GPIO_MODE_OUTPUT_OD);
	gpio_set_pull_mode(OLED_SCL_PIN, GPIO_PULLUP_ONLY);
	gpio_set_pull_mode(OLED_SDA_PIN, GPIO_PULLUP_ONLY);

	OLED_W_SCL(1);
	OLED_W_SDA(1);
}

/**
  * @brief  I2C开始
  * @param  无
  * @retval 无
  */
void OLED_I2C_Start(void)
{
	OLED_W_SDA(1);
	OLED_W_SCL(1);
	OLED_I2C_DELAY();
	OLED_W_SDA(0);
	OLED_I2C_DELAY();
	OLED_W_SCL(0);
}

/**
  * @brief  I2C停止
  * @param  无
  * @retval 无
  */
void OLED_I2C_Stop(void)
{
	OLED_W_SDA(0);
	OLED_W_SCL(1);
	OLED_I2C_DELAY();
	OLED_W_SDA(1);
	OLED_I2C_DELAY();
}

/**
  * @brief  I2C发送一个字节
  * @param  Byte 要发送的一个字节
  * @retval 无
  */
void OLED_I2C_SendByte(uint8_t Byte)
{
	uint8_t i;
	for (i = 0; i < 8; i++)
	{
		OLED_W_SDA(!!(Byte & (0x80 >> i)));
		OLED_I2C_DELAY();
		OLED_W_SCL(1);
		OLED_I2C_DELAY();
		OLED_W_SCL(0);
		OLED_I2C_DELAY();
	}
	OLED_W_SCL(1);	//额外的一个时钟，不处理应答信号
	OLED_I2C_DELAY();
	OLED_W_SCL(0);
}

/**
  * @brief  OLED写命令
  * @param  Command 要写入的命令
  * @retval 无
  */
void OLED_WriteCommand(uint8_t Command)
{
	OLED_I2C_Start();
	OLED_I2C_SendByte(0x78);		//从机地址
	OLED_I2C_SendByte(0x00);		//写命令
	OLED_I2C_SendByte(Command);
	OLED_I2C_Stop();
}

/**
  * @brief  OLED写数据
  * @param  Data 要写入的数据
  * @retval 无
  */
void OLED_WriteData(uint8_t Data)
{
	OLED_I2C_Start();
	OLED_I2C_SendByte(0x78);		//从机地址
	OLED_I2C_SendByte(0x40);		//写数据
	OLED_I2C_SendByte(Data);
	OLED_I2C_Stop();
}

/**
  * @brief  OLED设置光标位置
  * @param  Y 以左上角为原点，向下方向的坐标，范围：0~7
  * @param  X 以左上角为原点，向右方向的坐标，范围：0~127
  * @retval 无
  */
void OLED_SetCursor(uint8_t Y, uint8_t X)
{
	OLED_WriteCommand(0xB0 | Y);					//设置Y位置
	OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));	//设置X位置高4位
	OLED_WriteCommand(0x00 | (X & 0x0F));			//设置X位置低4位
}

/**
  * @brief  OLED清屏
  * @param  无
  * @retval 无
  */
void OLED_Clear(void)
{
	uint8_t i, j;
	for (j = 0; j < 8; j++)
	{
		OLED_SetCursor(j, 0);
		for(i = 0; i < 128; i++)
		{
			OLED_WriteData(0x00);
		}
	}
}

/**
  * @brief  OLED显示一个字符
  * @param  Line 行位置，范围：1~4
  * @param  Column 列位置，范围：1~16
  * @param  Char 要显示的一个字符，范围：ASCII可见字符
  * @retval 无
  */
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
	uint8_t i;
	OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8);		//设置光标位置在上半部分
	for (i = 0; i < 8; i++)
	{
		OLED_WriteData(OLED_F8x16[Char - ' '][i]);			//显示上半部分内容
	}
	OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8);	//设置光标位置在下半部分
	for (i = 0; i < 8; i++)
	{
		OLED_WriteData(OLED_F8x16[Char - ' '][i + 8]);		//显示下半部分内容
	}
}

/**
  * @brief  OLED显示字符串
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  String 要显示的字符串，范围：ASCII可见字符
  * @retval 无
  */
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String)
{
	uint8_t i;
	for (i = 0; String[i] != '\0'; i++)
	{
		OLED_ShowChar(Line, Column + i, String[i]);
	}
}

/**
  * @brief  OLED显示一个16x16图标
  * @param  Line 行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~8（图标占 2 个字符位）
  * @param  Icon 图标点阵（32字节，排列同汉字字模）；传 NULL 清除该区域
  * @retval 无
  */
void OLED_ShowIcon(uint8_t Line, uint8_t Column, const uint8_t *Icon)
{
	uint8_t i;
	uint8_t x = (Column - 1) * 8;

	OLED_SetCursor((Line - 1) * 2, x);			//上半部分
	for (i = 0; i < 16; i++)
	{
		OLED_WriteData(Icon != NULL ? Icon[i] : 0x00);
	}
	OLED_SetCursor((Line - 1) * 2 + 1, x);		//下半部分
	for (i = 0; i < 16; i++)
	{
		OLED_WriteData(Icon != NULL ? Icon[i + 16] : 0x00);
	}
}

/**
  * @brief  OLED显示一个16x16汉字（按字库序号）
  * @param  Line 行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~8（汉字占 2 个字符位）
  * @param  Index 汉字在字库 OLED_ChineseFont 中的序号
  * @retval 无
  */
void OLED_ShowChinese(uint8_t Line, uint8_t Column, uint8_t Index)
{
	OLED_ShowIcon(Line, Column, OLED_ChineseFont[Index].data);
}

/**
  * @brief  在字库中查找汉字
  * @param  Utf8 指向 UTF-8 编码汉字的首字节
  * @retval 汉字在字库中的序号；未找到返回 -1
  */
static int OLED_FindChinese(const char *Utf8)
{
	uint8_t i;
	for (i = 0; i < OLED_ChineseFontCount; i++)
	{
		if (memcmp(OLED_ChineseFont[i].name, Utf8, 3) == 0)
		{
			return i;
		}
	}
	return -1;
}

/**
  * @brief  显示一个 UTF-8 字符（1 字节 ASCII 或 3 字节汉字）
  * @param  Line 行位置
  * @param  Column 起始列位置
  * @param  Utf8 指向该字符的首字节
  * @param  Bytes 输出参数：该字符占用的字节数
  * @retval 该字符占用的字符位（1=ASCII，2=汉字）；放不下返回 0
  */
static uint8_t OLED_ShowUtf8Char(uint8_t Line, uint8_t Column, const char *Utf8, uint8_t *Bytes)
{
	/* ASCII 可见字符：1 字节，占 1 个字符位 */
	if ((uint8_t)Utf8[0] < 0x80)
	{
		*Bytes = 1;
		OLED_ShowChar(Line, Column, Utf8[0]);
		return 1;
	}

	/* 汉字：3 字节 UTF-8，占 2 个字符位 */
	*Bytes = 3;
	if (Column + 1 > OLED_LINE_WIDTH)			//本行只剩 1 列，放不下 16 像素宽的汉字
	{
		return 0;
	}

	int index = OLED_FindChinese(Utf8);
	if (index < 0)								//字库中没有该汉字：用空格占位
	{
		OLED_ShowChar(Line, Column, ' ');
		OLED_ShowChar(Line, Column + 1, ' ');
		return 2;
	}

	OLED_ShowChinese(Line, Column, (uint8_t)index);
	return 2;
}

/**
  * @brief  OLED显示汉字/ASCII 混排字符串（UTF-8，不补空格）
  * @param  Line 行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  String 要显示的 UTF-8 字符串
  * @retval 无
  */
void OLED_ShowChineseStr(uint8_t Line, uint8_t Column, const char *String)
{
	uint8_t column = Column;
	uint8_t index = 0;

	while (String[index] != '\0' && column <= OLED_LINE_WIDTH)
	{
		uint8_t bytes = 0;
		uint8_t width = OLED_ShowUtf8Char(Line, column, &String[index], &bytes);
		if (width == 0)
		{
			break;
		}
		column += width;
		index += bytes;
	}
}

/**
  * @brief  OLED显示一行定长字符串（不足部分用空格补齐，超长截断）
  * @param  Line 行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  String 要显示的 UTF-8 字符串（最长显示到本行末尾）
  * @retval 无
  */
void OLED_ShowStringPad(uint8_t Line, uint8_t Column, const char *String)
{
	/* 1 起始列换算成 0 起始 */
	uint8_t column = (Column > 0) ? (Column - 1) : 0;
	uint8_t index = 0;

	/* 逐个字符显示（支持 ASCII 与汉字混排） */
	while (String[index] != '\0' && column < OLED_LINE_WIDTH)
	{
		uint8_t bytes = 0;
		uint8_t width = OLED_ShowUtf8Char(Line, column + 1, &String[index], &bytes);
		if (width == 0)
		{
			break;
		}
		column += width;
		index += bytes;
	}

	/* 本行剩余部分用空格补齐，避免上次显示的字符残留 */
	while (column < OLED_LINE_WIDTH)
	{
		OLED_ShowChar(Line, column + 1, ' ');
		column++;
	}
}

/**
  * @brief  OLED次方函数
  * @retval 返回值等于X的Y次方
  */
uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
	uint32_t Result = 1;
	while (Y--)
	{
		Result *= X;
	}
	return Result;
}

/**
  * @brief  OLED显示数字（十进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~4294967295
  * @param  Length 要显示数字的长度，范围：1~10
  * @retval 无
  */
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i++)
	{
		OLED_ShowChar(Line, Column + i, Number / OLED_Pow(10, Length - i - 1) % 10 + '0');
	}
}

/**
  * @brief  OLED显示数字（十进制，带符号数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：-2147483648~2147483647
  * @param  Length 要显示数字的长度，范围：1~10
  * @retval 无
  */
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length)
{
	uint8_t i;
	uint32_t Number1;
	if (Number >= 0)
	{
		OLED_ShowChar(Line, Column, '+');
		Number1 = Number;
	}
	else
	{
		OLED_ShowChar(Line, Column, '-');
		Number1 = -Number;
	}
	for (i = 0; i < Length; i++)
	{
		OLED_ShowChar(Line, Column + i + 1, Number1 / OLED_Pow(10, Length - i - 1) % 10 + '0');
	}
}

/**
  * @brief  OLED显示数字（十六进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~0xFFFFFFFF
  * @param  Length 要显示数字的长度，范围：1~8
  * @retval 无
  */
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i, SingleNumber;
	for (i = 0; i < Length; i++)
	{
		SingleNumber = Number / OLED_Pow(16, Length - i - 1) % 16;
		if (SingleNumber < 10)
		{
			OLED_ShowChar(Line, Column + i, SingleNumber + '0');
		}
		else
		{
			OLED_ShowChar(Line, Column + i, SingleNumber - 10 + 'A');
		}
	}
}

/**
  * @brief  OLED显示数字（二进制，正数）
  * @param  Line 起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~1111 1111 1111 1111
  * @param  Length 要显示数字的长度，范围：1~16
  * @retval 无
  */
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i++)
	{
		OLED_ShowChar(Line, Column + i, Number / OLED_Pow(2, Length - i - 1) % 2 + '0');
	}
}

/**
  * @brief  OLED初始化
  * @param  无
  * @retval 无
  */
void OLED_Init(void)
{
	vTaskDelay(pdMS_TO_TICKS(100));	//上电延时

	OLED_I2C_Init();			//端口初始化

	OLED_WriteCommand(0xAE);	//关闭显示

	OLED_WriteCommand(0xD5);	//设置显示时钟分频比/振荡器频率
	OLED_WriteCommand(0x80);

	OLED_WriteCommand(0xA8);	//设置多路复用率
	OLED_WriteCommand(0x3F);

	OLED_WriteCommand(0xD3);	//设置显示偏移
	OLED_WriteCommand(0x00);

	OLED_WriteCommand(0x40);	//设置显示开始行

	OLED_WriteCommand(0xA1);	//设置左右方向，0xA1正常 0xA0左右反置

	OLED_WriteCommand(0xC8);	//设置上下方向，0xC8正常 0xC0上下反置

	OLED_WriteCommand(0xDA);	//设置COM引脚硬件配置
	OLED_WriteCommand(0x12);

	OLED_WriteCommand(0x81);	//设置对比度控制
	OLED_WriteCommand(0xCF);

	OLED_WriteCommand(0xD9);	//设置预充电周期
	OLED_WriteCommand(0xF1);

	OLED_WriteCommand(0xDB);	//设置VCOMH取消选择级别
	OLED_WriteCommand(0x30);

	OLED_WriteCommand(0xA4);	//设置整个显示打开/关闭

	OLED_WriteCommand(0xA6);	//设置正常/倒转显示

	OLED_WriteCommand(0x8D);	//设置充电泵
	OLED_WriteCommand(0x14);

	OLED_WriteCommand(0xAF);	//开启显示

	OLED_Clear();				//OLED清屏
}
