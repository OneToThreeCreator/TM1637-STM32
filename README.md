# TM1637-STM32
Library for TM1637-based displays for STM32 HAL.
## Description
The TM1637 drives 7-segment displays and can also scan 2x8 matrix keyboard.
This library only supports TM1637 used for display purposes.
Should work on any STM32, only tested on F401 (blackpill) for now.
## Inclusion in your project
Put TM1637-LIB inside your project tree and either:
```cmake
#Put it AFTER add_subdirectory(cmake/stm32cubemx)!!!
add_subdirectory(FULL_PATH_TO_TM1637-LIB TM1637_LIB)
...
target_link_libraries(${CMAKE_PROJECT_NAME} tm1637)
```
or manually add all the sources to your project (and include Inc dir).
Also copy file TM1637-LIB/Config/template_tm1637_config.h to your include dir
and rename it to "tm1637_config.h". Tweak it to your needs.
Can be used with FreeRTOS or without, depending on application.
## Usage
In CubeMX:
Create CLK and DIO pins with push-pull or open-drain configuration.
When using the latter, don't forget to add pullup, either external or internal.
When using push-pull, it might be safe to decrease TM1637_DELAY_US, since TM1637 supports
up to 250 kHz communication.
When using FreeRTOS, make sure there is some dynamic memory available (lib needs it when
integrating with FreeRTOS).

in global scope:
```c
TM1637_HandleTypeDef htm1637 = {
  .brightness = 7,
  .digits = 4,
  .clk_port = ...,
  .clk_pin = ...,
  .dio_port = ...,
  .dio_pin = ...,
};
```
in main():
```c
// When using FreeRTOS, put it AFTER osKernelInitialize()!!!
TM1637_Init(&htm1637);

// To automatically refresh displays. Must be called only once. Requires FreeRTOS
TM1637_StartTask(NULL);
```
if not using FreeRTOS:
```c
// Infinite loop in main():
while(1) {
  TM1637_Tick();
  // User code
}
```
## API
See tm1637.h for list of available functions.

See Examples/ for concrete usecases.
