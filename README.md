# Urban Noise Heatmapper

A low-cost, dual-microphone IoT device (ESP32-S3) that measures the noise pollution with a built-in confidence score, generating a real-time noise heatmap accessible via web and mobile. Built using the ESP32-S3 microcontroller with an INMP441 and a PDM MEMS microphone. The device cross checks its own readings to calculate a noise level (LAeq), peak level (Lmax), intermittency ratio and finally a confidence score; all processed on the device before being sent to a cloud server for spatial interpolation and heatmap generation.

# Hardware Components Used:

1) INMP441 Omnidirectional Microphone
2) PDM MEMS Microphone Breakout Module with JST SH Connector
3) ESP32-S3 WROOM-1 Dual Type-C USB N16R8 Micropython Board
4) 3.7V 1200mAh 25C Lipo Battery
5) 1A Type-C Lithium Battery Charger Module with Protection (3.7V/ 4.2V)

