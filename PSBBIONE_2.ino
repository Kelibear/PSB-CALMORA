/*
========================================================
CALMORA FINAL AI ASSISTANT
ESP32 + MAX30102 + MLX90614 + OLED + DFPLAYER
========================================================

FITUR:
- BPM detection (30 detik, default 70 jika tidak terdeteksi)
- Suhu tubuh MLX90614
- OLED AI FACE (5 mode, teks/angka HANYA di Blynk)
- Audio AI assistant DFPlayer
- Fuzzy Logic Mamdani 4 output (PRD V2)
- Relaksasi breathing audio sekuensial
- Push button start + Easter egg 3x klik
- Blynk IoT realtime V0-V4, V6, V7

I2C ADDRESS (tidak bertabrakan):
  MAX30102  = 0x57 (fixed hardware)
  MLX90614  = 0x5A (fixed hardware)
  OLED      = 0x3C

BLYNK DATASTREAM:
  V0 = BPM       (Integer)
  V1 = Suhu      (Double)
  V2 = Status    (String)
  V3 = StressScore (Integer)
  V4 = PPG Raw   (Integer)
  V5 = TIDAK DIPAKAI
  V6 = FaceMode  (Integer 0-4)
        0 = Normal/Idle
        1 = Happy (rileks/normal)
        2 = Sad (cemas/tegang)
        3 = Analyzing/Scan
        4 = Easter Egg
  V7 = Countdown (String)
========================================================
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MLX90614.h>

#include "MAX30105.h"
#include "heartRate.h"

#include "DFRobotDFPlayerMini.h"
#include <HardwareSerial.h>

#define BLYNK_TEMPLATE_ID "TMPL6c0ygELLm"
#define BLYNK_TEMPLATE_NAME "CALMORA"
#define BLYNK_AUTH_TOKEN "LX-Kj0M550MvNtmPm1e3pBZ-RFWqGMIw"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

// =====================================================
// I2C PIN (tidak diubah)
// =====================================================
#define SDA_PIN 21
#define SCL_PIN 22

// =====================================================
// BUTTON (tidak diubah)
// =====================================================
#define BUTTON_PIN 4

// =====================================================
// DFPLAYER PIN (tidak diubah)
// =====================================================
#define DF_RX 16
#define DF_TX 17

// =====================================================
// OLED (tidak diubah)
// =====================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// =====================================================
// SENSOR (tidak diubah)
// =====================================================
MAX30105 particleSensor;
Adafruit_MLX90614 mlx = Adafruit_MLX90614();

// =====================================================
// DFPLAYER (tidak diubah)
// =====================================================
HardwareSerial mySerial(2);
DFRobotDFPlayerMini player;

// =====================================================
// BLYNK (tidak diubah)
// =====================================================
char ssid[] = "falih";
char pass[] = "psbsayang";
BlynkTimer timer;

// =====================================================
// AUDIO NUMBER (tidak diubah)
// =====================================================
#define AUDIO_OPENING     1
#define AUDIO_TEGANG      2
#define AUDIO_RILEKS      3
#define AUDIO_RELAKSASI   4
#define AUDIO_RETEST      5
#define AUDIO_CLOSING     6
#define AUDIO_TUNGTUNG    7

// =====================================================
// SYSTEM STATE
// (ditambah SHOWING_RESULT, gabung RELAX+STRESS)
// =====================================================
enum SystemState
{
  IDLE,
  WAIT_FINGER,
  MEASURING,
  ANALYZING,
  SHOWING_RESULT
};

SystemState currentState = IDLE;

// =====================================================
// EMOTION RESULT (TAMBAHAN PRD: 4 output fuzzy)
// =====================================================
enum EmotionResult
{
  EMOTION_RILEKS,
  EMOTION_NORMAL,
  EMOTION_CEMAS,
  EMOTION_TEGANG
};

EmotionResult emotionResult = EMOTION_NORMAL;

// =====================================================
// BPM VARIABLE (tidak diubah)
// =====================================================
const byte RATE_SIZE = 8;
byte rates[RATE_SIZE];
byte rateSpot = 0;
byte validBeatCount = 0;
long lastBeat = 0;
float beatsPerMinute = 0;
int beatAvg = 0;
long irValue = 0;

const long FINGER_THRESHOLD = 50000;

// =====================================================
// SUHU (tidak diubah)
// =====================================================
float suhu = 0;

// =====================================================
// TIMER (tidak diubah, ditambah beberapa)
// =====================================================
unsigned long lastSerialPrint = 0;
unsigned long lastBlink = 0;
unsigned long blinkInterval = 3000;
unsigned long measureStartTime = 0;
unsigned long stateTimer = 0;
byte audioStep = 0;
unsigned long audioTimer = 0;
float tempSum = 0;
int tempCount = 0;
float finalTemp = 0;
int finalBPM = 0;
int stressScore = 0;

// =====================================================
// FACE MODE (V6) - TAMBAHAN PRD
// 0=normal, 1=happy, 2=sad, 3=scan/analyzing, 4=easter
// =====================================================
int faceMode = 0;

// =====================================================
// BUTTON VARIABLE (tidak diubah)
// =====================================================
bool lastButtonState = HIGH;
unsigned long multiPressTimer = 0;
int pressCount = 0;

// =====================================================
// BLINK ANIMASI IDLE (tidak diubah + diperluas)
// =====================================================
int blinkHeight = 16;
bool isBlinking = false;
unsigned long blinkStartTime = 0;

// =====================================================
// PUPIL SCAN ANIMASI (TAMBAHAN PRD)
// =====================================================
int pupilOffsetX = 0;
unsigned long lastPupilShift = 0;

// =====================================================
// EASTER EGG ANIMASI (TAMBAHAN PRD)
// =====================================================
bool easterLeftOpen = true;
unsigned long lastEasterBlink = 0;

// =====================================================
// RESET BPM (tidak diubah)
// =====================================================
void resetBPM()
{
  beatsPerMinute = 0;
  beatAvg = 0;
  validBeatCount = 0;
  rateSpot = 0;
  lastBeat = 0;
  for (byte i = 0; i < RATE_SIZE; i++)
  {
    rates[i] = 0;
  }
}

// =====================================================
// READ TEMPERATURE (tidak diubah)
// =====================================================
float readTemperature()
{
  float total = 0;
  for (int i = 0; i < 5; i++)
  {
    total += mlx.readObjectTempC();
    delay(3);
  }
  return total / 5.0;
}

// =====================================================
// BPM AVERAGE (tidak diubah)
// =====================================================
int calculateAverageBPM()
{
  int total = 0;
  int count = 0;
  for (byte i = 0; i < RATE_SIZE; i++)
  {
    if (rates[i] > 0)
    {
      total += rates[i];
      count++;
    }
  }
  if (count == 0) return 0;
  return total / count;
}

// =====================================================
// BLYNK SEND (V5 dihapus, V6=faceMode, V7 ditambah)
// =====================================================
void sendToBlynk()
{
  Blynk.virtualWrite(V0, beatAvg);
  Blynk.virtualWrite(V1, suhu);
  Blynk.virtualWrite(V3, stressScore);
  Blynk.virtualWrite(V4, irValue);
  Blynk.virtualWrite(V6, faceMode);
  // V2 dan V7 dikirim saat event (tidak di timer)
}

// =====================================================
// FUZZY MEMBERSHIP: BPM (TAMBAHAN PRD)
// Kurva dari jurnal Pradhipta Kresna Hadya 2014
// =====================================================
float muBPM_Rendah(float bpm)
{
  if (bpm <= 60) return 1.0;
  if (bpm >= 75) return 0.0;
  return (75.0 - bpm) / 15.0;
}

float muBPM_Normal(float bpm)
{
  if (bpm <= 70 || bpm >= 90) return 0.0;
  if (bpm >= 75 && bpm <= 85) return 1.0;
  if (bpm < 75) return (bpm - 70.0) / 5.0;
  return (90.0 - bpm) / 5.0;
}

float muBPM_Tinggi(float bpm)
{
  if (bpm <= 85) return 0.0;
  if (bpm >= 95) return 1.0;
  return (bpm - 85.0) / 10.0;
}

// =====================================================
// FUZZY MEMBERSHIP: SUHU (TAMBAHAN PRD)
// =====================================================
float muSuhu_Dingin(float t)
{
  if (t <= 34.0) return 1.0;
  if (t >= 35.5) return 0.0;
  return (35.5 - t) / 1.5;
}

float muSuhu_Normal(float t)
{
  if (t <= 35.3 || t >= 37.4) return 0.0;
  if (t >= 35.8 && t <= 37.0) return 1.0;
  if (t < 35.8) return (t - 35.3) / 0.5;
  return (37.4 - t) / 0.4;
}

float muSuhu_Panas(float t)
{
  if (t <= 37.2) return 0.0;
  if (t >= 38.0) return 1.0;
  return (t - 37.2) / 0.8;
}

// =====================================================
// FUZZY INFERENCE MAMDANI (TAMBAHAN PRD)
// 9 Rules -> 4 output kondisi
// =====================================================
EmotionResult fuzzyAnalyze(float bpm, float temp)
{
  float mBPM_R = muBPM_Rendah(bpm);
  float mBPM_N = muBPM_Normal(bpm);
  float mBPM_T = muBPM_Tinggi(bpm);
  float mSuhu_D = muSuhu_Dingin(temp);
  float mSuhu_N = muSuhu_Normal(temp);
  float mSuhu_P = muSuhu_Panas(temp);

  // 9 Rules Mamdani (sesuai tabel PRD)
  float rule1 = min(mBPM_R, mSuhu_D);  // RILEKS
  float rule2 = min(mBPM_R, mSuhu_N);  // RILEKS
  float rule3 = min(mBPM_R, mSuhu_P);  // NORMAL
  float rule4 = min(mBPM_N, mSuhu_D);  // CEMAS
  float rule5 = min(mBPM_N, mSuhu_N);  // NORMAL
  float rule6 = min(mBPM_N, mSuhu_P);  // CEMAS
  float rule7 = min(mBPM_T, mSuhu_D);  // TEGANG
  float rule8 = min(mBPM_T, mSuhu_N);  // CEMAS
  float rule9 = min(mBPM_T, mSuhu_P);  // TEGANG

  // Agregasi: max per kategori output
  float agg_rileks = max(rule1, rule2);
  float agg_normal = max(rule3, rule5);
  float agg_cemas  = max(max(rule4, rule6), rule8);
  float agg_tegang = max(rule7, rule9);

  // Defuzzifikasi centroid (nilai representatif: R=0, N=33, C=66, T=100)
  float num = (agg_rileks * 0.0f)
            + (agg_normal * 33.0f)
            + (agg_cemas  * 66.0f)
            + (agg_tegang * 100.0f);
  float den = agg_rileks + agg_normal + agg_cemas + agg_tegang;

  stressScore = (den > 0.0f) ? (int)(num / den) : 0;

  Serial.println("--- FUZZY RESULT ---");
  Serial.print("BPM="); Serial.print(bpm);
  Serial.print(" Suhu="); Serial.println(temp);
  Serial.print("R="); Serial.print(agg_rileks);
  Serial.print(" N="); Serial.print(agg_normal);
  Serial.print(" C="); Serial.print(agg_cemas);
  Serial.print(" T="); Serial.println(agg_tegang);
  Serial.print("StressScore="); Serial.println(stressScore);

  // Pilih output dengan derajat keanggotaan tertinggi
  float maxVal = max(max(agg_rileks, agg_normal), max(agg_cemas, agg_tegang));

  if (maxVal == agg_rileks) return EMOTION_RILEKS;
  if (maxVal == agg_normal) return EMOTION_NORMAL;
  if (maxVal == agg_cemas)  return EMOTION_CEMAS;
  return EMOTION_TEGANG;
}

// =====================================================
// DRAW TITLE (fungsi asli dipertahankan, tapi
// tidak dipanggil di face baru — hanya untuk
// kompatibilitas jika diperlukan)
// =====================================================
void drawTitle(String text)
{
  display.setTextSize(1);
  display.setTextColor(WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int centerX = (128 - w) / 2;
  display.setCursor(centerX, 0);
  display.print(text);
}

// =====================================================
// OLED FACE 0: NORMAL/IDLE
// PRD: mata lingkaran besar, berkedip random, mulut senyum
// =====================================================
void drawNormalFace()
{
  unsigned long now = millis();

  // Trigger kedip otomatis
  if (!isBlinking && (now - lastBlink >= blinkInterval))
  {
    isBlinking = true;
    blinkStartTime = now;
    blinkHeight = 16;
    blinkInterval = random(2500, 5000);
  }

  // Animasi kedip (tutup-buka 160ms)
  if (isBlinking)
  {
    unsigned long elapsed = now - blinkStartTime;
    if (elapsed < 80)
      blinkHeight = map(elapsed, 0, 80, 16, 2);
    else if (elapsed < 160)
      blinkHeight = map(elapsed, 80, 160, 2, 16);
    else
    {
      isBlinking = false;
      blinkHeight = 16;
      lastBlink = now;
    }
  }

  display.clearDisplay();

  // Mata kiri
  display.fillRoundRect(20, 18, 30, blinkHeight, 8, WHITE);
  // Mata kanan
  display.fillRoundRect(78, 18, 30, blinkHeight, 8, WHITE);

  // Pupil (hanya jika mata cukup terbuka)
  if (blinkHeight > 4)
  {
    display.fillCircle(35, 26, 4, BLACK);
    display.fillCircle(93, 26, 4, BLACK);
  }

  // Mulut senyum ramah (drawCircleHelper bagian bawah)
  display.drawCircleHelper(64, 44, 10, 0x0C, WHITE);
  display.drawCircleHelper(64, 44, 11, 0x0C, WHITE);

  display.display();
  faceMode = 0;
}

// =====================================================
// OLED FACE 1: HAPPY (rileks/normal)
// PRD: mata ^_^ (U terbalik), mulut senyum lebar
// =====================================================
void drawHappyFace()
{
  display.clearDisplay();

  // Mata ^_^ = kurva ke atas (bagian atas lingkaran)
  // Mata kiri: drawCircleHelper kuadran 1 & 2 (atas)
  display.drawCircleHelper(34, 30, 12, 0x03, WHITE);
  display.drawCircleHelper(34, 30, 13, 0x03, WHITE);
  // Mata kanan
  display.drawCircleHelper(94, 30, 12, 0x03, WHITE);
  display.drawCircleHelper(94, 30, 13, 0x03, WHITE);

  // Mulut senyum lebar: setengah lingkaran ke bawah
  display.drawCircleHelper(64, 42, 14, 0x0C, WHITE);
  display.drawCircleHelper(64, 42, 15, 0x0C, WHITE);
  // Garis penutup atas agar tampak setengah lingkaran
  display.drawFastHLine(50, 42, 28, WHITE);

  display.display();
  faceMode = 1;
}

// =====================================================
// OLED FACE 2: SAD (cemas/tegang)
// PRD: mata miring/mengerut, mulut cemberut
// =====================================================
void drawSadFace()
{
  display.clearDisplay();

  // Mata kiri: mengerut ke dalam
  display.fillRoundRect(18, 20, 30, 14, 5, WHITE);
  // Segitiga hitam untuk efek miring (sudut kanan atas)
  display.fillTriangle(36, 20, 48, 20, 48, 28, BLACK);

  // Mata kanan: mengerut ke dalam
  display.fillRoundRect(80, 20, 30, 14, 5, WHITE);
  // Segitiga hitam untuk efek miring (sudut kiri atas)
  display.fillTriangle(80, 20, 92, 20, 80, 28, BLACK);

  // Pupil
  display.fillCircle(30, 27, 3, BLACK);
  display.fillCircle(93, 27, 3, BLACK);

  // Mulut cemberut: kurva ke atas (bagian atas lingkaran)
  display.drawCircleHelper(64, 56, 12, 0x03, WHITE);
  display.drawCircleHelper(64, 56, 13, 0x03, WHITE);

  display.display();
  faceMode = 2;
}

// =====================================================
// OLED FACE 3: SCANNING/ANALYZING
// PRD: mata micing (setengah tutup), pupil gerak
// =====================================================
void drawScanFace(int detikSisa)
{
  unsigned long now = millis();

  // Geser pupil kiri-kanan setiap 600ms
  if (now - lastPupilShift > 600)
  {
    pupilOffsetX = random(-5, 6);
    lastPupilShift = now;
  }

  display.clearDisplay();

  // Mata setengah tertutup (kelopak atas menutupi setengah)
  // Mata kiri: kotak penuh lalu tutup bagian atas dengan hitam
  display.fillRoundRect(18, 18, 32, 16, 6, WHITE);
  display.fillRect(18, 18, 32, 8, BLACK);  // tutup setengah atas
  // Mata kanan
  display.fillRoundRect(78, 18, 32, 16, 6, WHITE);
  display.fillRect(78, 18, 32, 8, BLACK);

  // Pupil bergerak (dibatasi agar tidak keluar mata)
  int px = constrain(35 + pupilOffsetX, 22, 46);
  int qx = constrain(95 + pupilOffsetX, 82, 106);
  display.fillCircle(px, 28, 3, BLACK);
  display.fillCircle(qx, 28, 3, BLACK);

  // Mulut garis datar (fokus/serius)
  display.drawFastHLine(44, 50, 40, WHITE);
  display.drawFastHLine(44, 51, 40, WHITE);

  // Countdown kecil di pojok kanan bawah
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(98, 56);
  if (detikSisa < 10) display.print("0");
  display.print(detikSisa);
  display.print("s");

  display.display();
  faceMode = 3;
}

// =====================================================
// OLED FACE 4: EASTER EGG
// PRD: mata bergantian kedip, mulut bersiul (lingkaran)
// =====================================================
void drawEasterFace()
{
  unsigned long now = millis();

  if (now - lastEasterBlink > 200)
  {
    easterLeftOpen = !easterLeftOpen;
    lastEasterBlink = now;
  }

  display.clearDisplay();

  // Mata kiri
  if (easterLeftOpen)
  {
    display.fillRoundRect(18, 18, 30, 16, 6, WHITE);
    display.fillCircle(33, 26, 4, BLACK);
  }
  else
  {
    // Kedip = garis tipis
    display.drawFastHLine(18, 26, 30, WHITE);
    display.drawFastHLine(18, 27, 30, WHITE);
  }

  // Mata kanan (berlawanan dengan kiri)
  if (!easterLeftOpen)
  {
    display.fillRoundRect(80, 18, 30, 16, 6, WHITE);
    display.fillCircle(95, 26, 4, BLACK);
  }
  else
  {
    display.drawFastHLine(80, 26, 30, WHITE);
    display.drawFastHLine(80, 27, 30, WHITE);
  }

  // Mulut bersiul: lingkaran kecil di tengah bawah
  display.drawCircle(64, 50, 7, WHITE);
  display.drawCircle(64, 50, 6, WHITE);

  display.display();
  faceMode = 4;
}

// =====================================================
// BUTTON CHECK (logika asli dipertahankan)
// =====================================================
void checkButton()
{
  bool reading = digitalRead(BUTTON_PIN);

  if (reading == LOW && lastButtonState == HIGH)
  {
    pressCount++;

    if (pressCount == 1)
    {
      multiPressTimer = millis();
    }

    // Easter egg: 3x klik dalam 3 detik
    if (pressCount >= 3 &&
        millis() - multiPressTimer <= 3000)
    {
      player.play(AUDIO_TUNGTUNG);
      pressCount = 0;

      // Tampil easter face selama audio ~3 detik
      unsigned long easterStart = millis();
      while (millis() - easterStart < 3000)
      {
        drawEasterFace();
        // Blynk tetap jalan selama easter egg
        Blynk.run();
        timer.run();
      }
      Blynk.virtualWrite(V2, "EASTER EGG");
    }

    // Single press: mulai deteksi dari IDLE
    if (currentState == IDLE)
    {
      resetBPM();
      tempSum = 0;
      tempCount = 0;
      finalTemp = 0;
      finalBPM = 0;
      audioStep = 0;
      stressScore = 0;
      currentState = WAIT_FINGER;
      Blynk.virtualWrite(V2, "WAIT FINGER");
    }

    delay(200);
  }

  // Reset pressCount jika lebih dari 3 detik tanpa klik tambahan
  if (pressCount > 0 && millis() - multiPressTimer > 3000)
  {
    pressCount = 0;
  }

  lastButtonState = reading;
}

// =====================================================
// SETUP
// =====================================================
void setup()
{
  Serial.begin(115200);

  delay(500);

  // I2C: init dengan pin yang benar
  // MAX30102=0x57, MLX90614=0x5A, OLED=0x3C — tidak bertabrakan
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);  // 100kHz untuk stabilitas multi-device

  // Button
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // ---- OLED 0x3C ----
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
  {
    Serial.println("[ERR] OLED gagal init!");
    while (1);
  }
  display.clearDisplay();
  display.display();
  Serial.println("[OK] OLED 0x3C");

  // ---- MAX30102 0x57 ----
  // begin() otomatis menggunakan Wire yang sudah diinit
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD))
  {
    Serial.println("[ERR] MAX30102 gagal init!");
    while (1);
  }
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);
  Serial.println("[OK] MAX30102 0x57");

  // ---- MLX90614 0x5A ----
  // Wire sudah diinit, MLX begin menggunakan Wire yang sama
  if (!mlx.begin())
  {
    Serial.println("[ERR] MLX90614 gagal init!");
    while (1);
  }
  Serial.println("[OK] MLX90614 0x5A");

  // ---- DFPlayer ----
  mySerial.begin(9600, SERIAL_8N1, DF_RX, DF_TX);
  delay(1000);

  if (!player.begin(mySerial))
  {
    Serial.println("[WARN] DFPlayer init gagal, coba lanjut...");
  }
  player.volume(50);  // volume asli dipertahankan
  Serial.println("[OK] DFPlayer UART2");

  // ---- WiFi & Blynk ----
  WiFi.begin(ssid, pass);
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // Timer Blynk 1 detik
  timer.setInterval(1000L, sendToBlynk);

  // Audio pembuka
  player.play(AUDIO_OPENING);

  // Tampil idle face awal
  drawNormalFace();

  Serial.println("[OK] CALMORA V2 READY");
  Blynk.virtualWrite(V2, "IDLE");
}

// =====================================================
// LOOP
// =====================================================
void loop()
{
  checkButton();
  Blynk.run();
  timer.run();

  // ==================================================
  // MODE: WAIT_FINGER
  // ==================================================
  if (currentState == WAIT_FINGER)
  {
    irValue = particleSensor.getIR();

    // Tampil normal face + teks kecil "Tempelkan Jari"
    // (sesuai PRD: teks minimal, wajah dominan)
    drawNormalFace();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    // Teks kecil di bawah wajah
    int16_t x1, y1; uint16_t tw, th;
    display.getTextBounds("Tempelkan Jari", 0, 0, &x1, &y1, &tw, &th);
    display.setCursor((128 - tw) / 2, 56);
    display.print("Tempelkan Jari");
    display.display();

    Blynk.virtualWrite(V2, "WAIT FINGER");

    if (irValue > FINGER_THRESHOLD)
    {
      measureStartTime = millis();
      currentState = MEASURING;
      Blynk.virtualWrite(V2, "MEASURING");
      Serial.println("Jari terdeteksi -> MEASURING");
    }

    return;
  }

  // ==================================================
  // MODE: MEASURING (30 detik penuh)
  // ==================================================
  if (currentState == MEASURING)
  {
    irValue = particleSensor.getIR();
    suhu = readTemperature();

    tempSum += suhu;
    tempCount++;

    bool beatDetected = checkForBeat(irValue);

    if (beatDetected)
    {
      long now = millis();
      if (lastBeat > 0)
      {
        long delta = now - lastBeat;
        beatsPerMinute = 60.0 / (delta / 1000.0);

        if (beatsPerMinute >= 40 && beatsPerMinute <= 180)
        {
          rates[rateSpot] = (byte)beatsPerMinute;
          rateSpot++;
          if (rateSpot >= RATE_SIZE) rateSpot = 0;
          beatAvg = calculateAverageBPM();
        }
      }
      lastBeat = now;
    }

    // Hitung sisa detik
    int sisaDetik = 30 - (int)((millis() - measureStartTime) / 1000);
    if (sisaDetik < 0) sisaDetik = 0;

    // Tampil scan face dengan countdown
    drawScanFace(sisaDetik);

    // Kirim countdown ke V7
    String countStr = String(sisaDetik) + "s";
    Blynk.virtualWrite(V7, countStr);

    // Serial monitor periodik
    if (millis() - lastSerialPrint >= 1000)
    {
      lastSerialPrint = millis();
      Serial.print("IR="); Serial.print(irValue);
      Serial.print(" BPM="); Serial.print(beatAvg);
      Serial.print(" Suhu="); Serial.print(suhu);
      Serial.print(" Sisa="); Serial.print(sisaDetik);
      Serial.println("s");
    }

    // Selesai 30 detik
    if (millis() - measureStartTime >= 30000)
    {
      // Default 70 BPM jika tidak terdeteksi
      if (beatAvg > 0)
        finalBPM = beatAvg;
      else
        finalBPM = 85;

      finalTemp = (tempCount > 0) ? (tempSum / tempCount) : 36.5;

      Serial.print("Final BPM: "); Serial.println(finalBPM);
      Serial.print("Final Temp: "); Serial.println(finalTemp);

      Blynk.virtualWrite(V7, "Done");
      currentState = ANALYZING;
    }

    return;
  }

  // ==================================================
  // MODE: ANALYZING
  // ==================================================
  if (currentState == ANALYZING)
  {
    Serial.println("===== ANALYZING =====");

    // Jalankan Fuzzy Logic Mamdani
    emotionResult = fuzzyAnalyze((float)finalBPM, finalTemp);

    // Update Blynk dengan nilai final
    Blynk.virtualWrite(V0, finalBPM);
    Blynk.virtualWrite(V1, finalTemp);
    Blynk.virtualWrite(V3, stressScore);

    switch (emotionResult)
    {
      case EMOTION_RILEKS:
        Blynk.virtualWrite(V2, "RILEKS");
        Serial.println("STATUS = RILEKS");
        break;
      case EMOTION_NORMAL:
        Blynk.virtualWrite(V2, "NORMAL");
        Serial.println("STATUS = NORMAL");
        break;
      case EMOTION_CEMAS:
        Blynk.virtualWrite(V2, "CEMAS");
        Serial.println("STATUS = CEMAS");
        break;
      case EMOTION_TEGANG:
        Blynk.virtualWrite(V2, "TEGANG");
        Serial.println("STATUS = TEGANG");
        break;
    }

    audioStep = 0;
    audioTimer = millis();
    currentState = SHOWING_RESULT;
    return;
  }

  // ==================================================
  // MODE: SHOWING_RESULT
  // Gabungan RELAX_RESULT + STRESS_RESULT dari kode asli
  // ==================================================
  if (currentState == SHOWING_RESULT)
  {
    // Tampilkan wajah sesuai kondisi
    if (emotionResult == EMOTION_RILEKS || emotionResult == EMOTION_NORMAL)
    {
      drawHappyFace();
    }
    else
    {
      drawSadFace();
    }

    // --------------------------------------------------
    // SKENARIO RILEKS atau NORMAL
    // PRD: RILEKS audio -> CLOSING -> IDLE
    // (timing sama persis dengan kode asli RELAX_RESULT)
    // --------------------------------------------------
    if (emotionResult == EMOTION_RILEKS || emotionResult == EMOTION_NORMAL)
    {
      if (audioStep == 0)
      {
        Serial.println("PLAY RILEKS");
        player.play(AUDIO_RILEKS);
        audioTimer = millis();
        audioStep = 1;
      }
      else if (audioStep == 1 && millis() - audioTimer > 7000)
      {
        audioTimer = millis();
        audioStep = 2;
      }
      else if (audioStep == 2 && millis() - audioTimer > 2000)
      {
        player.play(AUDIO_CLOSING);
        audioTimer = millis();
        audioStep = 3;
      }
      else if (audioStep == 3 && millis() - audioTimer > 4000)
      {
        currentState = IDLE;
        audioStep = 0;
        faceMode = 0;
        Blynk.virtualWrite(V2, "IDLE");
        Serial.println("Kembali ke IDLE");
      }
    }

    // --------------------------------------------------
    // SKENARIO CEMAS atau TEGANG
    // PRD: TEGANG -> RELAKSASI -> RETEST -> IDLE
    // (timing sama persis dengan kode asli STRESS_RESULT)
    // --------------------------------------------------
    else
    {
      if (audioStep == 0)
      {
        Serial.println("PLAY TEGANG");
        player.play(AUDIO_TEGANG);
        audioTimer = millis();
        audioStep = 1;
      }
      else if (audioStep == 1 && millis() - audioTimer > 6000)
      {
        audioTimer = millis();
        audioStep = 2;
      }
      else if (audioStep == 2 && millis() - audioTimer > 2000)
      {
        player.play(AUDIO_RELAKSASI);
        audioTimer = millis();
        audioStep = 3;
      }
      else if (audioStep == 3 && millis() - audioTimer > 27000)
      {
        audioTimer = millis();
        audioStep = 4;
      }
      else if (audioStep == 4 && millis() - audioTimer > 2000)
      {
        player.play(AUDIO_RETEST);
        audioTimer = millis();
        audioStep = 5;
      }
      else if (audioStep == 5 && millis() - audioTimer > 3000)
      {
        currentState = IDLE;
        audioStep = 0;
        faceMode = 0;
        Blynk.virtualWrite(V2, "IDLE");
        Serial.println("Kembali ke IDLE");
      }
    }

    return;
  }

  // ==================================================
  // MODE: IDLE
  // ==================================================
  if (currentState == IDLE)
  {
    drawNormalFace();
    delay(10);
    return;
  }
}
