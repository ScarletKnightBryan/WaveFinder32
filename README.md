ESP32 Wi-Fi CSI Motion Detection

An embedded C++ project that uses an ESP32 and Wi-Fi Channel State Information (CSI) to detect potential motion by analyzing changes in wireless signals.

Built with ESP-IDF, the system extracts I/Q data from Wi-Fi packets, calculates signal magnitudes, and compares consecutive measurements to identify possible movement.

Features

Wi-Fi CSI collection and I/Q signal processing

Magnitude-based motion detection

Consecutive-packet threshold detection to reduce false positives

Wi-Fi reconnection and SNTP timestamp support

UDP networking module for remote data transmission

Planned Features

FreeRTOS queues for asynchronous data processing

Circular buffers and adaptive motion detection

Real-time visualization dashboard

Improved motion detection and filtering

Tech Stack

C++ | ESP-IDF | ESP32 | FreeRTOS (planned) | UDP | Wi-Fi CSI

Project Status

Work in Progress — Core CSI collection and basic motion-detection logic are implemented. UDP integration and improved detection algorithms are under development.
