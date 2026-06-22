#include <assert.h>
#include <string.h>

#include "main.h"
#include "tm1637.h"

#if defined(TM1637_USE_FREERTOS) && TM1637_USE_FREERTOS > 0
#include "cmsis_os.h"
#include "semphr.h"
#endif

#ifndef TM1637_STACK
#define TM1637_STACK 64
#endif

#ifndef TM1637_PRIORITY
#define TM1637_PRIORITY osPriorityLow
#endif

#ifndef TM1637_DELAY_US
#define TM1637_DELAY_US 5
#endif

#ifndef TM1637_DISPLAYS_MAX
#define TM1637_DISPLAYS_MAX 2
#endif

#ifndef TM1637_UPDATE_INTERVAL_MS
#define TM1637_UPDATE_INTERVAL_MS 1000
#endif

#ifndef TM1637_DIGITS_MAX
#define TM1637_DIGITS_MAX 8
#endif

#define TM1637_CHARS_NUMS        0
#define TM1637_CHARS_LETTERS     10
#define TM1637_CHAR_DASH         10+26
#define TM1637_CHAR_UNDERSCORE   10+26+1
#define TM1637_CHAR_EQUAL        10+26+2
#define TM1637_DOT               0x80

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

static const uint8_t tm1637_chars[] = {
  [0] = 0x3f, [1] = 0x06, [2] = 0x5b, [3] = 0x4f, [4] = 0x66,
  [5] = 0x6d, [6] = 0x7d, [7] = 0x07, [8] = 0x7f, [9] = 0x6f,
  [10+0   /*A*/] = 0x77,
  [10+1   /*B*/] = 0x7c,
  [10+2   /*C*/] = 0x39,
  [10+3   /*D*/] = 0x5e,
  [10+4   /*E*/] = 0x79,
  [10+5   /*F*/] = 0x71,
  [10+6   /*G*/] = 0x3d,
  [10+7   /*H*/] = 0x76,
  [10+8   /*I*/] = 0x38,
  [10+9   /*J*/] = 0x1e,
  [10+10  /*K*/] = 0x46,
  [10+11  /*L*/] = 0x38,
  [10+12  /*M*/] = 0x37,
  [10+13  /*N*/] = 0x54,
  [10+14  /*O*/] = 0x3f,
  [10+15  /*P*/] = 0x73,
  [10+16  /*Q*/] = 0x67,
  [10+17  /*R*/] = 0x50,
  [10+18  /*S*/] = 0x6d,
  [10+19  /*T*/] = 0x78,
  [10+20  /*U*/] = 0x1c,
  [10+21  /*V*/] = 0x3e,
  [10+22  /*W*/] = 0x7e,
  [10+23  /*X*/] = 0x36,
  [10+24  /*Y*/] = 0x6e,
  [10+25  /*Z*/] = 0x6e,
  [10+26+0/*-*/] = 0x40,
  [10+26+1/*_*/] = 0x08,
  [10+26+2/*=*/] = 0x48,
};

// Taken from https://github.com/RobTillaart/TM1637_RT
static uint8_t tm1637_digits_order[TM1637_DIGITS_MAX]  = {0};
#if TM1637_DIGITS_MAX >= 6
static const uint8_t tm1637_6digits_order[] = {2, 1, 0, 5, 4, 3};
#endif

static void delay_us(uint32_t us) {
  uint32_t start = SysTick->VAL;
  uint32_t coreclock = SystemCoreClock/1000000U;
  uint32_t trg = (us * coreclock);
  uint32_t reload = SysTick->LOAD;
  uint32_t val = SysTick->VAL;
  while (((start >= val) ? start - val : start + (reload - val)) < trg) {
    val = SysTick->VAL;
  }
}

// --- low-level helpers ---
static void set_clk(TM1637_HandleTypeDef **h, uint32_t c, GPIO_PinState s) {
  for (unsigned i = 0; i < c; ++i)
    HAL_GPIO_WritePin(h[i]->clk_port, h[i]->clk_pin, s);
  delay_us(TM1637_DELAY_US);
}
static void set_dio(TM1637_HandleTypeDef **h, uint32_t c, GPIO_PinState s) {
  for (unsigned i = 0; i < c; ++i)
    HAL_GPIO_WritePin(h[i]->dio_port, h[i]->dio_pin, s);
  delay_us(TM1637_DELAY_US);
}

static void set_dios(TM1637_HandleTypeDef **h, uint32_t c, uint8_t s[]) {
  for (unsigned i = 0; i < c; ++i)
    HAL_GPIO_WritePin(h[i]->dio_port, h[i]->dio_pin, (s[i] & 0x1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  delay_us(TM1637_DELAY_US);
}

static void dio_out(TM1637_HandleTypeDef **h, uint32_t c) {
  GPIO_InitTypeDef gpio = {0};
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  for (unsigned i = 0; i < c; ++i) {
    gpio.Pin = h[i]->dio_pin;
    HAL_GPIO_Init(h[i]->dio_port, &gpio);
  }
}
static void dio_in(TM1637_HandleTypeDef **h, uint32_t c) {
  GPIO_InitTypeDef gpio = {0};
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  for (unsigned i = 0; i < c; ++i) {
    gpio.Pin = h[i]->dio_pin;
    HAL_GPIO_Init(h[i]->dio_port, &gpio);
  }
}

static void start_cond(TM1637_HandleTypeDef **h, uint32_t c) {
  set_clk(h, c, GPIO_PIN_SET);
  set_dio(h, c, GPIO_PIN_SET);
  set_dio(h, c, GPIO_PIN_RESET);
  set_clk(h, c, GPIO_PIN_RESET);
}
static void stop_cond(TM1637_HandleTypeDef **h, uint32_t c) {
  set_clk(h, c, GPIO_PIN_RESET);
  set_dio(h, c, GPIO_PIN_RESET);
  set_clk(h, c, GPIO_PIN_SET);
  set_dio(h, c, GPIO_PIN_SET);
}

static bool get_ack(TM1637_HandleTypeDef **h, uint32_t c) {
  set_clk(h, c, GPIO_PIN_RESET);
  dio_in(h, c);
  set_clk(h, c, GPIO_PIN_SET);
  bool ack = true;
  for (unsigned i = 0; i < c; ++i)
    ack = ack && (HAL_GPIO_ReadPin(h[i]->dio_port, h[i]->dio_pin) == GPIO_PIN_RESET);
  set_clk(h, c, GPIO_PIN_RESET);
  dio_out(h, c);
  return ack;
}

static bool write_byte(TM1637_HandleTypeDef **h, uint32_t c, uint8_t b) {
  for (int i=0; i<8; i++){
    set_clk(h, c, GPIO_PIN_RESET);
    set_dio(h, c, (b & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    b >>= 1;
    set_clk(h, c, GPIO_PIN_SET);
  }
  return get_ack(h, c);
}

static bool write_bytes(TM1637_HandleTypeDef **h, uint32_t c, uint8_t *b) {
  for (unsigned i = 0; i < 8; ++i) {
    set_clk(h, c, GPIO_PIN_RESET);
    set_dios(h, c, b);
    for (unsigned j = 0; j < c; ++j)
      b[j] >>= 1;
    set_clk(h, c, GPIO_PIN_SET);
  }
  return get_ack(h, c);
}

static void copy_raw_ordered(uint8_t *buf, TM1637_HandleTypeDef *htm, uint32_t displays_count) {
  for (unsigned i = 0; i < htm->digits; ++i) {
    buf[i*displays_count] = htm->raw[htm->digits_order[i]];
  }
}

// --- API ---
void TM1637_Brightness(TM1637_HandleTypeDef *htm, uint8_t brightness) {
  brightness = MIN(brightness, 7);
#if defined(TM1637_USE_FREERTOS) && TM1637_USE_FREERTOS > 0
  if (htm->mutex) {
    if (xSemaphoreTake(htm->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      htm->brightness = brightness;
      xSemaphoreGive(htm->mutex);
    }
  } else
#endif // TM1637_USE_FREERTOS
    htm->brightness = brightness;
}

void TM1637_ShowRaw(TM1637_HandleTypeDef *htm, const uint8_t raw[]) {
#if defined(TM1637_USE_FREERTOS) && TM1637_USE_FREERTOS > 0
  if (htm->mutex) {
    if (xSemaphoreTake(htm->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      memcpy(htm->raw, raw, htm->digits);
      xSemaphoreGive(htm->mutex);
    }
  } else
#endif // TM1637_USE_FREERTOS
    memcpy(htm->raw, raw, htm->digits);
}

void TM1637_ShowRawRtL(TM1637_HandleTypeDef *htm, const uint8_t raw[]) {
#if defined(TM1637_USE_FREERTOS) && TM1637_USE_FREERTOS > 0
  if (htm->mutex) {
    if (xSemaphoreTake(htm->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      for (unsigned i = 0; i < htm->digits; ++i) {
        htm->raw[htm->digits - i - 1] = raw[i];
      }
      xSemaphoreGive(htm->mutex);
    }
    return;
  }
#endif // TM1637_USE_FREERTOS
  for (unsigned i = 0; i < htm->digits; ++i) {
    htm->raw[htm->digits - i - 1] = raw[i];
  }
}

void TM1637_Clear(TM1637_HandleTypeDef *htm) {
  uint8_t z[TM1637_DIGITS_MAX] = {0};
  TM1637_ShowRaw(htm, z);
}

void TM1637_UpdateOnce(TM1637_HandleTypeDef *htm) {
  TM1637_UpdateOnceAll(&htm, 1);
}

void TM1637_UpdateOnceAll(TM1637_HandleTypeDef *htms[], uint32_t htms_count) {
  if (htms_count <= 0 || htms_count > MAX(TM1637_DISPLAYS_MAX, 1))
    return;
  uint8_t buf[TM1637_DIGITS_MAX * MAX(TM1637_DISPLAYS_MAX, 1)] = {0};
  uint8_t brightness_buf[MAX(TM1637_DISPLAYS_MAX, 1)] = {0};
  unsigned digits = 0;
  for (unsigned i = 0; i < htms_count; ++i) {
#if defined(TM1637_USE_FREERTOS) && TM1637_USE_FREERTOS > 0
    if (htms[i]->mutex) {
      if (xSemaphoreTake(htms[i]->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        digits = MAX(htms[i]->digits, digits);
        copy_raw_ordered(buf + i, htms[i], htms_count);
        brightness_buf[i] = 0x88 | (htms[i]->brightness & 0x07);
        xSemaphoreGive(htms[i]->mutex);
      } else {
        return; // Mutex aquiring failed
      }
    } else
  #endif // TM1637_USE_FREERTOS
      copy_raw_ordered(buf + i, htms[i], htms_count),
      brightness_buf[i] = 0x88 | (htms[i]->brightness & 0x07),
      digits = MAX(htms[i]->digits, digits);
  }
  // Auto-increment mode
  start_cond(htms, htms_count);
  write_byte(htms, htms_count, 0x40);
  stop_cond(htms, htms_count);

  start_cond(htms, htms_count);
  write_byte(htms, htms_count, 0xC0); // Address 0
  for (unsigned i = 0; i < digits; ++i){
    write_bytes(htms, htms_count, buf + i*htms_count);
  }
  stop_cond(htms, htms_count);

  // Brightness cmd
  start_cond(htms, htms_count);
  write_bytes(htms, htms_count, brightness_buf);
  stop_cond(htms, htms_count);
}

#if TM1637_DISPLAYS_MAX > 0
TM1637_HandleTypeDef *htms[TM1637_DISPLAYS_MAX];
uint32_t htms_count = 0;
#endif

void TM1637_Init(TM1637_HandleTypeDef *htm) {
  assert(htm->digits < TM1637_DIGITS_MAX);
  if (htm->digits == 0)
    htm->digits = 4; // Reasonable default, clock displays show up relatively often
#if TM1637_DISPLAYS_MAX > 0
  assert(htms_count < TM1637_DISPLAYS_MAX);
  htms[htms_count++] = htm;
#endif
  memset(htm->raw, 0, sizeof(htm->raw));
  if (!htm->digits_order) {
#if TM1637_DIGITS_MAX >= 6
    if (htm->digits == 6) {
      htm->digits_order = tm1637_6digits_order;
    } else
#endif // TM1637_DIGITS_MAX
    {
      htm->digits_order = tm1637_digits_order;
      if (tm1637_digits_order[TM1637_DIGITS_MAX - 1] == 0) {
        for (unsigned i = 1; i < TM1637_DIGITS_MAX; ++i) {
          tm1637_digits_order[i] = i;
        }
      }
    }
  }
  // Pin reconfiguration
  GPIO_InitTypeDef gpio = {0};
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Pin = htm->dio_pin;
  HAL_GPIO_Init(htm->dio_port, &gpio);
  gpio.Pin = htm->clk_pin;
  HAL_GPIO_Init(htm->clk_port, &gpio);
#if defined(TM1637_USE_FREERTOS) && TM1637_USE_FREERTOS > 0
  if (htm->mutex == NULL) {
    htm->mutex = xSemaphoreCreateMutex();
    xSemaphoreGive(htm->mutex);
  }
#endif
}

#if TM1637_DISPLAYS_MAX > 0
void TM1637_Tick(void) {
  static uint32_t lastcalled = 0;
  uint32_t time = HAL_GetTick();
  if (time - lastcalled < TM1637_UPDATE_INTERVAL_MS)
    return;
  lastcalled = time;
  TM1637_UpdateOnceAll(htms, htms_count);
}

#if defined(TM1637_USE_FREERTOS) && TM1637_USE_FREERTOS > 0
static void TM1637_Task(void *empty) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  for (;;){
    TM1637_UpdateOnceAll(htms, htms_count);
    uint32_t waitfor = pdMS_TO_TICKS(TM1637_UPDATE_INTERVAL_MS);
    vTaskDelayUntil(&xLastWakeTime, waitfor);
  }
}

void TM1637_StartTask(const char *task_name) {
  xTaskCreate(TM1637_Task, task_name, TM1637_STACK, NULL, TM1637_PRIORITY, NULL);
}
#endif // TM1637_USE_FREERTOS
#endif // TM1637_DISPLAYS_MAX

void TM1637_ShowDecimal(TM1637_HandleTypeDef *htm, int value, bool leading_zero){
  unsigned i = 0;
  uint8_t buf[TM1637_DIGITS_MAX];
  unsigned negative = value < 0;
  if (value == 0) {
    buf[i++] = tm1637_chars[value];
  }
  while (i < htm->digits - negative) {
    buf[i++] = (value == 0 && !leading_zero) ? 0 : tm1637_chars[value % 10];
    value /= 10;
  }
  if (negative) {
    buf[i] = TM1637_CHAR_DASH;
  }
  TM1637_ShowRawRtL(htm, buf);
}

void TM1637_ShowHex(TM1637_HandleTypeDef *htm, unsigned value, bool leading_zero) {
  unsigned i = 0;
  uint8_t buf[TM1637_DIGITS_MAX];
  if (value == 0) {
    buf[i++] = tm1637_chars[value];
  }
  while (i < htm->digits) {
    buf[i++] = (value == 0 && !leading_zero) ? 0 : tm1637_chars[value % 16];
    value /= 16;
  }
  TM1637_ShowRawRtL(htm, buf);
}

void TM1637_ShowTime(TM1637_HandleTypeDef *htm, unsigned time) {
  uint8_t buf[TM1637_DIGITS_MAX];
  buf[0] = tm1637_chars[time % 10];
  time /= 10;
  for (int i = 1; i < htm->digits - 1; i += 2) {
    buf[i] = tm1637_chars[time % 6];
    time /= 6;
    buf[i+1] = tm1637_chars[time % 10] | TM1637_DOT;
    time /= 10;
  }
  if (htm->digits % 2 == 0)
    buf[htm->digits - 1] = tm1637_chars[time % 10];
  TM1637_ShowRawRtL(htm, buf);
}

void TM1637_ShowBCD(TM1637_HandleTypeDef *htm, uint32_t time) {
  uint8_t buf[TM1637_DIGITS_MAX];
  buf[0] = tm1637_chars[time & 0xF];
  time >>= 4;
  for (int i = 1; i < htm->digits - 1; i += 2) {
    buf[i] = tm1637_chars[time & 0xF];
    time >>= 4;
    buf[i+1] = tm1637_chars[time & 0xF] | TM1637_DOT;
    time >>= 4;
  }
  if (htm->digits % 2 == 0)
    buf[htm->digits - 1] = tm1637_chars[time & 0xF];
  TM1637_ShowRawRtL(htm, buf);
}

void TM1637_ShowBCD_HM(TM1637_HandleTypeDef *htm, uint32_t time) {
  TM1637_ShowBCD(htm, time >> (6 - MIN(htm->digits, 6)) * 4);
}

uint8_t TM1637_ASCIIToRaw(char c) {
  if (c >= '0' && c <= '9') {
    c += TM1637_CHARS_NUMS - '0';
  } else if (c >= 'A' && c <= 'Z') {
    c += TM1637_CHARS_LETTERS - 'A';
  } else if (c >= 'a' && c <= 'z') {
    c += TM1637_CHARS_LETTERS - 'a';
  } else if (c == '-') {
    c = TM1637_CHAR_DASH;
  } else if (c == '_') {
    c = TM1637_CHAR_UNDERSCORE;
  } else if (c == '=') {
    c = TM1637_CHAR_EQUAL;
  } else if (c == '.' || c == ',') {
    c = TM1637_DOT;
  } else {
    return 0;
  }
  return tm1637_chars[(unsigned)c];
}

void TM1637_ShowText(TM1637_HandleTypeDef *htm, const char *const str) {
  uint8_t buf[TM1637_DIGITS_MAX] = {0};
  for (unsigned i = 0, j = 0; i < htm->digits; ++i, ++j) {
    if (str[j] == '\0')
      break;
    if ((str[j] == '.' || str[j] == ',') && i > 0 && buf[i-1] < TM1637_DOT) {
      buf[--i] |= TM1637_DOT;
      continue;
    }
    buf[i] = TM1637_ASCIIToRaw(str[j]);
  }
  TM1637_ShowRaw(htm, buf);
}

#if defined(TM1637_PRINTF) && TM1637_PRINTF != 0
#include <stdio.h>
#include <stdarg.h>

bool TM1637_vprintf(TM1637_HandleTypeDef *htm, const char *const fmt, va_list l) {
  char buf[TM1637_DIGITS_MAX * 2]; // Counting dots
  if (vsnprintf(buf, htm->digits * 2, fmt, l) < 0)
    return false;
  TM1637_ShowText(htm, buf);
  return true;
}

bool TM1637_printf(TM1637_HandleTypeDef *htm, const char *const fmt, ...) {
  va_list l;
  va_start(l, fmt);
  bool b = TM1637_vprintf(htm, fmt, l);
  va_end(l);
  return b;
}
#endif // TM1637_PRINTF