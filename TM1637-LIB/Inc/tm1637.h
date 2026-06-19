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
  const uint8_t *digits_order; /* Array of indices in which digits should be passed to be displayed left to right. Provide if digit order is wrong */
  GPIO_TypeDef *clk_port; /* GPIO port used to provide clk to display */
  GPIO_TypeDef *dio_port; /* GPIO port used to provide dio to display */
  uint16_t clk_pin; /* GPIO pin number used to provide clk to display */
  uint16_t dio_pin; /* GPIO pin number used to provide dio to display */
  uint8_t brightness; /* Display brightness, 0-7 */
  uint8_t digits; /* Display digits, defaults to 4 */
  // Right to left digits array
  uint8_t raw[TM1637_DIGITS_MAX]; /* Raw data displayed */
#if defined(TM1637_USE_FREERTOS) && TM1637_USE_FREERTOS > 0
  void *mutex; /* Mutex to protect struct from race conditions */
#endif
} TM1637_HandleTypeDef;
/**
 * @brief Initializes tm1637 handle - puts it in global array for tick()
 * and creates mutex (if TM1637_USE_FREERTOS is non-zero).
 * @param htm Pointer to the TM1637_HandleTypeDef structure
 */
void TM1637_Init(TM1637_HandleTypeDef *htm);
/**
 * @brief Sets brightness of tm1637 display. Thread-safe.
 * @param htm        Pointer to the TM1637_HandleTypeDef structure
 * @param brightness Level of brightness (0-7)
 */
void TM1637_Brightness (TM1637_HandleTypeDef *htm, uint8_t brightness);
/**
 * @brief Clears the display.
 * @param htm Pointer to the TM1637_HandleTypeDef structure
 */
void TM1637_Clear (TM1637_HandleTypeDef *htm);
/**
 * @brief Displays raw bytes, right-to-left.
 * @param htm Pointer to the TM1637_HandleTypeDef structure
 * @param raw Data array to display
 */
void TM1637_ShowRaw (TM1637_HandleTypeDef *htm, const uint8_t raw[]);
/**
 * @brief Displays raw bytes, right-to-left (reverses the buffer).
 * @param htm Pointer to the TM1637_HandleTypeDef structure
 * @param raw Data array to display.
 */
void TM1637_ShowRawRtL(TM1637_HandleTypeDef *htm, const uint8_t raw[]);
/**
 * @brief Displays decimal integer number
 * @param htm          Pointer to the TM1637_HandleTypeDef structure
 * @param value        Number to display
 * @param leading_zero Prepend number with empty spaces (leading_zero=false) or with zeroes (leading_zero=true)
 */
void TM1637_ShowDecimal(TM1637_HandleTypeDef *htm, int value, bool leading_zero);
/**
 * @brief Displays hexadecimal integer number
 * @param htm          Pointer to the TM1637_HandleTypeDef structure
 * @param value        Number to display
 * @param leading_zero Prepend number with empty spaces (leading_zero=false) or with zeroes (leading_zero=true)
 */
void TM1637_ShowHex(TM1637_HandleTypeDef *htm, unsigned value, bool leading_zero);
/**
 * @brief Displays integer number as time (mm:ss by default)
 * @param htm  Pointer to the TM1637_HandleTypeDef structure
 * @param time Number to display
 */
void TM1637_ShowTime(TM1637_HandleTypeDef *htm, unsigned time);
/**
 * @brief Displays integer number as binary-coded decimal time, as returned by RTC->TR.
 * If passing time as is, will display mm:ss on 4-digit display
 * @param htm  Pointer to the TM1637_HandleTypeDef structure
 * @param time Time to display
 */
void TM1637_ShowBCD(TM1637_HandleTypeDef *htm, uint32_t time);
/**
 * @brief Displays integer number as binary-coded decimal time, as returned by RTC->TR.
 * If passing time as is, will display hh:mm on 4-digit display.
 * If using 6-digit display, equivalent to TM1637_ShowBCD.
 * @param htm  Pointer to the TM1637_HandleTypeDef structure
 * @param time Time to display
 */
void TM1637_ShowBCD_HM(TM1637_HandleTypeDef *htm, uint32_t time);
/**
 * @brief Convert ASCII character to tm1637 raw data to display.
 * @param c Character to convert
 * @return bit value specifying segments to show on TM1637 display
 */
uint8_t TM1637_ASCIIToRaw(char c);
/**
 * @brief Displays string.
 * @param htm Pointer to the TM1637_HandleTypeDef structure
 * @param str String to display
 */
void TM1637_ShowText(TM1637_HandleTypeDef *htm, const char *const str);
/**
 * @brief Manually refreshes the display. Usually called from TM1637_Tick() or by TM1637_Task(),
 * so does not needed to be called manually.
 * @param htm Pointer to the TM1637_HandleTypeDef structure
 */
void TM1637_UpdateOnce(TM1637_HandleTypeDef *htm);
#if TM1637_DISPLAYS_MAX > 0
/**
 * @brief Refreshes all connected displays one by one, waiting TM1637_UPDATE_INTERVAL_MS/htms_count.
 */
void TM1637_Tick(void);
#if defined(TM1637_USE_FREERTOS) && TM1637_USE_FREERTOS > 0
/**
 * @brief Starts FreeRTOS task to automatically refresh all connected displays.
 * @param task_name Name of created task, can be NULL
 */
void TM1637_StartTask(const char *task_name);
#endif // TM1637_USE_FREERTOS
#endif // TM1637_DISPLAYS_MAX
#endif // TM1637_H
