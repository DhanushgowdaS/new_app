# Mini Camera Robot

An ESP32-CAM based camera system with an SSD1306 OLED preview, browser-based live streaming, photo capture, and email delivery.

**Board:** AI Thinker ESP32-CAM (OV2640)  
**Display:** SSD1306 128×64 I2C OLED  
**Repository:** [DhanushgowdaS/new_app](https://github.com/DhanushgowdaS/new_app)

## Features

- Live MJPEG camera stream in a browser.
- Low-resolution camera preview on the OLED.
- Capture a photo from the web dashboard.
- Send the captured photo through Gmail SMTP.
- Show capture/email status in the dashboard.

## Repository structure

```text
new_app/
├── Camera_Image_Email/
│   └── README.md
├── Dashboard/
│   └── README.md
├── Object_Detection/
│   ├── README.md
│   └── PC_Inference/
│       └── README.md
├── docs/
│   └── HARDWARE_AND_PINOUT.md
├── Libraries/
│   └── libraries.txt
├── .gitignore
├── LICENSE
└── README.md
```

## Hardware

| Part | Details |
|---|---|
| Board/camera | AI Thinker ESP32-CAM with OV2640 |
| OLED | SSD1306 128×64 |
| OLED SDA | GPIO 13 |
| OLED SCL | GPIO 14 |
| OLED address | 0x3C |

See [hardware and pinout](docs/HARDWARE_AND_PINOUT.md).

## Getting started

1. Install Arduino IDE and Espressif ESP32 board support.
2. Install dependencies listed in [Libraries/libraries.txt](Libraries/libraries.txt).
3. Add the working camera/email sketch as `Camera_Image_Email/Camera_Image_Email.ino`.
4. Configure Wi-Fi and Gmail SMTP settings locally. Do not commit real credentials.
5. Select AI Thinker ESP32-CAM, upload, and read the IP address in Serial Monitor.
6. Open the device IP in a browser. The MJPEG stream is served on port 81.

## Security

Never commit Wi-Fi passwords, Gmail App Passwords, private keys, or personal access tokens. The repository should contain placeholders only. Keep actual credentials in your local untracked files. Revoke and replace any credential that has been exposed.

## Object detection roadmap

The `Object_Detection/` folder is reserved for a separate PC/laptop inference feature. The planned system reads the ESP32-CAM MJPEG stream and runs a general object-detection model on a computer. This implementation is not included yet and is deliberately kept separate from the camera/email baseline.

## Status

- [x] Repository structure and project documentation.
- [ ] Add the exact tested camera/email Arduino sketch.
- [ ] Export standalone dashboard HTML from the sketch (optional).
- [ ] Implement PC-based object detection.
- [ ] Integrate detection results into a dashboard.

**Note:** AI Thinker ESP32-CAM has limited compute and memory. For broad classes such as people, animals, vehicles, and everyday objects, PC inference is the practical starting point. On-device detection would require a small custom model and dataset.