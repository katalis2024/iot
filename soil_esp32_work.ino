#include <Arduino.h>
#include <ESP32_Supabase.h> //install
#include <SPI.h>
#include <Wire.h>
#include <SoftwareSerial.h>
#include <LiquidCrystal_I2C.h> //install zip
#include <ArduinoJson.h> //install
#include "RTClib.h" //install
#include "WiFi.h"
#include <HTTPClient.h>

#define ESP32SDA 21
#define ESP32SCL 22

#define RS485RX 16
#define RS485TX 17

#define sensorFrameSize 19
#define sensorWaitingTime 1000
#define sensorID 0x01
#define sensorFunction 0x03
#define sensorByteResponse 0x0E

#define RELAY 25
#define RELAY1 26
#define RELAY2 27

#define BUZZER 19

EspSoftwareSerial::UART sensor; //install

unsigned char byteRequest[8] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x04, 0x44, 0x09 };

unsigned char byteResponse[19] = {};

float moisture, temperature, ph, max_temperature;
bool state;

// set LCD address, number of columns and rows
int lcdColumns = 20;
int lcdRows = 4;

LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);

// set Real time clock
RTC_DS3231 rtc;

char daysOfTheWeek[7][12] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

int time_span_day = 14;
int id_work_orfez = 0;
int id_work_manager;
TimeSpan time_left;
DateTime future;


// setup database
Supabase db;

// Put your supabase URL and Anon key here...
String supabase_url = "https://vyxvjuzabusxpnkhobxn.supabase.co";
String anon_key = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InZ5eHZqdXphYnVzeHBua2hvYnhuIiwicm9sZSI6ImFub24iLCJpYXQiOjE3MTg1MTY2OTcsImV4cCI6MjAzNDA5MjY5N30.GivX-ZmS9ZNgbzHHcVSnKeIQhk9YOjA8JhmNEuSpPD8";

// put your WiFi credentials (SSID and Password) here
const char *ssid = "ppko2024";
const char *psswd = "11111111";

// User Credentials, not Supabase Account
// OPTIONAL (only use this if you activate RLS) !!
const String email = "";
const String password = "";

// Put your JSON that you want to insert rows
// You can also use library like ArduinoJson generate this
String JSON = "{\"temperature\":" + String(temperature) + ",\"humidity\":" + String(moisture) + ",\"pH\":" + String(ph) + "}";

bool upsert = false;
int sendinginterval = 5;

void setup() {

  rtc.begin();
  Serial.begin(115200);
  sensor.begin(9600, SWSERIAL_8N1, RS485RX, RS485TX, false);

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, let's set the time!");
    // When time needs to be set on a new device, or after a power loss, the
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  pinMode(RELAY, OUTPUT);
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  digitalWrite(RELAY, LOW);
  digitalWrite(RELAY1, HIGH);
  digitalWrite(RELAY2, HIGH);
  digitalWrite(BUZZER, LOW);
  state = false;

  // setup LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print(" PPKO EneRC 2024!");
  lcd.setCursor(7, 1);
  lcd.print(":)");
  delay(2000);
  lcd.clear();

  delay(1000);
  Serial.println();
  Serial.println("Agriculture Kit Sensor Ready");

  // Connecting to Wi-Fi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, psswd);
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.print(".");
    lcd.setCursor(0, 0);
    lcd.print(" Wifi Connecting!");
    lcd.setCursor(4, 1);
    lcd.print(ssid);
    delay(2000);
    lcd.clear();
  }
  Serial.println("Connected!");

  // // Logging in with your account you made in Supabase
  // db.login_email(email, password);
}


void loop() {
  readHumiturePH();
  if (rtc.lostPower()) {
    // When time needs to be set on a new device, or after a power loss, the
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  Serial.println("Moisture: " + (String)moisture + " %");
  Serial.println("Temperature: " + (String)temperature + " °C");
  Serial.println("pH: " + (String)ph);

  if ((WiFi.status() == WL_CONNECTED)) {
    // Logger to Database
    databaseInsert();
    databaseReadIDWM();
    if (id_work_manager > id_work_orfez) {
      DateTime now = rtc.now();
      // RESET Time Span Day
      databaseReadDayTS();
      // calculate a date which is 7 days, 12 hours, 30 minutes, 6 seconds into the future
      future = now + TimeSpan(time_span_day, 0, 1, 30);
      Serial.print(" now + TimeSpan ");
      Serial.print(future.year(), DEC);
      Serial.print('/');
      Serial.print(future.month(), DEC);
      Serial.print('/');
      Serial.print(future.day(), DEC);
      Serial.print(' ');
      Serial.print(future.hour(), DEC);
      Serial.print(':');
      Serial.print(future.minute(), DEC);
      Serial.print(':');
      Serial.print(future.second(), DEC);
      Serial.println();
      id_work_orfez = id_work_manager;
    }
  }

  displayDate();
  displaySoil(moisture, temperature, ph);


  DateTime now = rtc.now();
  // Reset Time Left
  time_left = future - now;
  Serial.print("Time Left: ");
  Serial.println(time_left.totalseconds(), DEC);
  // if time left is run out, valve off
  if (time_left.totalseconds() >= 0) {
    Serial.print("GREATER TIME LEFT, VALVE OK!");
    // set Valve Range temperature on 25-40 C
    if (temperature > 40.0) {
      // Valve Open
      Serial.println("Valve Open!");
      digitalWrite(RELAY2, LOW);
      digitalWrite(RELAY1, HIGH);
      delay(3000);
      // digitalWrite(RELAY, LOW);
      // state = true;
    } else if (temperature < 30.0) {
      //Valve Close
      Serial.println("Valve Close!");
      digitalWrite(RELAY1, LOW);
      digitalWrite(RELAY2, HIGH);
      delay(3000);
      // state = false;
    } else {
      //Normal Range
    }
  } else {
    Serial.println("Waktu Habis");
    digitalWrite(BUZZER, HIGH);
    delay(3000);
    digitalWrite(BUZZER, LOW);
  }
}

void databaseReadIDWM() {

  // Beginning Supabase Connection
  // db.begin(supabase_url, anon_key);
  // // Select query with filter and order, limiting the result is mandatory here
  // String payload = db.from("work_manager").select("id").order("id", "asc", true).limit(2).doSelect();
  // Serial.println("payload ReadIDWM: " + payload);

  // // Parse JSON
  // const size_t capacity = JSON_ARRAY_SIZE(1) + JSON_OBJECT_SIZE(1) + 20;
  // StaticJsonDocument<capacity> doc;

  // DeserializationError error = deserializeJson(doc, payload);

  // // Check for errors in deserialization
  // if (error) {
  //   Serial.print(F("deserializeJson() failed: "));
  //   Serial.println(error.f_str());
  //   return;
  // }

  // Beginning Supabase Connection
  HTTPClient http;
  String url = String(supabase_url) + "/rest/v1/work_manager?select=id&order=id.desc&limit=1";
  http.begin(url);
  http.addHeader("apikey", anon_key);

  // Send GET request
  int httpCode = http.GET();

  if (httpCode > 0) {  // Check for the returning code
    String payload = http.getString();
    Serial.println("IDWM: " + payload);

    // Parse JSON
    const size_t capacity = JSON_ARRAY_SIZE(1) + JSON_OBJECT_SIZE(1) + 20;
    StaticJsonDocument<capacity> doc;

    DeserializationError error = deserializeJson(doc, payload);

    // Check for errors in deserialization
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }

    // Extract the integer value associated with "id"
    id_work_manager = doc[0]["id"];

    // Print the extracted value
    Serial.print("The value of id is: ");
    Serial.println(id_work_manager);
  } else {
    Serial.println("Error on HTTP request");
  }

  http.end();
}

void databaseReadDayTS() {

  // Beginning Supabase Connection
  db.begin(supabase_url, anon_key);
  // Select query with filter and order, limiting the result is mandatory here
  String payload_time_span_day = db.from("work_manager").select("time_span_day").eq("id", String(id_work_manager)).order("id", "asc", false).limit(1).doSelect();


  Serial.println("payload TS Read: " + payload_time_span_day);

  // Parse JSON
  const size_t capacity = JSON_ARRAY_SIZE(1) + JSON_OBJECT_SIZE(1) + 20;
  StaticJsonDocument<capacity> doc;

  DeserializationError error = deserializeJson(doc, payload_time_span_day);

  // Check for errors in deserialization
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  // Extract the integer value associated with "id"
  time_span_day = doc[0]["time_span_day"];
  Serial.println("Value read time_span_day: " + (String)time_span_day);

  // Reset Your Query before doing everything else
  db.urlQuery_reset();
}

void databaseInsert() {

  // Beginning Supabase Connection
  db.begin(supabase_url, anon_key);
  // // Select query with filter and order, limiting the result is mandatory here
  // String read = db.from("table").select("*").eq("column", "value").order("column", "asc", true).limit(1).doSelect();
  // Serial.println(read);

  // // Reset Your Query before doing everything else
  // db.urlQuery_reset();

  // // Join operation with other table that connected via PK or FK
  // read = db.from("table").select("*, other_table(other_table_column1, other_table_column2, another_table(*))").order("column", "asc", true).limit(1).doSelect();
  // Serial.println(read);
  // db.urlQuery_reset();

  // Insert operation
  int HTTPresponseCode = db.insert("maintable", "{\"temperature\":" + String(temperature) + ",\"humidity\":" + String(moisture) + ",\"pH\":" + String(ph) + "}", upsert);
  Serial.println(HTTPresponseCode);
  db.urlQuery_reset();

  // // Update Operation
  // int code = db.update("table").eq("column", "value").doUpdate(JSON);
  // Serial.println(code);
  // db.urlQuery_reset();
}

void controlValve() {
  if (state = false) {
    Serial.println("Valve Open!");
    digitalWrite(RELAY, HIGH);
    delay(5000);
    state = true;
  } else {
    Serial.println("Valve Close!");
    digitalWrite(RELAY, LOW);
    delay(5000);
    state = false;
  }
}

void readHumiturePH() {
  sensor.write(byteRequest, 8);

  unsigned long resptime = millis();
  while ((sensor.available() < sensorFrameSize) && ((millis() - resptime) < sensorWaitingTime)) {
    delay(1);
  }

  while (sensor.available()) {
    for (int n = 0; n < sensorFrameSize; n++) {
      byteResponse[n] = sensor.read();
    }

    if (byteResponse[0] != sensorID && byteResponse[1] != sensorFunction && byteResponse[2] != sensorByteResponse) {
      return;
    }
  }

  Serial.println();
  Serial.println("===== SOIL PARAMETERS =====");
  Serial.print("Byte Response: ");

  String responseString;
  for (int j = 0; j < 19; j++) {
    responseString += byteResponse[j] < 0x10 ? " 0" : " ";
    responseString += String(byteResponse[j], HEX);
    responseString.toUpperCase();
  }
  Serial.println(responseString);

  moisture = sensorValue((int)byteResponse[3], (int)byteResponse[4]) * 0.1;
  temperature = sensorValue((int)byteResponse[5], (int)byteResponse[6]) * 0.1;
  ph = sensorValue((int)byteResponse[9], (int)byteResponse[10]) * 0.1;
}

void displaySoil(float h, float t, float p) {
  lcd.setCursor(0, 1);
  lcd.print(F("T:"));
  lcd.print(t, 1);
  lcd.print(F(" \xDF"
              "C"));

  lcd.setCursor(0, 2);
  lcd.print(F("H:"));
  lcd.print(h, 1);
  lcd.print(F(" %"));

  lcd.setCursor(0, 3);
  lcd.print(F("pH:"));
  lcd.print(p, 1);

  delay(3000);
}

void displayDate() {
  //lcd.setCursor(column,row);
  // Serial.print(time_left.days(), DEC);

  // lcd.setCursor(6, 0);
  //lcd.print(daysOfTheWeek[now.dayOfTheWeek()]);
  lcd.setCursor(0, 0);
  lcd.print("Day:");
  lcd.setCursor(4, 0);
  lcd.print(time_left.days(), DEC);

  // lcd.setCursor(8, 0);
  // lcd.print(":");
  lcd.setCursor(7, 0);
  lcd.print("Time:");
  if (time_left.hours() <= 9) {
    lcd.print("0");
    lcd.setCursor(13, 0);
    lcd.print(time_left.hours(), DEC);
  } else {
    lcd.print(time_left.hours(), DEC);
  }
  lcd.setCursor(14, 0);
  lcd.print(":");
  lcd.setCursor(15, 0);
  lcd.print(time_left.minutes(), DEC);
  lcd.setCursor(17, 0);
  lcd.print(":");
  lcd.setCursor(18, 0);
  lcd.print(time_left.seconds(), DEC);
}

int sensorValue(int x, int y) {
  int t = 0;
  t = x * 256;
  t = t + y;

  return t;
}