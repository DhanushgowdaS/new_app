# Object Detection

Planned feature to identify people, animals, vehicles, and common objects visible through the camera.

## Planned PC/laptop architecture
1. ESP32-CAM serves its MJPEG stream at `http://<ESP32-IP>:81/stream`.
2. A computer reads frames and runs a pretrained object-detection model.
3. The computer displays labels, confidence scores, and bounding boxes.

This feature is kept separate from the working camera/email baseline. It is not implemented yet. For broad general-purpose classes, PC inference is the recommended approach; on-device detection would require a small custom model (for example, Edge Impulse FOMO) and a prepared dataset.