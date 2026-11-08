# Camera Image Email

Feature folder for the ESP32-CAM camera streaming, OLED preview, photo capture, and Gmail delivery sketch.

Hardware: AI Thinker ESP32-CAM (OV2640) and SSD1306 OLED. OLED SDA GPIO13, SCL GPIO14, I2C address 0x3C.

Open the sketch in Arduino IDE, install dependencies listed in `../Libraries/libraries.txt`, set Wi-Fi and Gmail SMTP values locally, upload to the AI Thinker ESP32-CAM, then open the IP printed in Serial Monitor.

Never commit real Wi-Fi passwords or Gmail App Passwords. Keep this baseline separate from future object-detection experiments.