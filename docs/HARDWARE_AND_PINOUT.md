# Hardware and Pinout

## Camera: AI Thinker ESP32-CAM / OV2640

| Signal | GPIO |
|---|---:|
| PWDN | 32 |
| RESET | -1 |
| XCLK | 0 |
| SIOD | 26 |
| SIOC | 27 |
| Y9 | 35 |
| Y8 | 34 |
| Y7 | 39 |
| Y6 | 36 |
| Y5 | 21 |
| Y4 | 19 |
| Y3 | 18 |
| Y2 | 5 |
| VSYNC | 25 |
| HREF | 23 |
| PCLK | 22 |

## SSD1306 OLED

| OLED | ESP32-CAM GPIO |
|---|---:|
| SDA | 13 |
| SCL | 14 |
| I2C address | 0x3C |

These are the project pin assignments. Confirm board revisions and peripheral conflicts before rewiring. Use a stable supply suitable for ESP32-CAM Wi-Fi/camera current peaks.