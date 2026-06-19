#ifndef TM1637_H
#define TM1637_H

#include <stdint.h>
#include <stdbool.h>

#include "tm1637_config.h"

#ifndef TM1637_DISPLAYS_MAX
#define TM1637_DISPLAYS_MAX 2
#endif

#ifndef TM1637_DIGITS_MAX
#define TM1637_DIGITS_MAX 8
#endif

typedef struct {
  const uint8_t *digits_order;
  GPIO_TypeDef *clk_port;
  GPIO_TypeDef *dio_port;
  uint16_t clk_pin;
  uint16_t dio_pin;
  uint8_t brightness;
  uint8_t digits;
  // Right to left digits array
  uint8_t raw[TM1637_DIGITS_MAX];
#if defined(TM1637_USE_FREERTOS) && TM1637_USE_FREERTOS > 0
  void *mutex; // Take it when writing to this struct
#endif
} TM1637_HandleTypeDef;

void TM1637_Init(TM1637_HandleTypeDef *htm);
void TM1637_Brightness (TM1637_HandleTypeDef *htm, uint8_t brightness);
void TM1637_Clear (TM1637_HandleTypeDef *htm);
void TM1637_ShowRaw (TM1637_HandleTypeDef *htm, const uint8_t raw[4]); // защищённо обновляет буфер
void TM1637_ShowDecimal(TM1637_HandleTypeDef *htm, int value, bool leading_zero);
void TM1637_ShowHex(TM1637_HandleTypeDef *htm, unsigned value, bool leading_zero);
void TM1637_ShowTime(TM1637_HandleTypeDef *htm, unsigned time);
void TM1637_ShowBCD_MS(TM1637_HandleTypeDef *htm, uint32_t time);
void TM1637_ShowBCD_HM(TM1637_HandleTypeDef *htm, uint32_t time);
void TM1637_ShowText(TM1637_HandleTypeDef *htm, const char *const str);
void TM1637_UpdateOnce(TM1637_HandleTypeDef *htm);
#if TM1637_DISPLAYS_MAX > 0
void TM1637_Tick(void);
#if defined(TM1637_USE_FREERTOS) && TM1637_USE_FREERTOS > 0
void TM1637_StartTask(const char *task_name);
#endif // TM1637_USE_FREERTOS
#endif // TM1637_DISPLAYS_MAX
#endif // TM1637_H
