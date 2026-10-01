#define ENABLE_DATABASE

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <DHT.h>

// Wi-Fi
#define WIFI_SSID "your_Wifi_name"
#define WIFI_PASSWORD "Wifi_Password"

// Firebase
#define DATABASE_URL "https://smarthomesecurity-11569-default-rtdb.firebaseio.com/"

// Sensors
#define DHT_PIN 4
#define DHT_TYPE DHT11

#define IR_PIN 27
#define SOUND_PIN 26
#define MQ135_DO 19

// Outputs
#define LED_PIN_1 2
#define LED_PIN_2 23

#define BUZZER_PIN_1 18
#define BUZZER_PIN_2 22

const float TEMP_LIMIT = 35.0;

DHT dht(DHT_PIN, DHT_TYPE);

WiFiClientSecure sslClient;
using AsyncClient = AsyncClientClass;

AsyncClient aClient(sslClient);
FirebaseApp app;
RealtimeDatabase Database;
NoAuth noAuth;

bool systemPower = true;

bool oldIR = false;
bool oldSound = false;
bool oldGas = false;
bool oldHighTemp = false;

bool alarmActive = false;
bool alarmOutputState = false;

unsigned long lastSensorRead = 0;
unsigned long alarmStart = 0;

const unsigned long SENSOR_DELAY = 2000;
const unsigned long ALARM_TIME = 20000;

// 3 seconds ON + 1 second OFF
const unsigned long ALARM_ON_TIME = 3000;
const unsigned long ALARM_OFF_TIME = 1000;


// --------------------------------------------------
// SAVE ALERT TO FIREBASE
// --------------------------------------------------

void saveAlert(String type) {

  if (!app.ready() || !systemPower) {
    return;
  }

  String alertId = String(millis());
  String path = "/alerts/" + alertId;

  Database.set<String>(
    aClient,
    path + "/type",
    type
  );

  Database.set<String>(
    aClient,
    path + "/time",
    String(millis())
  );

  Serial.print("Alert saved: ");
  Serial.println(type);
}


// --------------------------------------------------
// ALARM OUTPUTS ON
// --------------------------------------------------

void alarmOutputsOn() {

  digitalWrite(LED_PIN_1, HIGH);
  digitalWrite(LED_PIN_2, HIGH);

  digitalWrite(BUZZER_PIN_1, HIGH);
  digitalWrite(BUZZER_PIN_2, HIGH);
}


// --------------------------------------------------
// ALARM OUTPUTS OFF
// --------------------------------------------------

void alarmOutputsOff() {

  digitalWrite(LED_PIN_1, LOW);
  digitalWrite(LED_PIN_2, LOW);

  digitalWrite(BUZZER_PIN_1, LOW);
  digitalWrite(BUZZER_PIN_2, LOW);
}


// --------------------------------------------------
// UPDATE ALARM
//
// 3 sec ON
// 1 sec OFF
// repeated for 20 sec
// --------------------------------------------------

void updateAlarm() {

  if (!systemPower || !alarmActive) {

    alarmOutputsOff();
    alarmOutputState = false;

    return;
  }

  unsigned long elapsed = millis() - alarmStart;

  // Stop after exactly 20 seconds
  if (elapsed >= ALARM_TIME) {

    alarmActive = false;
    alarmOutputsOff();
    alarmOutputState = false;

    Serial.println("Alarm finished");

    return;
  }

  // 4-second repeating cycle:
  // first 3 sec ON
  // last 1 sec OFF
  unsigned long cycleTime = elapsed % 4000;

  if (cycleTime < ALARM_ON_TIME) {

    if (!alarmOutputState) {

      alarmOutputState = true;

      alarmOutputsOn();

      Serial.println("Alarm ON");
    }

  } else {

    if (alarmOutputState) {

      alarmOutputState = false;

      alarmOutputsOff();

      Serial.println("Alarm OFF");
    }
  }
}


// --------------------------------------------------
// SETUP
// --------------------------------------------------

void setup() {

  Serial.begin(115200);

  // Sensors
  pinMode(IR_PIN, INPUT);
  pinMode(SOUND_PIN, INPUT);
  pinMode(MQ135_DO, INPUT);

  // LEDs
  pinMode(LED_PIN_1, OUTPUT);
  pinMode(LED_PIN_2, OUTPUT);

  // Buzzers
  pinMode(BUZZER_PIN_1, OUTPUT);
  pinMode(BUZZER_PIN_2, OUTPUT);

  // Everything OFF initially
  alarmOutputsOff();

  dht.begin();

  Serial.println();
  Serial.println("Connecting to Wi-Fi...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {

    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Wi-Fi connected");

  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  sslClient.setInsecure();

  initializeApp(
    aClient,
    app,
    getAuth(noAuth)
  );

  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);

  Serial.println("Firebase connected");
}


// --------------------------------------------------
// MAIN LOOP
// --------------------------------------------------

void loop() {

  app.loop();


  // ------------------------------------------------
  // GET POWER STATE
  // ------------------------------------------------

  if (app.ready()) {

    bool power = Database.get<bool>(
      aClient,
      "/system/power"
    );

    systemPower = power;
  }


  // Alarm must update continuously
  updateAlarm();


  // ------------------------------------------------
  // SENSOR READING
  // ------------------------------------------------

  if (millis() - lastSensorRead < SENSOR_DELAY) {

    delay(1);
    return;
  }

  lastSensorRead = millis();


  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  bool irDetected =
      digitalRead(IR_PIN) == LOW;

  bool soundDetected =
      digitalRead(SOUND_PIN) == LOW;

  bool gasDetected =
      digitalRead(MQ135_DO) == LOW;

  bool highTemp =
      !isnan(temperature) &&
      temperature >= TEMP_LIMIT;


  // ------------------------------------------------
  // SERIAL MONITOR
  // ------------------------------------------------

  Serial.println();
  Serial.println("========== VASTURAKSHA ==========");

  Serial.print("System: ");
  Serial.println(
    systemPower ? "ON" : "OFF"
  );

  Serial.print("Temperature: ");

  if (!isnan(temperature)) {
    Serial.print(temperature);
    Serial.println(" C");
  } else {
    Serial.println("ERROR");
  }

  Serial.print("Humidity: ");

  if (!isnan(humidity)) {
    Serial.print(humidity);
    Serial.println(" %");
  } else {
    Serial.println("ERROR");
  }

  Serial.print("IR: ");
  Serial.println(
    irDetected ? "INTRUSION!" : "CLEAR"
  );

  Serial.print("Sound: ");
  Serial.println(
    soundDetected ? "DETECTED" : "CLEAR"
  );

  Serial.print("MQ-135: ");
  Serial.println(
    gasDetected ? "GAS DETECTED" : "NORMAL"
  );

  Serial.println("=================================");


  // ------------------------------------------------
  // SEND DATA TO FIREBASE
  // ------------------------------------------------

  if (app.ready()) {

    if (!isnan(temperature)) {

      Database.set<float>(
        aClient,
        "/sensors/temperature",
        temperature
      );
    }

    if (!isnan(humidity)) {

      Database.set<float>(
        aClient,
        "/sensors/humidity",
        humidity
      );
    }

    Database.set<bool>(
      aClient,
      "/sensors/ir",
      irDetected
    );

    Database.set<bool>(
      aClient,
      "/sensors/sound",
      soundDetected
    );

    Database.set<bool>(
      aClient,
      "/sensors/gas",
      gasDetected
    );

    Database.set<bool>(
      aClient,
      "/system/status",
      systemPower
    );
  }


  // ------------------------------------------------
  // CHECK ALERT
  // ------------------------------------------------

  bool alert =
      irDetected ||
      soundDetected ||
      gasDetected ||
      highTemp;


  // ------------------------------------------------
  // START ALARM
  // ------------------------------------------------

  if (systemPower && alert && !alarmActive) {

    alarmActive = true;

    alarmStart = millis();

    alarmOutputState = true;

    alarmOutputsOn();

    Serial.println("ALARM STARTED");
  }


  // ------------------------------------------------
  // INTRUSION EVENT
  // ------------------------------------------------

  if (irDetected && !oldIR) {

    Serial.println("New intrusion event");

    saveAlert("Intrusion");
  }

  oldIR = irDetected;


  // ------------------------------------------------
  // SOUND EVENT
  // ------------------------------------------------

  if (soundDetected && !oldSound) {

    Serial.println("New sound event");

    saveAlert("Sound");
  }

  oldSound = soundDetected;


  // ------------------------------------------------
  // GAS EVENT
  // ------------------------------------------------

  if (gasDetected && !oldGas) {

    Serial.println("New gas event");

    saveAlert("Gas");
  }

  oldGas = gasDetected;


  // ------------------------------------------------
  // HIGH TEMPERATURE EVENT
  // ------------------------------------------------

  if (highTemp && !oldHighTemp) {

    Serial.println("High temperature alert");

    saveAlert("High Temperature");
  }

  oldHighTemp = highTemp;


  updateAlarm();

  delay(5);
}
