# Sistema de Control de Acceso Biométrico Multifactor con ESP32

Proyecto tecnológico desarrollado para implementar un prototipo
de control de acceso a áreas restringidas mediante autenticación
multifactor.

## Factores de autenticación

1. Huella dactilar mediante sensor JM-101B.
2. Código temporal TOTP de seis dígitos.

## Hardware

- ESP32 NodeMCU DEVKIT V1
- Sensor biométrico JM-101B
- RTC DS3231
- OLED SSD1306
- Keypad 4x4
- Servo MG90S
- Sensor magnético de puerta
- Buzzer

## Funciones principales

- Registro y validación de huellas
- Segundo factor TOTP
- Control de apertura mediante servo
- Monitoreo del estado de la puerta
- Bloqueo por intentos fallidos
- Registro de eventos en SPIFFS
- Administración mediante Bluetooth

## Realizado por:

- MARIA TAIPE GUANOLUISA
- DENNIS CABRERA GUERRERO
