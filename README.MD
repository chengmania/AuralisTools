📟 Open Auralis Tools
A Modular ESP32-S3-Based Tuning Assistant for Piano Technicians and Musicians

Description:
Open Auralis Tools is an open-source, menu-driven tuning utility built on the Seeed Studio XIAO ESP32-S3. Designed for portability and precision, it offers a modern interface for exploring pitch, beat rates, tuning stretches, and environmental feedback in real time. Featuring an OLED display, rotary encoder navigation, and tone generation through PWM audio, this tool is especially tailored to support piano tuners, educators, and experimental musicians.

Core Features:

🎵 Tone Generator: Play reference tones including A440 and a full tuning assist palette (F3–A4) with stretchable octaves.

🧠 Octave Stretch Control: Adjust and store octave width preferences using beats-per-second (BPS) values.

🎛️ Rotary Encoder UI: Navigate menus and tune precisely with tactile control and visual feedback.

📶 Beat Rate Generator: Select beat rates (1–11 BPS) to simulate tuning intervals and verify aurally.

🌡️ Environment Monitor: Read temperature and humidity using the onboard AHT20 sensor.

🗂️ Flash Storage: Automatically stores octave stretch preferences across power cycles.

⚙️ FreeRTOS Multitasking: Handles user input, audio output, and environment updates concurrently for responsiveness.

Hardware Requirements:

Seeed Studio XIAO ESP32-S3

SSD1306 128x32 OLED display (I2C)

Rotary encoder with button

Adafruit STEMMA speaker or equivalent (PWM audio via GPIO8)

AHT20 temperature/humidity sensor

1kΩ resistor + 0.1µF capacitor (RC filter for audio smoothing)

License:
MIT License – freely usable and modifiable for both private and commercial projects.
