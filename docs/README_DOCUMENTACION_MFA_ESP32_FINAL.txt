PAQUETE DE DOCUMENTACIÓN FINAL – CONTROL DE ACCESO MFA ESP32

Contenido:
1. Manual_Tecnico_MFA_ESP32_FINAL.pdf
2. Mapa_Conexiones_MFA_ESP32_FINAL.pdf
3. Logica_Completa_MFA_ESP32_FINAL.pdf
4. Manual_Usuario_MFA_ESP32_FINAL.pdf
5. Manual_Administracion_Huella_PIN_MFA_ESP32_FINAL.pdf
6. Manual_Pruebas_Mantenimiento_MFA_ESP32_FINAL.pdf
7. Presentacion_Control_Acceso_MFA_ESP32_FINAL.pptx

Base técnica:
- ESP32 NodeMCU DEVKIT V1, 30 pines
- JM-101B
- TOTP de 6 dígitos
- RTC DS3231
- OLED SSD1306
- Keypad 4x4
- Sensor magnético
- MG90S
- Buzzer
- Bluetooth Classic
- SPIFFS

Tiempos principales:
- TOTP: 30 s de inactividad
- Penalización: 30 s
- Espera de apertura: 10 s
- Cuenta regresiva: 5 s
- Alarma puerta abierta: 25 s
- Cierre estable: 3 s
- Menú/PIN administrativo: 30 s
- Captura de huella: 20 s por etapa
