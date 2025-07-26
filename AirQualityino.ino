#include <SPI.h>
#include <SD.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <DHT.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define SD_CS 53
File dataFile;

#define DHTPIN 2
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// PMS5003 على Serial1
#define PMS_RX 19
#define PMS_TX 18

// GPS NEO-6M على Serial3
#define GPS_RX 15
#define GPS_TX 14

String filename = "";
unsigned long lastSave = 0;
int recordCount = 1;

// متغيرات الـ PMS
uint16_t pm1_last = 0, pm25_last = 0;
bool have_valid_pms = false;

// متغيرات GPS
bool gpsFixed = false;
String gps_lat = "NO FIX";
String gps_lon = "NO FIX";
String gps_time = "NO FIX";
int gps_sats = 0;

// تابع قراءة بيانات الحساس PMS5003 من Serial1
void readPMS5003() {
  while (Serial1.available() >= 32) {
    if (Serial1.read() == 0x42 && Serial1.read() == 0x4D) {
      uint8_t buffer[30];
      uint16_t sum = 0x42 + 0x4D;
      for (int i = 0; i < 30; i++) {
        while (!Serial1.available());
        buffer[i] = Serial1.read();
        if (i < 28) sum += buffer[i];
      }
      uint16_t checksum = ((uint16_t)buffer[28] << 8) | buffer[29];
      if (sum == checksum) {
        pm1_last = ((uint16_t)buffer[4] << 8) | buffer[5];
        pm25_last = ((uint16_t)buffer[6] << 8) | buffer[7];
        have_valid_pms = true;
      }
    } else {
      Serial1.read();
    }
  }
}

// تابع معالجة بيانات GPS (NMEA)
void readGPS() {
  static String line = "";
  while (Serial3.available()) {
    char c = Serial3.read();
    if (c == '\n') {
      if (line.startsWith("$GPGGA") || line.startsWith("$GPRMC")) {
        parseNMEA(line);
      }
      line = "";
    } else if (c != '\r') {
      line += c;
    }
  }
}

void parseNMEA(String nmea) {
  if (nmea.startsWith("$GPGGA")) {
    String parts[15];
    int field = 0;
    for (int i = 0; i < nmea.length(); i++) {
      if (nmea[i] == ',' || nmea[i] == '*') {
        field++;
      } else if (field < 15) {
        parts[field] += nmea[i];
      }
    }
    gpsFixed = (parts[6].toInt() >= 1);
    if (gpsFixed) {
      gps_lat = convertToDecimal(parts[2], parts[3]);
      gps_lon = convertToDecimal(parts[4], parts[5]);
      gps_sats = parts[7].toInt();
      gps_time = parseTime(parts[1]);
    } else {
      gps_lat = gps_lon = gps_time = "NO FIX";
      gps_sats = 0;
    }
  }
}

// ✅ التصحيح النهائي هنا:
String convertToDecimal(String raw, String dir) {
  if (raw.length() < 6) return "NO FIX";
  // إذا كان عندنا 3 أرقام قبل النقطة → خط الطول، إذا رقمين → خط العرض
  int degreeLength = (raw.indexOf('.') > 4) ? 3 : 2;
  float degrees = raw.substring(0, degreeLength).toFloat();
  float minutes = raw.substring(degreeLength).toFloat();
  float decimal = degrees + (minutes / 60.0);
  if (dir == "S" || dir == "W") decimal *= -1.0;
  char buf[15];
  dtostrf(decimal, 2, 6, buf);
  return String(buf);
}

String parseTime(String raw) {
  if (raw.length() < 6) return "NO FIX";
  String hh = raw.substring(0, 2);
  String mm = raw.substring(2, 4);
  String ss = raw.substring(4, 6);
  return hh + ":" + mm + ":" + ss;
}

void setup() {
  Serial.begin(9600);       // للحاسب
  Serial1.begin(9600);      // PMS5003
  Serial3.begin(9600);      // GPS
  dht.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) while (true);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (!SD.begin(SD_CS)) {
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("SD Card Error!");
    display.display();
    while (true);
  }

  // اسم ملف فريد باسم عشوائي
  randomSeed(analogRead(0));
  filename = "LOG" + String(random(1000,9999)) + ".csv";
  dataFile = SD.open(filename, FILE_WRITE);
  if (dataFile) {
    dataFile.println("No.,PM1.0 (µg/m3),PM2.5 (µg/m3),Temp (°C),Humidity (%),Lat,Lon,Sats,Time");
    dataFile.close();
  } else {
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("SD Write Err!");
    display.display();
    while (true);
  }

  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(10, 20);
  display.println("READY");
  display.display();
  delay(1000);
}

void loop() {
  readPMS5003();
  readGPS();

  if (millis() - lastSave >= 5000) {
    lastSave = millis();

    float temp = dht.readTemperature();
    float hum = dht.readHumidity();

    // OLED
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0, 0);
    if (have_valid_pms) {
      display.print("PM1:"); display.println(pm1_last);
      display.print("P2.5:"); display.println(pm25_last);
    } else {
      display.println("NO DATA");
    }
    display.setTextSize(1);
    display.setCursor(0, 40);
    display.print("T:"); display.print(temp, 1); display.print("C ");
    display.print("H:"); display.print(hum, 0); display.print("%");
    display.setCursor(0, 55);
    display.print("GPS:");
    display.print(gpsFixed ? "OK" : "NO FIX");
    display.display();

    // طباعة على الحاسب (Serial Monitor)
    Serial.print("No.: "); Serial.println(recordCount);
    Serial.print("PM1.0: "); Serial.println(pm1_last);
    Serial.print("PM2.5: "); Serial.println(pm25_last);
    Serial.print("Temp: "); Serial.print(temp, 1); Serial.print("C  ");
    Serial.print("Hum: "); Serial.print(hum, 0); Serial.println("%");
    Serial.print("Lat: "); Serial.println(gps_lat);
    Serial.print("Lon: "); Serial.println(gps_lon);
    Serial.print("Sats: "); Serial.println(gps_sats);
    Serial.print("Time: "); Serial.println(gps_time);
    Serial.println("-----------------------------");

    // حفظ في SD
    dataFile = SD.open(filename, FILE_WRITE);
    if (dataFile) {
      dataFile.print(recordCount); dataFile.print(",");
      dataFile.print(have_valid_pms ? pm1_last : 0); dataFile.print(",");
      dataFile.print(have_valid_pms ? pm25_last : 0); dataFile.print(",");
      dataFile.print(!isnan(temp) ? temp : 0); dataFile.print(",");
      dataFile.print(!isnan(hum) ? hum : 0); dataFile.print(",");
      dataFile.print(gps_lat); dataFile.print(",");
      dataFile.print(gps_lon); dataFile.print(",");
      dataFile.print(gps_sats); dataFile.print(",");
      dataFile.println(gps_time);
      dataFile.close();
      recordCount++;
    }
  }
}
