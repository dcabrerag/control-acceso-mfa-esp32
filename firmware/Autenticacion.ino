unsigned long marcaInicioMenuAdmin = 0;

void BtCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  if (event == ESP_SPP_SRV_OPEN_EVT) {
    bluetoothCelularConectado = true;
    sppHandleActual = param->srv_open.handle;
    marcaTiempoConexionBT = millis();
    
    char macStr[18]; 
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", param->srv_open.rem_bda[0], param->srv_open.rem_bda[1], param->srv_open.rem_bda[2], param->srv_open.rem_bda[3], param->srv_open.rem_bda[4], param->srv_open.rem_bda[5]);
    String cadenaMac = String(macStr); Serial.printf("\n>>> DISPOSITIVO CONECTADO -> MAC: %s\n", cadenaMac.c_str());
    registrarLog("CONEXION_BT", 0, "MAC: " + cadenaMac); pitidoValidacion();
    if (estadoActual == ACCESO_HUELLA) mostrarEstadoGrafico("CONTROL MFA", "Ponga su huella", true, true);
  }
  if (event == ESP_SPP_CLOSE_EVT) {
    bluetoothCelularConectado = false;
    sppHandleActual = 0;
    tokenPendientePorEnviar = false; 
    Serial.println("\n<<< DISPOSITIVO DESCONECTADO DEL PERIMETRO");
    registrarLog("DESCONEXION_BT", 0, "Celular desconectado");
    if (estadoActual == ACCESO_HUELLA) mostrarEstadoGrafico("CONTROL MFA", "Ponga su huella", true, true);
  }
}

void BtAuthCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
  if (event == ESP_BT_GAP_KEY_REQ_EVT) {
    memcpy(macRemotaDispositivo, param->key_notif.bda, 6); estadoActual = AUTENTICACION_BT; entradaTeclado = "";
    actualizarPantalla("ALERTA VINCULACION", "INGRESE PIN FIJO...");
  }
  if (event == ESP_BT_GAP_AUTH_CMPL_EVT && param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
    Serial.println("VINCULACION BLUETOOTH EXITOSA"); registrarLog("INFO", 0, "Celular vinculado por BT");
  }
}

void gestionarFaseHuella() {
  char tAdmin = teclado.getKey();
  if (tAdmin == 'A') {
    estadoActual = MODO_ADMIN;
    marcaInicioMenuAdmin = millis();
    pitidoClick();
    actualizarPantalla("MODO ADMIN", "* SALIR   # OK");
    return;
  }
  
  int res = finger.getImage(); if (res == FINGERPRINT_NOFINGER) return;
  if (res != FINGERPRINT_OK) { mostrarPantallaErrorX(); registrarLog("ERROR", 0, "Fallo sensor dactilar"); return; }
  if (finger.image2Tz() != FINGERPRINT_OK) return;
  
  if (finger.fingerFastSearch() == FINGERPRINT_OK) {
    usuarioAutenticadoID = finger.fingerID; DateTime ahora = rtc.now();
    tokenGeneradoMFA = String(totp.getCode(ahora.unixtime())); 
    while(tokenGeneradoMFA.length() < 6) tokenGeneradoMFA = "0" + tokenGeneradoMFA;
    
    tokenPendientePorEnviar = true; 
    estadoActual = ACCESO_TOTP; 
    marcaTiempoInactividad = millis(); 
    registrarLog("HUELLA_OK", usuarioAutenticadoID, "Huella aceptada");
    pitidoValidacion(); 
    actualizarPantalla("HUELLA CORRECTA", "Ingrese TOTP:");
  } else { 
    registrarLog("ERROR", 0, "Huella no registrada"); 
    mostrarPantallaErrorX(); 
    mostrarEstadoGrafico("CONTROL MFA", "Ponga su huella", true, true); 
  }
}

void verificarEnvioRetrasadoTOTP() {
  if (tokenPendientePorEnviar && bluetoothCelularConectado) {
    if (millis() - marcaTiempoConexionBT >= 2000) { 
      SerialBT.println("----- ALERTA DE ACCESO -----");
      SerialBT.println("Usuario UID " + String(usuarioAutenticadoID) + " verificado.");
      SerialBT.println("Su codigo temporal TOTP es: " + tokenGeneradoMFA);
      SerialBT.println("Valido por 30 segundos.");
      SerialBT.println("----------------------------");
      tokenPendientePorEnviar = false; 
    }
  }
}

void gestionarFaseTOTP() {
  if (millis() - marcaTiempoInactividad >= TIMEOUT_INACTIVIDAD) {
    registrarLog("ERROR", usuarioAutenticadoID, "Timeout inactividad"); 
    SerialBT.println("ALERTA: Tiempo de espera agotado. Sesion terminada.");
    forzarDesconexionBluetooth(); 
    mostrarPantallaErrorX(); entradaTeclado = ""; usuarioAutenticadoID = 0; tokenGeneradoMFA = ""; estadoActual = ACCESO_HUELLA;
    marcaInicioMenuAdmin = 0;
    mostrarEstadoGrafico("CONTROL MFA", "Ponga su huella", true, true); return;
  }
  
  char tecla = teclado.getKey(); if (!tecla) return;
  marcaTiempoInactividad = millis();
  
  if (tecla == '#') {
    if (entradaTeclado == tokenGeneradoMFA) { contadorErrores = 0; estadoActual = CONCEDIDO; }
    else {
      contadorErrores++; 
      registrarLog("ERROR", usuarioAutenticadoID, "TOTP incorrecto"); 
      SerialBT.println("ALERTA: Codigo TOTP incorrecto."); 
      mostrarPantallaErrorX(); 
      if (contadorErrores >= LIMITE_INTENTOS) { 
        forzarDesconexionBluetooth(); 
        estadoActual = PENALIZACION; marcaTiempoBloqueo = millis(); 
        registrarLog("BLOQUEO", usuarioAutenticadoID, "Exceso intentos"); 
      }
      else { 
        actualizarPantalla("ERROR: TOTP", "Intentos: " + String(LIMITE_INTENTOS - contadorErrores)); 
        delay(2000); actualizarPantalla("CONTROL MFA", "Ingrese TOTP:"); 
        marcaTiempoInactividad = millis(); 
      }
    } entradaTeclado = "";
  } else if (tecla == '*') { 
    entradaTeclado = ""; pitidoClick(); 
    actualizarPantalla("CONTROL MFA", "Ingrese TOTP:"); 
    marcaTiempoInactividad = millis(); 
  } else if (entradaTeclado.length() < 6) { 
    entradaTeclado += tecla; pitidoClick(); 
    oled.clearDisplay(); oled.setCursor(0,0); oled.print("INGRESE TOTP");
    oled.setCursor(0,20); oled.print("N:" + String(entradaTeclado.length()) + "/6"); oled.display();
  }
}

void operarCerradura() {

  unsigned long ahora = millis();

  // Primera entrada al estado CONCEDIDO
  if (!cicloCerraduraActivo) {

    cicloCerraduraActivo = true;

    fasePuerta = PUERTA_ESPERANDO_APERTURA;

    marcaInicioCicloPuerta = ahora;
    marcaTiempoPuertaAbierta = 0;
    marcaTiempoCierreEstable = 0;

    cuentaRegresivaCierreSinApertura = false;
    alarmaPuertaActiva = false;

    mostrarPantallaExitoVisto();

    mostrarEstadoGrafico(
      "ACCESO EXITOSO",
      "Abra la puerta",
      false,
      true
    );

    SerialBT.println(
      "ACCESO CONCEDIDO: Abra el perimetro."
    );

    registrarLog(
      "ACCESO",
      usuarioAutenticadoID,
      "Cerradura abierta"
    );

    // Abrir cerradura
    cerraduraMG90S.write(90);

    Serial.println(
      "Cerradura abierta. Esperando apertura de puerta..."
    );
  }
}

void verificarSensorPuerta() {

  unsigned long ahora = millis();

  int estadoPuerta =
    digitalRead(PIN_SENSOR_MAGNETICO);


  // ==========================================================
  // CONCEDIDO: ESPERANDO QUE ABRAN LA PUERTA
  // ==========================================================

  if (
    estadoActual == CONCEDIDO &&
    fasePuerta == PUERTA_ESPERANDO_APERTURA
  ) {

    // HIGH = puerta abierta
    if (estadoPuerta == HIGH) {

      fasePuerta = PUERTA_ABIERTA;

      marcaTiempoPuertaAbierta = ahora;

      cuentaRegresivaCierreSinApertura = false;

      Serial.println(
        ">>> PUERTA ABIERTA DETECTADA"
      );

      mostrarEstadoGrafico(
        "PUERTA ABIERTA",
        "Detectado por sensor",
        false,
        true
      );

      return;
    }


    // Todavía no abrió.
    // Después de 10 segundos comienza cuenta de 5 segundos.

    if (
      !cuentaRegresivaCierreSinApertura &&
      ahora - marcaInicioCicloPuerta >=
      TIEMPO_ESPERA_APERTURA
    ) {

      cuentaRegresivaCierreSinApertura = true;

      marcaInicioCuentaRegresiva = ahora;

      registrarLog(
        "TIMEOUT",
        usuarioAutenticadoID,
        "Puerta no abierta en 10 segundos"
      );

      SerialBT.println(
        "ALERTA: La puerta no fue abierta. Bloqueo en 5 segundos."
      );
    }


    // Cuenta regresiva de 5 segundos
    if (cuentaRegresivaCierreSinApertura) {

      unsigned long transcurrido =
        ahora - marcaInicioCuentaRegresiva;

      if (
        transcurrido >=
        TIEMPO_CUENTA_REGRESIVA
      ) {

        digitalWrite(
          PIN_BUZZER,
          LOW
        );

        cerraduraMG90S.write(0);

        pitidoClick();

        mostrarEstadoGrafico(
          "TIEMPO AGOTADO",
          "Area bloqueada",
          true,
          true
        );

        registrarLog(
          "CIERRE",
          usuarioAutenticadoID,
          "Cierre automatico por falta de apertura"
        );

        SerialBT.println(
          "NOTIFICACION: Tiempo agotado. Perimetro bloqueado."
        );

        forzarDesconexionBluetooth();

        entradaTeclado = "";
        usuarioAutenticadoID = 0;
        tokenGeneradoMFA = "";
        tokenPendientePorEnviar = false;

        cicloCerraduraActivo = false;
        cuentaRegresivaCierreSinApertura = false;
        alarmaPuertaActiva = false;

        fasePuerta = PUERTA_ESPERANDO_APERTURA;

        marcaTiempoPuertaAbierta = 0;
        marcaTiempoCierreEstable = 0;
        marcaInicioCuentaRegresiva = 0;

        estadoActual = ACCESO_HUELLA;

        delay(2000);

        mostrarEstadoGrafico(
          "CONTROL MFA",
          "Ponga su huella",
          true,
          true
        );

        return;
      }


      // Buzzer durante los 5 segundos
      digitalWrite(
        PIN_BUZZER,
        ((ahora / 250) % 2 == 0)
          ? HIGH
          : LOW
      );

      int segundosRestantes =
        5 - (transcurrido / 1000);

      if (segundosRestantes < 1)
        segundosRestantes = 1;

      mostrarEstadoGrafico(
        "SIN APERTURA",
        "Bloqueo en " +
        String(segundosRestantes) +
        "s",
        true,
        true
      );
    }

    return;
  }


  // ==========================================================
  // PUERTA ABIERTA
  // ==========================================================

  if (
    (estadoActual == CONCEDIDO ||
     estadoActual == PUERTA_ABIERTA_ALERTA) &&
    fasePuerta == PUERTA_ABIERTA
  ) {

    // --------------------------------------------------------
    // LA PUERTA SE CERRO
    // --------------------------------------------------------

    if (estadoPuerta == LOW) {

      digitalWrite(
        PIN_BUZZER,
        LOW
      );

      alarmaPuertaActiva = false;

      fasePuerta =
        PUERTA_CIERRE_ESTABLE;

      marcaTiempoCierreEstable =
        ahora;

      marcaTiempoPuertaAbierta = 0;

      estadoActual = CONCEDIDO;

      Serial.println(
        ">>> PUERTA CERRADA"
      );

      Serial.println(
        ">>> INICIANDO VERIFICACION DE 3 SEGUNDOS"
      );

      mostrarEstadoGrafico(
        "PUERTA CERRADA",
        "Cierre estable: 3s",
        true,
        true
      );

      registrarLog(
        "SISTEMA_OK",
        0,
        "Puerta cerrada, alarma apagada"
      );

      return;
    }


    // --------------------------------------------------------
    // LA PUERTA CONTINUA ABIERTA
    // --------------------------------------------------------

    if (marcaTiempoPuertaAbierta == 0) {

      marcaTiempoPuertaAbierta =
        ahora;
    }


    unsigned long tiempoAbierta =
      ahora - marcaTiempoPuertaAbierta;


    // --------------------------------------------------------
    // 25 SEGUNDOS ABIERTA -> ALARMA
    // --------------------------------------------------------

    if (
      tiempoAbierta >=
      TIEMPO_MAX_ABIERTA
    ) {

      if (
        estadoActual !=
        PUERTA_ABIERTA_ALERTA
      ) {

        estadoActual =
          PUERTA_ABIERTA_ALERTA;

        alarmaPuertaActiva = true;

        registrarLog(
          "ALARMA",
          0,
          "Puerta dejada abierta durante 25 segundos"
        );

        SerialBT.println(
          "ALERTA CRITICA: ¡La puerta ha quedado abierta!"
        );

        Serial.println(
          ">>> ALARMA ACTIVADA: 25 SEGUNDOS"
        );
      }


      // Buzzer permanece activo mientras esté abierta
      digitalWrite(
        PIN_BUZZER,
        ((ahora / 200) % 2 == 0)
          ? HIGH
          : LOW
      );


      mostrarEstadoGrafico(
        "ALERTA SEGURIDAD",
        "CIERRE LA PUERTA",
        false,
        true
      );
    }

    return;
  }


  // ==========================================================
  // PUERTA CERRADA - ESPERANDO 3 SEGUNDOS
  // ==========================================================

  if (
    estadoActual == CONCEDIDO &&
    fasePuerta == PUERTA_CIERRE_ESTABLE
  ) {

    // --------------------------------------------------------
    // SE VOLVIO A ABRIR
    // --------------------------------------------------------

    if (estadoPuerta == HIGH) {

      fasePuerta =
        PUERTA_ABIERTA;

      marcaTiempoPuertaAbierta =
        ahora;

      marcaTiempoCierreEstable = 0;

      digitalWrite(
        PIN_BUZZER,
        LOW
      );

      Serial.println(
        ">>> PUERTA REABIERTA"
      );

      Serial.println(
        ">>> CANCELANDO TEMPORIZADOR DE 3 SEGUNDOS"
      );

      mostrarEstadoGrafico(
        "PUERTA ABIERTA",
        "Cierre cancelado",
        false,
        true
      );

      return;
    }


    // --------------------------------------------------------
    // SIGUE CERRADA
    // --------------------------------------------------------

    if (
      ahora - marcaTiempoCierreEstable >=
      TIEMPO_CIERRE_ESTABLE
    ) {

      digitalWrite(
        PIN_BUZZER,
        LOW
      );

      // BLOQUEAR
      cerraduraMG90S.write(0);

      pitidoClick();

      mostrarEstadoGrafico(
        "PUERTA CERRADA",
        "Asegurando area",
        true,
        true
      );

      registrarLog(
        "CIERRE",
        0,
        "Cerradura asegurada despues de 3 segundos estables"
      );

      SerialBT.println(
        "NOTIFICACION: Puerta cerrada. Perimetro bloqueado."
      );

      Serial.println(
        ">>> 3 SEGUNDOS ESTABLES"
      );

      Serial.println(
        ">>> SERVO BLOQUEANDO A 0 GRADOS"
      );

      forzarDesconexionBluetooth();

      entradaTeclado = "";
      usuarioAutenticadoID = 0;
      tokenGeneradoMFA = "";
      tokenPendientePorEnviar = false;

      cicloCerraduraActivo = false;

      cuentaRegresivaCierreSinApertura = false;
      alarmaPuertaActiva = false;

      fasePuerta =
        PUERTA_ESPERANDO_APERTURA;

      marcaTiempoPuertaAbierta = 0;
      marcaTiempoCierreEstable = 0;
      marcaInicioCuentaRegresiva = 0;

      estadoActual =
        ACCESO_HUELLA;

      delay(2000);

      mostrarEstadoGrafico(
        "CONTROL MFA",
        "Ponga su huella",
        true,
        true
      );
    }
  }
}

// ================================================================
// MENU ADMINISTRATIVO: HUELLA Y CAMBIO DE PIN
// ================================================================
void mostrarMenuAdminOLED() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("MENU ADMIN");
  oled.setCursor(0, 18);
  oled.print("1 HUELLA   2 PIN");
  oled.setCursor(0, 36);
  oled.print("* SALIR");
  oled.display();
}

void mostrarPinAdminOLED(const String &titulo, int digitos, bool mostrarOK) {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print(titulo);
  oled.setCursor(0, 18);
  oled.print("DIGITOS: ");
  oled.print(digitos);
  oled.print("/4");
  oled.setCursor(0, 36);
  oled.print("* SALIR");
  if (mostrarOK) oled.print("   # OK");
  oled.display();
}

void mostrarSeleccionIDOLED() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("REGISTRO HUELLA");
  oled.setCursor(0, 18);
  oled.print("ID: 1 - 9");
  oled.setCursor(0, 36);
  oled.print("* SALIR");
  oled.display();
}

void volverPantallaPrincipalAdmin() {
  entradaTeclado = "";
  marcaInicioMenuAdmin = 0;
  estadoActual = ACCESO_HUELLA;
  mostrarEstadoGrafico("CONTROL MFA", "Ponga su huella", true, true);
}

void mostrarAdminTimeout(const String &mensaje) {
  actualizarPantalla("ADMIN TIMEOUT", mensaje);
  registrarLog("TIMEOUT", 0, mensaje);
  delay(1200);
  volverPantallaPrincipalAdmin();
}

void ejecutarEnrolamiento() {
  const unsigned long TIMEOUT_MENU_ADMIN = 30000UL;  // 30 segundos
  const unsigned long TIMEOUT_PIN_ADMIN  = 30000UL;  // 30 segundos

  // ------------------------------------------------------------
  // Cargar PIN maestro guardado
  // ------------------------------------------------------------
  String claveMaestraActual = "1234";

  if (SPIFFS.exists("/clave_maestra.txt")) {
    File fClave = SPIFFS.open("/clave_maestra.txt", FILE_READ);
    if (fClave) {
      String pinLeido = fClave.readStringUntil('\n');
      pinLeido.trim();
      if (pinLeido.length() == 4) claveMaestraActual = pinLeido;
      fClave.close();
    }
  }

  // ------------------------------------------------------------
  // Ingreso inicial al modo administrador
  // ------------------------------------------------------------
  if (marcaInicioMenuAdmin == 0) {
    marcaInicioMenuAdmin = millis();
    actualizarPantalla("MODO ADMIN", "* SALIR   # OK");
  }

  if (millis() - marcaInicioMenuAdmin >= TIMEOUT_MENU_ADMIN) {
    mostrarAdminTimeout("Sin cambios");
    return;
  }

  char tecla = teclado.getKey();
  if (!tecla) return;

  // ------------------------------------------------------------
  // SALIR con * desde el PIN inicial
  // ------------------------------------------------------------
  if (tecla == '*') {
    pitidoClick();
    actualizarPantalla("SALIDA ADMIN", "Sin cambios");
    delay(1000);
    volverPantallaPrincipalAdmin();
    return;
  }

  // ------------------------------------------------------------
  // INGRESAR PIN MAESTRO
  // ------------------------------------------------------------
  if (tecla >= '0' && tecla <= '9') {
    if (entradaTeclado.length() < 4) {
      entradaTeclado += tecla;
      marcaInicioMenuAdmin = millis();
      pitidoClick();
      mostrarPinAdminOLED("PIN ADMIN", entradaTeclado.length(), true);
    }
    return;
  }

  // ------------------------------------------------------------
  // CONFIRMAR PIN MAESTRO con #
  // ------------------------------------------------------------
  if (tecla != '#') return;

  if (entradaTeclado.length() != 4) {
    pitidoClick();
    actualizarPantalla("PIN INCOMPLETO", "4 digitos + #");
    delay(1000);
    mostrarPinAdminOLED("PIN ADMIN", entradaTeclado.length(), true);
    marcaInicioMenuAdmin = millis();
    return;
  }

  if (entradaTeclado != claveMaestraActual) {
    mostrarPantallaErrorX();
    actualizarPantalla("PIN INCORRECTO", "Sin cambios");
    registrarLog("ERROR", 0, "PIN maestro incorrecto");
    delay(1200);
    entradaTeclado = "";
    marcaInicioMenuAdmin = millis();
    actualizarPantalla("MODO ADMIN", "* SALIR   # OK");
    return;
  }

  // ------------------------------------------------------------
  // PIN CORRECTO -> MENU ADMIN
  // ------------------------------------------------------------
  entradaTeclado = "";
  pitidoValidacion();
  mostrarMenuAdminOLED();

  unsigned long inicioMenu = millis();
  char opcion = 0;

  while (opcion == 0) {
    char t = teclado.getKey();

    if (t == '*') {
      opcion = '*';
      pitidoClick();
    }
    else if (t == '1' || t == '2') {
      opcion = t;
      pitidoClick();
    }

    if (millis() - inicioMenu >= TIMEOUT_MENU_ADMIN) {
      mostrarAdminTimeout("Menu sin actividad");
      return;
    }

    delay(10);
  }

  // ------------------------------------------------------------
  // SALIR DEL MENU ADMIN
  // ------------------------------------------------------------
  if (opcion == '*') {
    actualizarPantalla("SALIDA ADMIN", "Sin cambios");
    delay(1000);
    volverPantallaPrincipalAdmin();
    return;
  }

  // ============================================================
  // OPCION 1: REGISTRO / REEMPLAZO DE HUELLA
  // ============================================================
  if (opcion == '1') {
    const unsigned long TIMEOUT_ID = 30000UL;

    mostrarSeleccionIDOLED();
    unsigned long inicioID = millis();
    char idChar = 0;

    while (idChar == 0) {
      char t = teclado.getKey();

      if (t == '*') {
        pitidoClick();
        actualizarPantalla("SALIDA ADMIN", "Sin cambios");
        delay(1000);
        volverPantallaPrincipalAdmin();
        return;
      }

      if (t >= '1' && t <= '9') {
        idChar = t;
        pitidoClick();
      }

      if (millis() - inicioID >= TIMEOUT_ID) {
        actualizarPantalla("ID TIMEOUT", "Sin cambios");
        registrarLog("TIMEOUT", 0, "Seleccion de ID de huella agotada");
        delay(1200);
        volverPantallaPrincipalAdmin();
        return;
      }

      delay(10);
    }

    uint8_t idHuella = (uint8_t)(idChar - '0');

    // ----------------------------------------------------------
    // Comprobar si el ID ya esta ocupado
    // ----------------------------------------------------------
    bool idOcupado = (finger.loadModel(idHuella) == FINGERPRINT_OK);

    if (idOcupado) {
      oled.clearDisplay();
      oled.setTextSize(1);
      oled.setCursor(0, 0);
      oled.print("ID ");
      oled.print(idHuella);
      oled.print(" OCUPADO");
      oled.setCursor(0, 18);
      oled.print("A REEMPLAZAR");
      oled.setCursor(0, 36);
      oled.print("* SALIR");
      oled.display();

      unsigned long inicioConfirmacion = millis();
      char confirmacion = 0;

      while (confirmacion == 0) {
        char t = teclado.getKey();

        if (t == 'A' || t == '*') {
          confirmacion = t;
          pitidoClick();
        }

        if (millis() - inicioConfirmacion >= TIMEOUT_ID) {
          actualizarPantalla("CONFIRMACION", "Tiempo agotado");
          registrarLog("TIMEOUT", idHuella, "Confirmacion de reemplazo agotada");
          delay(1200);
          volverPantallaPrincipalAdmin();
          return;
        }

        delay(10);
      }

      if (confirmacion == '*') {
        actualizarPantalla("CAMBIO CANCELADO", "Huella sin cambios");
        delay(1200);
        volverPantallaPrincipalAdmin();
        return;
      }
    }

    // ----------------------------------------------------------
    // Registrar nueva huella / reemplazar huella existente
    // ----------------------------------------------------------
    bool registroOK = registrarHuellaEnID(idHuella);

    if (registroOK) {
      pitidoValidacion();
      actualizarPantalla("HUELLA GUARDADA", "ID: " + String(idHuella));
      registrarLog(
        "ADMIN",
        idHuella,
        idOcupado ? "Huella reemplazada correctamente" : "Huella registrada correctamente"
      );
    }
    else {
      mostrarPantallaErrorX();
      actualizarPantalla("ERROR HUELLA", "Sin cambios");
      registrarLog("ERROR", idHuella, "Fallo o timeout en registro de huella");
    }

    delay(1800);
    volverPantallaPrincipalAdmin();
    return;
  }

  // ============================================================
  // OPCION 2: CAMBIO DEL PIN MAESTRO
  // ============================================================
  if (opcion == '2') {
    const unsigned long TIMEOUT_PIN = 30000UL;

    String pinActualIngresado = "";
    String nuevoPIN = "";
    String confirmacionPIN = "";

    // ----------------------------------------------------------
    // PASO 1: verificar PIN actual
    // ----------------------------------------------------------
    mostrarPinAdminOLED("PIN ACTUAL", 0, true);
    unsigned long inicioPinActual = millis();

    while (pinActualIngresado.length() < 4) {
      char t = teclado.getKey();

      if (t == '*') {
        pitidoClick();
        actualizarPantalla("CAMBIO CANCELADO", "PIN sin cambios");
        delay(1200);
        volverPantallaPrincipalAdmin();
        return;
      }

      if (t >= '0' && t <= '9') {
        pinActualIngresado += t;
        pitidoClick();
        inicioPinActual = millis();
        mostrarPinAdminOLED("PIN ACTUAL", pinActualIngresado.length(), true);
      }

      if (millis() - inicioPinActual >= TIMEOUT_PIN) {
        actualizarPantalla("PIN TIMEOUT", "Sin cambios");
        registrarLog("TIMEOUT", 0, "Ingreso de PIN actual agotado");
        delay(1200);
        volverPantallaPrincipalAdmin();
        return;
      }

      delay(10);
    }

    // Esperar # para confirmar el PIN actual
    mostrarPinAdminOLED("PIN ACTUAL", 4, true);
    unsigned long inicioOKActual = millis();
    bool okActual = false;

    while (!okActual) {
      char t = teclado.getKey();

      if (t == '*') {
        pitidoClick();
        actualizarPantalla("CAMBIO CANCELADO", "PIN sin cambios");
        delay(1200);
        volverPantallaPrincipalAdmin();
        return;
      }

      if (t == '#') {
        okActual = true;
        pitidoClick();
      }

      if (millis() - inicioOKActual >= TIMEOUT_PIN) {
        actualizarPantalla("PIN TIMEOUT", "Sin cambios");
        registrarLog("TIMEOUT", 0, "Confirmacion de PIN actual agotada");
        delay(1200);
        volverPantallaPrincipalAdmin();
        return;
      }

      delay(10);
    }

    if (pinActualIngresado != claveMaestraActual) {
      mostrarPantallaErrorX();
      actualizarPantalla("PIN INCORRECTO", "Sin cambios");
      registrarLog("ERROR", 0, "Cambio PIN rechazado: PIN actual incorrecto");
      delay(1800);
      volverPantallaPrincipalAdmin();
      return;
    }

    pitidoValidacion();

    // ----------------------------------------------------------
    // PASO 2: nuevo PIN
    // ----------------------------------------------------------
    mostrarPinAdminOLED("NUEVO PIN", 0, true);
    unsigned long inicioNuevoPIN = millis();

    while (nuevoPIN.length() < 4) {
      char t = teclado.getKey();

      if (t == '*') {
        pitidoClick();
        actualizarPantalla("CAMBIO CANCELADO", "PIN sin cambios");
        delay(1200);
        volverPantallaPrincipalAdmin();
        return;
      }

      if (t >= '0' && t <= '9') {
        nuevoPIN += t;
        pitidoClick();
        inicioNuevoPIN = millis();
        mostrarPinAdminOLED("NUEVO PIN", nuevoPIN.length(), true);
      }

      if (millis() - inicioNuevoPIN >= TIMEOUT_PIN) {
        actualizarPantalla("PIN TIMEOUT", "Sin cambios");
        registrarLog("TIMEOUT", 0, "Ingreso de nuevo PIN agotado");
        delay(1200);
        volverPantallaPrincipalAdmin();
        return;
      }

      delay(10);
    }

    // Esperar # para confirmar nuevo PIN
    mostrarPinAdminOLED("NUEVO PIN", 4, true);
    unsigned long inicioOKNuevo = millis();
    bool okNuevo = false;

    while (!okNuevo) {
      char t = teclado.getKey();

      if (t == '*') {
        pitidoClick();
        actualizarPantalla("CAMBIO CANCELADO", "PIN sin cambios");
        delay(1200);
        volverPantallaPrincipalAdmin();
        return;
      }

      if (t == '#') {
        okNuevo = true;
        pitidoClick();
      }

      if (millis() - inicioOKNuevo >= TIMEOUT_PIN) {
        actualizarPantalla("PIN TIMEOUT", "Sin cambios");
        registrarLog("TIMEOUT", 0, "Confirmacion de nuevo PIN agotada");
        delay(1200);
        volverPantallaPrincipalAdmin();
        return;
      }

      delay(10);
    }

    // ----------------------------------------------------------
    // PASO 3: confirmar nuevo PIN
    // ----------------------------------------------------------
    mostrarPinAdminOLED("CONFIRMAR PIN", 0, true);
    unsigned long inicioConfirmacionPIN = millis();

    while (confirmacionPIN.length() < 4) {
      char t = teclado.getKey();

      if (t == '*') {
        pitidoClick();
        actualizarPantalla("CAMBIO CANCELADO", "PIN sin cambios");
        delay(1200);
        volverPantallaPrincipalAdmin();
        return;
      }

      if (t >= '0' && t <= '9') {
        confirmacionPIN += t;
        pitidoClick();
        inicioConfirmacionPIN = millis();
        mostrarPinAdminOLED("CONFIRMAR PIN", confirmacionPIN.length(), true);
      }

      if (millis() - inicioConfirmacionPIN >= TIMEOUT_PIN) {
        actualizarPantalla("PIN TIMEOUT", "Sin cambios");
        registrarLog("TIMEOUT", 0, "Confirmacion del nuevo PIN agotada");
        delay(1200);
        volverPantallaPrincipalAdmin();
        return;
      }

      delay(10);
    }

    // Esperar # para confirmar la repeticion
    mostrarPinAdminOLED("CONFIRMAR PIN", 4, true);
    unsigned long inicioOKConfirmacion = millis();
    bool okConfirmacion = false;

    while (!okConfirmacion) {
      char t = teclado.getKey();

      if (t == '*') {
        pitidoClick();
        actualizarPantalla("CAMBIO CANCELADO", "PIN sin cambios");
        delay(1200);
        volverPantallaPrincipalAdmin();
        return;
      }

      if (t == '#') {
        okConfirmacion = true;
        pitidoClick();
      }

      if (millis() - inicioOKConfirmacion >= TIMEOUT_PIN) {
        actualizarPantalla("PIN TIMEOUT", "Sin cambios");
        registrarLog("TIMEOUT", 0, "Confirmacion final del PIN agotada");
        delay(1200);
        volverPantallaPrincipalAdmin();
        return;
      }

      delay(10);
    }

    // ----------------------------------------------------------
    // PASO 4: comparar y guardar
    // ----------------------------------------------------------
    if (nuevoPIN != confirmacionPIN) {
      mostrarPantallaErrorX();
      actualizarPantalla("PIN NO COINCIDE", "Sin cambios");
      registrarLog("ERROR", 0, "Cambio PIN rechazado: no coincide");
      delay(1800);
      volverPantallaPrincipalAdmin();
      return;
    }

    File fNuevoPIN = SPIFFS.open("/clave_maestra.txt", FILE_WRITE);

    if (!fNuevoPIN) {
      mostrarPantallaErrorX();
      actualizarPantalla("ERROR GUARDADO", "PIN sin cambios");
      registrarLog("ERROR", 0, "No se pudo abrir clave_maestra.txt");
      delay(1800);
      volverPantallaPrincipalAdmin();
      return;
    }

    fNuevoPIN.println(nuevoPIN);
    fNuevoPIN.close();

    // Verificacion inmediata del archivo
    String pinVerificado = "";
    File fVerificarPIN = SPIFFS.open("/clave_maestra.txt", FILE_READ);

    if (fVerificarPIN) {
      pinVerificado = fVerificarPIN.readStringUntil('\n');
      pinVerificado.trim();
      fVerificarPIN.close();
    }

    if (pinVerificado == nuevoPIN) {
      pitidoValidacion();
      actualizarPantalla("PIN CAMBIADO", "Guardado correctamente");
      registrarLog("ADMIN", 0, "PIN maestro cambiado correctamente");
      delay(1800);
    }
    else {
      mostrarPantallaErrorX();
      actualizarPantalla("ERROR GUARDADO", "PIN sin cambios");
      registrarLog("ERROR", 0, "Fallo verificando nuevo PIN maestro");
      delay(1800);
    }

    volverPantallaPrincipalAdmin();
    return;
  }
}


// ================================================================
// REGISTRO / REEMPLAZO DE HUELLA EN EL JM-101B
// ================================================================
bool registrarHuellaEnID(uint8_t id) {
  const unsigned long TIMEOUT_CAPTURA_HUELLA = 20000UL; // 20 s por etapa

  uint8_t p;
  unsigned long inicioEtapa;

  // --------------------------------------------------------------
  // CAPTURA 1
  // --------------------------------------------------------------
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("COLOQUE DEDO");
  oled.setCursor(0, 18);
  oled.print("CAPTURA 1 / 2");
  oled.setCursor(0, 36);
  oled.print("* SALIR");
  oled.display();

  Serial.println(">>> Coloque el dedo para la captura 1/2");
  inicioEtapa = millis();

  while (true) {
    char t = teclado.getKey();

    if (t == '*') {
      pitidoClick();
      actualizarPantalla("REGISTRO CANCELADO", "Sin cambios");
      delay(1000);
      return false;
    }

    p = finger.getImage();

    if (p == FINGERPRINT_OK) break;

    if (p != FINGERPRINT_NOFINGER) {
      Serial.printf(">>> Error captura 1: %u\n", p);
      return false;
    }

    if (millis() - inicioEtapa >= TIMEOUT_CAPTURA_HUELLA) {
      actualizarPantalla("HUELLA TIMEOUT", "Sin cambios");
      registrarLog("TIMEOUT", id, "Captura 1 de huella agotada");
      delay(1000);
      return false;
    }

    delay(50);
  }

  p = finger.image2Tz(1);

  if (p != FINGERPRINT_OK) {
    Serial.printf(">>> Error convirtiendo captura 1: %u\n", p);
    return false;
  }

  // --------------------------------------------------------------
  // RETIRAR DEDO
  // --------------------------------------------------------------
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("RETIRE DEDO");
  oled.setCursor(0, 18);
  oled.print("ESPERANDO");
  oled.setCursor(0, 36);
  oled.print("* SALIR");
  oled.display();

  Serial.println(">>> Retire el dedo");
  inicioEtapa = millis();

  while (true) {
    char t = teclado.getKey();

    if (t == '*') {
      pitidoClick();
      actualizarPantalla("REGISTRO CANCELADO", "Sin cambios");
      delay(1000);
      return false;
    }

    p = finger.getImage();

    if (p == FINGERPRINT_NOFINGER) break;

    if (millis() - inicioEtapa >= TIMEOUT_CAPTURA_HUELLA) {
      actualizarPantalla("HUELLA TIMEOUT", "Retire el dedo");
      registrarLog("TIMEOUT", id, "Retiro de dedo agotado");
      delay(1000);
      return false;
    }

    delay(50);
  }

  // --------------------------------------------------------------
  // CAPTURA 2
  // --------------------------------------------------------------
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("COLOQUE DEDO");
  oled.setCursor(0, 18);
  oled.print("CAPTURA 2 / 2");
  oled.setCursor(0, 36);
  oled.print("* SALIR");
  oled.display();

  Serial.println(">>> Coloque nuevamente el mismo dedo para la captura 2/2");
  inicioEtapa = millis();

  while (true) {
    char t = teclado.getKey();

    if (t == '*') {
      pitidoClick();
      actualizarPantalla("REGISTRO CANCELADO", "Sin cambios");
      delay(1000);
      return false;
    }

    p = finger.getImage();

    if (p == FINGERPRINT_OK) break;

    if (p != FINGERPRINT_NOFINGER) {
      Serial.printf(">>> Error captura 2: %u\n", p);
      return false;
    }

    if (millis() - inicioEtapa >= TIMEOUT_CAPTURA_HUELLA) {
      actualizarPantalla("HUELLA TIMEOUT", "Sin cambios");
      registrarLog("TIMEOUT", id, "Captura 2 de huella agotada");
      delay(1000);
      return false;
    }

    delay(50);
  }

  p = finger.image2Tz(2);

  if (p != FINGERPRINT_OK) {
    Serial.printf(">>> Error convirtiendo captura 2: %u\n", p);
    return false;
  }

  // --------------------------------------------------------------
  // CREAR MODELO
  // --------------------------------------------------------------
  actualizarPantalla("PROCESANDO", "Creando huella...");
  p = finger.createModel();

  if (p != FINGERPRINT_OK) {
    Serial.printf(">>> Las capturas no coinciden. Codigo: %u\n", p);
    return false;
  }

  // --------------------------------------------------------------
  // GUARDAR MODELO
  // --------------------------------------------------------------
  p = finger.storeModel(id);

  if (p != FINGERPRINT_OK) {
    Serial.printf(">>> Error guardando huella en ID %u. Codigo: %u\n", id, p);
    return false;
  }

  Serial.printf(">>> HUELLA GUARDADA CORRECTAMENTE EN ID %u\n", id);
  return true;
}

void forzarDesconexionBluetooth() {
  if (sppHandleActual > 0) {
    esp_spp_disconnect(sppHandleActual);
    sppHandleActual = 0;
    bluetoothCelularConectado = false;
  }
}

void manejarBloqueoTemporal() {
  unsigned long tAct = millis(); long segRest = (DURACION_BLOQUEO - (tAct - marcaTiempoBloqueo)) / 1000;
  digitalWrite(PIN_BUZZER, (segRest % 2 == 0) ? HIGH : LOW);
  if (tAct - marcaTiempoBloqueo >= DURACION_BLOQUEO) {
    digitalWrite(PIN_BUZZER, LOW); contadorErrores = 0; entradaTeclado = ""; usuarioAutenticadoID = 0; tokenGeneradoMFA = "";
    mostrarEstadoGrafico("SISTEMA LIBERADO", "Ponga su huella", true, true); pitidoValidacion();
    delay(1500); estadoActual = ACCESO_HUELLA; mostrarEstadoGrafico("CONTROL MFA", "Ponga su huella", true, true);
  } else { actualizarPantalla("SISTEMA BLOQUEADO", "Espere: " + String(segRest) + "s"); delay(200); }
}