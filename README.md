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
add_subdirectory(FULL_PATH_TO_TM1637-LIB)
...
target_link_libraries(${CMAKE_PROJECT_NAME} tm1637)
```
or manually add all the sources to your project (and include Inc dir).
Also copy file TM1637-LIB/Config/template_tm1637_config.h to your include dir
and rename it to "tm1637_config.h". Tweak it to your needs.
Can be used with FreeRTOS or without, depending on application.
## Usage
In CubeMX (optional):

Create CLK and DIO pins with open-drain configuration. Library will reconfigure them as necessary.
Add external pullup resistor, currently library does not support internal pullup.

You can use separate CLK and DIO pins for multiple displays, or you can use single common CLK pin and multiple (per display) DIO pins.
Using single DIO pin and multiple CLK pins is not supported (it will not work).

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
