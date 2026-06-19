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
static const uint8_t tm1637_4digits_order[] = {3, 2, 1, 0};
static const uint8_t tm1637_6digits_order[] = {3, 4, 5, 0, 1, 2};

static void delay_us(TM1637_HandleTypeDef *h, uint32_t us) {
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
static void set_clk(TM1637_HandleTypeDef *h, GPIO_PinState s) {
  HAL_GPIO_WritePin(h->clk_port, h->clk_pin, s);
  delay_us(h, TM1637_DELAY_US);
}
static void set_dio(TM1637_HandleTypeDef *h, GPIO_PinState s) {
  HAL_GPIO_WritePin(h->dio_port, h->dio_pin, s);
  delay_us(h, TM1637_DELAY_US);
}

static void dio_out(TM1637_HandleTypeDef *h) {
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = h->dio_pin;
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(h->dio_port, &gpio);
}
static void dio_in(TM1637_HandleTypeDef *h) {
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = h->dio_pin;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(h->dio_port, &gpio);
}

static void start_cond(TM1637_HandleTypeDef *h) {
  dio_out(h);
  set_clk(h, GPIO_PIN_SET);
  set_dio(h, GPIO_PIN_SET);
  set_dio(h, GPIO_PIN_RESET);
  set_clk(h, GPIO_PIN_RESET);
}
static void stop_cond(TM1637_HandleTypeDef *h) {
  dio_out(h);
  set_clk(h, GPIO_PIN_RESET);
  set_dio(h, GPIO_PIN_RESET);
  set_clk(h, GPIO_PIN_SET);
  set_dio(h, GPIO_PIN_SET);
}

static bool write_byte(TM1637_HandleTypeDef *h, uint8_t b) {
  dio_out(h);
  for (int i=0;i<8;i++){
    set_clk(h, GPIO_PIN_RESET);
    if (b & 0x01) set_dio(h, GPIO_PIN_SET); else set_dio(h, GPIO_PIN_RESET);
    b >>= 1;
    set_clk(h, GPIO_PIN_SET);
  }
  // ACK
  set_clk(h, GPIO_PIN_RESET);
  dio_in(h);
  set_clk(h, GPIO_PIN_SET);
  bool ack = (HAL_GPIO_ReadPin(h->dio_port, h->dio_pin) == GPIO_PIN_RESET);
  set_clk(h, GPIO_PIN_RESET);
  dio_out(h);
  return ack;
}

static void copy_raw_ordered(uint8_t *buf, TM1637_HandleTypeDef *htm) {
  for (unsigned i = 0; i < htm->digits; ++i) {
    buf[i] = htm->raw[htm->digits_order[i]];
  }
}

// --- API ---
void TM1637_Brightness(TM1637_HandleTypeDef *htm, uint8_t brightness) {
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

void TM1637_ShowRaw(TM1637_HandleTypeDef *htm, const uint8_t raw[4]) {
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

void TM1637_Clear(TM1637_HandleTypeDef *htm) {
  uint8_t z[TM1637_DIGITS_MAX] = {0};
  TM1637_ShowRaw(htm, z);
}

void TM1637_UpdateOnce(TM1637_HandleTypeDef *htm) {
  uint8_t buf[TM1637_DIGITS_MAX];
  unsigned digits = TM1637_DIGITS_MAX;
#if defined(TM1637_USE_FREERTOS) && TM1637_USE_FREERTOS > 0
  if (htm->mutex) {
    if (xSemaphoreTake(htm->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      digits = htm->digits;
      copy_raw_ordered(buf, htm);
      xSemaphoreGive(htm->mutex);
    } else {
      return; // Mutex aquiring failed
    }
  } else
#endif // TM1637_USE_FREERTOS
    copy_raw_ordered(buf, htm), digits = htm->digits;
  // Auto-increment mode
  start_cond(htm);
  write_byte(htm, 0x40);
  stop_cond(htm);

  start_cond(htm);
  write_byte(htm, 0xC0); // Address 0
  for (unsigned i = 0; i < digits; ++i){
    write_byte(htm, buf[i]);
  }
  stop_cond(htm);

  // Brightness cmd
  start_cond(htm);
  write_byte(htm, 0x88 | (htm->brightness & 0x07));
  stop_cond(htm);
}

#if TM1637_DISPLAYS_MAX > 0
TM1637_HandleTypeDef *htms[TM1637_DISPLAYS_MAX];
uint32_t htms_size = 0;
#endif

void TM1637_Init(TM1637_HandleTypeDef *htm) {
  assert(htm->digits < TM1637_DIGITS_MAX);
  if (htm->digits == 0)
    htm->digits = 4; // Reasonable default, clock displays show up relatively often
  if (!htm->digits_order) {
    if (htm->digits == 4) {
      htm->digits_order = tm1637_4digits_order;
    } else if (htm->digits == 6) {
      htm->digits_order = tm1637_6digits_order;
    } else {
      assert(!"Please manually specify digits order for your display!");
      return;
    }
  }
#if TM1637_DISPLAYS_MAX > 0
  assert(htms_size < TM1637_DISPLAYS_MAX);
  htms[htms_size++] = htm;
#endif
  memset(htm->raw, 0, sizeof(htm->raw));
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
  static uint32_t i = 0;
  uint32_t time = HAL_GetTick();
  if (time - lastcalled < TM1637_UPDATE_INTERVAL_MS / htms_size)
    return;
  lastcalled = time;
  TM1637_UpdateOnce(htms[i++]);
  if (i >= htms_size)
    i = 0;
}

#if defined(TM1637_USE_FREERTOS) && TM1637_USE_FREERTOS > 0
static void TM1637_Task(void *empty) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  uint32_t i = 0;
  for (;;){
    TM1637_UpdateOnce(htms[i++]);
    if (i >= htms_size)
      i = 0;
    uint32_t waitfor = pdMS_TO_TICKS(TM1637_UPDATE_INTERVAL_MS / htms_size);
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
  TM1637_ShowRaw(htm, buf);
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
  TM1637_ShowRaw(htm, buf);
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
  TM1637_ShowRaw(htm, buf);
}

void TM1637_ShowBCD_MS(TM1637_HandleTypeDef *htm, uint32_t time) {
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
  TM1637_ShowRaw(htm, buf);
}

void TM1637_ShowBCD_HM(TM1637_HandleTypeDef *htm, uint32_t time) {
  TM1637_ShowBCD_MS(htm, time >> (6 - MIN(htm->digits, 6)) * 4);
}

void TM1637_ShowText(TM1637_HandleTypeDef *htm, const char *const str) {
  uint8_t buf[TM1637_DIGITS_MAX] = {0};
  for (unsigned i = 0; i < htm->digits; ++i) {
    unsigned char c = str[i];
    if (c == '\0') {
      break;
    } else if (c >= '0' && c <= '9') {
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
    } else {
      buf[htm->digits - i - 1] = 0;
      continue;
    }
    buf[htm->digits - i - 1] = tm1637_chars[c];
  }
  TM1637_ShowRaw(htm, buf);
}