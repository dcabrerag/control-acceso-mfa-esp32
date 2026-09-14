void actualizarPantalla(String l1, String l2) {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);  oled.print(l1);
  oled.setCursor(0, 20); oled.print(l2);
  oled.display();
}

void mostrarEstadoGrafico(String l1, String l2, bool esCerrado, bool dibujarIconos) {
  oled.clearDisplay();
  oled.setTextSize(1);
  
  // Cálculo matemático del centrado horizontal automático (Cada letra en tamaño 1 mide 6 px de ancho)
  int xL1 = (128 - (l1.length() * 6)) / 2;
  int xL2 = (128 - (l2.length() * 6)) / 2;
  
  // Dibujar Textos Centrados sobre el plano medio vertical de la pantalla
  oled.setCursor(xL1 < 0 ? 0 : xL1, 20); oled.print(l1);
  oled.setCursor(xL2 < 0 ? 0 : xL2, 32); oled.print(l2);
  
  if (dibujarIconos) {
    // Candado reubicado arriba a la izquierda (0,0) de forma segura
    if (esCerrado) {
      oled.drawBitmap(0, 0, imgCandadoCerrado, 16, 16, SSD1306_WHITE);
    } else {
      oled.drawBitmap(0, 0, imgCandadoAbierto, 16, 16, SSD1306_WHITE);
    }
    
    // Telemetría e íconos de estado alineados arriba a la derecha
    if (bluetoothCelularConectado) {
      oled.drawBitmap(96, 0, imgBluetooth, 16, 16, SSD1306_WHITE);
    }
    oled.drawBitmap(112, 0, imgBateria, 16, 16, SSD1306_WHITE);
    
    // Ícono profesional de huella digital centrado abajo (x: 56, y: 46)
    oled.drawBitmap(56, 46, imgIconoHuella, 16, 16, SSD1306_WHITE);
  }
  oled.display();
}

void mostrarPantallaErrorX() {
  oled.clearDisplay();
  oled.setTextSize(2);
  oled.setCursor(45, 10); oled.print("[X]");
  oled.setTextSize(1);
  oled.setCursor(25, 45); oled.print("OPERACION FALLIDA");
  oled.display();
  pitidoError();
  delay(1500);
}

void mostrarPantallaExitoVisto() {
  oled.clearDisplay();
  oled.setTextSize(2);
  oled.setCursor(45, 10); oled.print("[V]");
  oled.setTextSize(1);
  oled.setCursor(30, 45); oled.print("VERIFICADO OK");
  oled.display();
  pitidoValidacion();
  delay(1500);
}

void pitidoClick() {
  digitalWrite(PIN_BUZZER, HIGH); delay(50); digitalWrite(PIN_BUZZER, LOW);
}

void pitidoValidacion() {
  digitalWrite(PIN_BUZZER, HIGH); delay(100); digitalWrite(PIN_BUZZER, LOW); delay(50);
  digitalWrite(PIN_BUZZER, HIGH); delay(200); digitalWrite(PIN_BUZZER, LOW);
}

void pitidoError() {
  digitalWrite(PIN_BUZZER, HIGH); delay(500); digitalWrite(PIN_BUZZER, LOW);
}

void musicaBienvenida() {
  int notas[] = {261, 329, 392, 523};
  for (int i = 0; i < 4; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delayMicroseconds(500000 / notas[i]);
    digitalWrite(PIN_BUZZER, LOW);
    delay(100);
  }
}

void registrarLog(String clasificacion, int usuarioID, String detalleCausa) {
  File logFile = SPIFFS.open("/registro_accesos.txt", FILE_APPEND);
  if (!logFile) {
    Serial.println("Imposible escribir log en SPIFFS");
    return;
  }
  
  String timestamp = "00/00/0000 00:00:00";
  if (rtc.begin()) {
    DateTime ahora = rtc.now();
    char buffer[20];
    sprintf(buffer, "%02d/%02d/%04d %02d:%02d:%02d", ahora.day(), ahora.month(), ahora.year(), ahora.hour(), ahora.minute(), ahora.second());
    timestamp = String(buffer);
  }
  
  String entradaLog = "[" + timestamp + "] [" + clasificacion + "] UID:" + String(usuarioID) + " -> " + detalleCausa;
  logFile.println(entradaLog);
  Serial.println("LOG CAPTURADO: " + entradaLog);
  logFile.close();
}