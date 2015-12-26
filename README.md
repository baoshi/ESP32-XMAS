# What is this?

This is a simple program that receives data from WebSocket and dump them onto an LCD screen. It uses the Espressif
ESP31 (ESP32 Beta) module.

# Hardware Wiring

An 128x160 TFT panel based on ILI9163 is needed. It probably works for ST7735 panels but the initial sequence may need
change. Wiring from the TFT panel to ESP31 as follows:
    A0     - GPIO17
    Reset  - GPIO18
    #CS    - GPIO19
    CLK    - GPIO20
    SDI    - GPIO21
 
# Software setup

ESP32_RTOS_SDK (https://github.com/espressif/ESP32_RTOS_SDK) is used. The steps to setup development environment is fully
detailed in SDK README. 

# License/legal

This program uses LCD codes from Sprite_tm's ESP31-SMSEMUhttps://github.com/espressif/esp31-smsemu
The WebSocket server is based on Putilov Andrey 's CWebsocket https://github.com/m8rge/cwebsocket
Both are licensed under MIT license, so does mine.