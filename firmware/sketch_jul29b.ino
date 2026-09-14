#include <Keypad.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>
#include <TOTP.h>
#include <ESP32Servo.h>
#include <FS.h>
#include <SPIFFS.h>
#include <Adafruit_Fingerprint.h>
#include <BluetoothSerial.h> 
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please enable BLUETOOTH in tools.
#endif

#define ANCHO_PANTALLA 128
#define ALTO_PANTALLA 64
Adafruit_SSD1306 oled(ANCHO_PANTALLA, ALTO_PANTALLA, &Wire, -1);

// Mapas de bits para interfaz OLED
const unsigned char PROGMEM imgCandadoCerrado[] = {
  0x07, 0xE0, 0x0F, 0xF0, 0x1E, 0x78, 0x1C, 0x38, 0x1C, 0x38, 0x1C, 0x38, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x3F, 0xFC, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

const unsigned char PROGMEM imgCandadoAbierto[] = {
  0x07, 0xE0, 0x0F, 0xF0, 0x1E, 0x00, 0x1C, 0x00, 0x1C, 0x00, 0x1C, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x3F, 0xFC, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

const unsigned char PROGMEM imgBateria[] = {
  0x7F, 0xFC, 0xFF, 0xFE, 0xC0, 0x03, 0xC7, 0xE3, 0xC7, 0xE3, 0xC7, 0xE3, 0xC7, 0xE3, 0xC7, 0xE3,
  0xC7, 0xE3, 0xC7, 0xE3, 0xC7, 0xE3, 0xC7, 0xE3, 0xC0, 0x03, 0xFF, 0xFE, 0x7F, 0xFC, 0x00, 0x00
};

const unsigned char PROGMEM imgBluetooth[] = {
  0x01, 0x00, 0x01, 0x80, 0x01, 0xC0, 0x01, 0xE0, 0x09, 0xF0, 0x0D, 0xE0, 0x07, 0xC0, 0x03, 0x80,
  0x03, 0x80, 0x07, 0xC0, 0x0D, 0xE0, 0x09, 0xF0, 0x01, 0xE0, 0x01, 0xC0, 0x01, 0x80, 0x01, 0x00
};

// Icono de Huella Dactilar profesional (16x16 píxeles)
const unsigned char PROGMEM imgIconoHuella[] = {
  0x01, 0x80, 0x07, 0xE0, 0x0E, 0x70, 0x1C, 0x38, 0x38, 0x1C, 0x31, 0x8C, 0x63, 0xC6, 0x67, 0xE6,
  0x67, 0x66, 0x6E, 0x76, 0x6C, 0x36, 0x78, 0x1E, 0x38, 0x1C, 0x1C, 0x38, 0x07, 0xE0, 0x01, 0x80
};

// Prototipos de funciones
void gestionarFaseHuella();
void gestionarFaseTOTP();
void operarCerradura();
void manejarBloqueoTemporal();
void ejecutarEnrolamiento();
void pitidoClick();
void pitidoValidacion();
void pitidoError();
void actualizarPantalla(String l1, String l2);
void registrarLog(String clasificacion, int usuarioID, String detalleCausa);
void BtAuthCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
void BtCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param);
void mostrarEstadoGrafico(String l1, String l2, bool esCerrado, bool dibujarIconos);
void mostrarPantallaErrorX();
void mostrarPantallaExitoVisto();
void musicaBienvenida();
void forzarDesconexionBluetooth();
void verificarEnvioRetrasadoTOTP();
void verificarSensorPuerta();

// Configuración Teclado Matricial
const byte FILAS = 4;
const byte COLUMNAS = 4;

char keys[FILAS][COLUMNAS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

byte pinFilas[FILAS] = {13, 12, 14, 27};
byte pinColumnas[COLUMNAS] = {26, 25, 33, 32};

Keypad teclado = Keypad(makeKeymap(keys), pinFilas, pinColumnas, FILAS, COLUMNAS);

// Pines Periféricos y Objetos de hardware
const int PIN_BUZZER = 2;
const int PIN_SERVO = 4;
const int PIN_SENSOR_MAGNETICO = 5;

HardwareSerial serialHuella(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&serialHuella);

RTC_DS3231 rtc;
Servo cerraduraMG90S;
BluetoothSerial SerialBT;

// Configuración Algorítmica TOTP
uint8_t hmacKey[] = {
  0x53, 0x45, 0x47, 0x55, 0x52, 0x49, 0x44, 0x41,
  0x44, 0x52, 0x45, 0x53, 0x54, 0x52, 0x49, 0x4E
};

TOTP totp = TOTP(hmacKey, 16);

// Máquina de Estados Finita
enum Estados {
  ACCESO_HUELLA,
  ACCESO_TOTP,
  CONCEDIDO,
  PENALIZACION,
  MODO_ADMIN,
  AUTENTICACION_BT,
  PUERTA_ABIERTA_ALERTA
};

Estados estadoActual = ACCESO_HUELLA;

// Variables de Control de Flujo Globales
String entradaTeclado = "";
int contadorErrores = 0;
const int LIMITE_INTENTOS = 3;

unsigned long marcaTiempoBloqueo = 0;
const unsigned long DURACION_BLOQUEO = 30000;

int usuarioAutenticadoID = 0;

unsigned long marcaTiempoInactividad = 0;
const unsigned long TIMEOUT_INACTIVIDAD = 30000;

String tokenGeneradoMFA = "";
bool bluetoothCelularConectado = false;

uint8_t macRemotaDispositivo[] = {0, 0, 0, 0, 0, 0};
uint32_t sppHandleActual = 0;

// Variables para el envío retrasado por Bluetooth
unsigned long marcaTiempoConexionBT = 0;
bool tokenPendientePorEnviar = false;

// ============================================================
// VARIABLES PARA EL CONTROL NO BLOQUEANTE DE LA PUERTA
// ============================================================

enum FasesPuerta {
  PUERTA_ESPERANDO_APERTURA,
  PUERTA_ABIERTA,
  PUERTA_CIERRE_ESTABLE
};

FasesPuerta fasePuerta = PUERTA_ESPERANDO_APERTURA;

bool cicloCerraduraActivo = false;
bool cuentaRegresivaCierreSinApertura = false;
bool alarmaPuertaActiva = false;

unsigned long marcaInicioCicloPuerta = 0;
unsigned long marcaTiempoPuertaAbierta = 0;
unsigned long marcaTiempoCierreEstable = 0;
unsigned long marcaInicioCuentaRegresiva = 0;

// Tiempos solicitados
const unsigned long TIEMPO_ESPERA_APERTURA = 10000;   // 10 s
const unsigned long TIEMPO_CUENTA_REGRESIVA = 5000;   // 5 s
const unsigned long TIEMPO_MAX_ABIERTA = 25000;       // 25 s
const unsigned long TIEMPO_CIERRE_ESTABLE = 3000;     // 3 s

void setup() {

  Serial.begin(115200);

  serialHuella.begin(57600, SERIAL_8N1, 16, 17);

  Wire.begin();

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  pinMode(PIN_SENSOR_MAGNETICO, INPUT_PULLUP);

  cerraduraMG90S.attach(PIN_SERVO);
  cerraduraMG90S.write(0);

  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    Serial.println("Fallo OLED");

  if (!rtc.begin())
    Serial.println("Fallo ZS-042");

  if (!SPIFFS.begin(true))
    Serial.println("Fallo SPIFFS");

  if (!SPIFFS.exists("/clave_maestra.txt")) {

    File fInit = SPIFFS.open("/clave_maestra.txt", FILE_WRITE);

    if (fInit) {
      fInit.println("1234");
      fInit.close();
    }
  }

  finger.begin(57600);

  SerialBT.register_callback(BtCallback);
  SerialBT.begin("ESP32_Control_MFA");

  esp_bt_gap_register_callback(BtAuthCallback);

  esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
  esp_bt_io_cap_t iocamp = ESP_BT_IO_CAP_IN;

  esp_bt_gap_set_security_param(
    param_type,
    &iocamp,
    sizeof(uint8_t)
  );

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  musicaBienvenida();

  mostrarEstadoGrafico(
    "CONTROL MFA",
    "Ponga su huella",
    true,
    true
  );
}

void loop() {

  verificarEnvioRetrasadoTOTP();

  // Supervisión no bloqueante del ciclo de puerta.
  // Debe ejecutarse también en CONCEDIDO para contar los 10 s / 25 s / 3 s.
  if (estadoActual == CONCEDIDO || estadoActual == PUERTA_ABIERTA_ALERTA) {
    verificarSensorPuerta();
  }

  // Buzzer de autenticación Bluetooth
  if (estadoActual == AUTENTICACION_BT &&
      ((millis() / 250) % 2 == 0)) {

    digitalWrite(PIN_BUZZER, HIGH);

  } else if (estadoActual == AUTENTICACION_BT) {

    digitalWrite(PIN_BUZZER, LOW);
  }

  switch (estadoActual) {

    case ACCESO_HUELLA:
      gestionarFaseHuella();
      break;

    case ACCESO_TOTP:
      gestionarFaseTOTP();
      break;

    case CONCEDIDO:
      operarCerradura();
      break;

    case PENALIZACION:
      manejarBloqueoTemporal();
      break;

    case MODO_ADMIN:
      ejecutarEnrolamiento();
      break;

    case PUERTA_ABIERTA_ALERTA:

      // La alarma y su temporización son controladas por
      // verificarSensorPuerta(). No sobrescribir el buzzer aquí.
      break;

    case AUTENTICACION_BT:

      char t = teclado.getKey();

      if (!t)
        break;

      if (t == '#') {

        if (entradaTeclado == "456789") {

          digitalWrite(PIN_BUZZER, LOW);

          esp_bt_gap_ssp_passkey_reply(
            macRemotaDispositivo,
            true,
            456789
          );

          pitidoValidacion();

          estadoActual = ACCESO_HUELLA;

          actualizarPantalla(
            "VINCULADO OK",
            "Acceso Inalambrico"
          );

          delay(2000);

          mostrarEstadoGrafico(
            "CONTROL MFA",
            "Ponga su huella",
            true,
            true
          );

        } else {

          digitalWrite(PIN_BUZZER, LOW);

          esp_bt_gap_ssp_passkey_reply(
            macRemotaDispositivo,
            false,
            0
          );

          mostrarPantallaErrorX();

          estadoActual = ACCESO_HUELLA;

          mostrarEstadoGrafico(
            "CONTROL MFA",
            "Ponga su huella",
            true,
            true
          );
        }

        entradaTeclado = "";

      } else if (t == '*') {

        entradaTeclado = "";
        pitidoClick();

      } else if (entradaTeclado.length() < 6) {

        entradaTeclado += t;

        pitidoClick();

        oled.clearDisplay();
        oled.setCursor(0, 0);
        oled.print("DIGITE PIN BT");

        oled.setCursor(0, 20);
        oled.print(
          "Digitos: " +
          String(entradaTeclado.length())
        );

        oled.display();
      }

      break;
  }
}