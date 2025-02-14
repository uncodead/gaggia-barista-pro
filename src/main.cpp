#include "time.h"
#include "Arduino.h"
#include "max6675.h"
#include "TFT_eSPI.h"

#include <EEPROM.h>
#include <PID_v1.h>
#include <PID_AutoTune_v0.h>

#define gray 0x6B6D
#define blue 0x0967
#define orange 0xC260
#define purple 0x604D
#define green 0x1AE9

int gw = 204;
int gh = 102;
int gx = 110;
int gy = 144;
TFT_eSPI lcd = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&lcd);

enum BrewMode
{
  BREW,
  STEAM,
};
BrewMode brewMode;
int lastSteamState = HIGH;

String brewModeString()
{
  if (brewMode == BREW)
  {
    return "BREW";
  }
  else
  {
    return "STEAM";
  }
}

const int STEAM_PIN = 43;
boolean steamOn = false;

const int RELAY_PIN = 13;
boolean relayState = false;

boolean tunePid = false;
int sampleTime = 1500;
#define PWM_MIN 0
#define PWM_MAX 255
double input, output, setpoint;
PID pid(&input, &output, &setpoint, 1, 1, 1, DIRECT);
PID_ATune aTune(&input, &output);

float temps[8] = {0};
int lastIndexTemp = 7;
int tempOffset = 0;
int thermoDO = 10;
int thermoCS = 11;
int thermoCLK = 12;
MAX6675 thermocouple(thermoCLK, thermoCS, thermoDO);

int buttonTimeout = 5000;
int buttonStartTimeout = 0;

int longPress = 2000;
int shortPress = 1000;
bool textVisible = true;
unsigned long textMillis = 0;
unsigned long textInterval = 500;

const int KEY_PIN = 14;
const int BOT_PIN = 0;
unsigned long buttonPressStartTime = 0;
bool buttonKeyPressed = false;
bool buttonBotPressed = false;

enum MenuState
{
  MENU_OFF,
  BREW_TEMP_MODE,
  STEAM_TEMP_MODE,
  TUNNING_MODE
};

int eepromAddr = 0;
MenuState currentMenuState = MENU_OFF;
struct Config
{
  float brewTemp;
  float steamTemp;
  float kp;
  float ki;
  float kd;
};
Config conf;

void saveConfig(const Config &config)
{
  EEPROM.begin(sizeof(Config));
  EEPROM.put(eepromAddr, config);
  EEPROM.commit();
}

Config getConfig()
{
  Config config;
  EEPROM.begin(sizeof(Config));
  EEPROM.get(eepromAddr, config);
  return config;
}

void setup()
{
  Serial.begin(9600);

  pinMode(KEY_PIN, INPUT_PULLUP);
  pinMode(BOT_PIN, INPUT_PULLUP);
  pinMode(STEAM_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);

  lcd.init();
  lcd.fillScreen(TFT_BLACK);
  lcd.setRotation(1);
  sprite.createSprite(320, 170);
  sprite.setTextDatum(3);
  sprite.setSwapBytes(true);
  analogReadResolution(10);

  pid.SetMode(AUTOMATIC);
  pid.SetOutputLimits(PWM_MIN, PWM_MAX);
  pid.SetSampleTime(sampleTime);
  aTune.SetControlType(1);
  aTune.SetNoiseBand(1);
  aTune.SetOutputStep(50);
  aTune.SetLookbackSec(20);

  conf = getConfig();
  if (isnan(conf.brewTemp))
  {
    conf.brewTemp = 60;
    conf.steamTemp = 120;
    conf.kp = 15;
    conf.ki = 0.006;
    conf.kd = 20;
  }
  saveConfig(conf);

  setpoint = conf.brewTemp;
  pid.SetTunings(conf.kp, conf.ki, conf.kd);
  delay(500);
}

void handleButtonPress(int buttonState, bool &buttonFlag)
{
  if (buttonState == LOW && !buttonFlag)
  {
    buttonPressStartTime = millis();
    buttonFlag = true;
  }
}

void loop()
{
  int keyState = digitalRead(KEY_PIN);
  int botState = digitalRead(BOT_PIN);

  handleButtonPress(keyState, buttonKeyPressed);
  handleButtonPress(botState, buttonBotPressed);

  if (keyState == HIGH && buttonKeyPressed)
  {
    unsigned long buttonPressDuration = millis() - buttonPressStartTime;
    if (buttonPressDuration >= longPress)
    {
      if (currentMenuState == MENU_OFF)
      {
        currentMenuState = BREW_TEMP_MODE;
      }
      else if (currentMenuState == BREW_TEMP_MODE)
      {
        currentMenuState = STEAM_TEMP_MODE;
      }
      else if (currentMenuState == STEAM_TEMP_MODE)
      {
        currentMenuState = TUNNING_MODE;
      }
      else
      {
        currentMenuState = MENU_OFF;
        tunePid = false;
      }
    }
    else if (buttonPressDuration < longPress && currentMenuState == BREW_TEMP_MODE)
    {
      conf.brewTemp += 1;
    }
    else if (buttonPressDuration < longPress && currentMenuState == STEAM_TEMP_MODE)
    {
      conf.steamTemp += 1;
    }
    else if (buttonPressDuration < longPress && currentMenuState == TUNNING_MODE)
    {
      tunePid = true;
    }
    buttonStartTimeout = millis();
    saveConfig(conf);
    buttonKeyPressed = false;
  }

  if (botState == HIGH && buttonBotPressed)
  {
    unsigned long buttonPressDuration = millis() - buttonPressStartTime;
    if (buttonPressDuration < 3000 && currentMenuState == BREW_TEMP_MODE)
    {
      conf.brewTemp -= 1;
    }
    else if (buttonPressDuration < longPress && currentMenuState == STEAM_TEMP_MODE)
    {
      conf.steamTemp -= 1;
    }
    buttonStartTimeout = millis();
    saveConfig(conf);
    buttonBotPressed = false;
  }

  if (millis() - buttonStartTimeout >= buttonTimeout)
  {
    currentMenuState = MENU_OFF;
  }

  if (millis() - textMillis >= textInterval)
  {
    textVisible = !textVisible;
    textMillis = millis();
  }

  float tempc = thermocouple.readCelsius() + tempOffset;
  for (int i = 0; i < lastIndexTemp; i++)
  {
    temps[i] = temps[i + 1];
  }
  temps[lastIndexTemp] = tempc;

  input = tempc;

  if (tunePid)
  {
    byte val = (aTune.Runtime());
    Serial.print("tunning val: ");
    Serial.println(val);
    if (val != 0)
    {
      tunePid = false;
    }
    if (!tunePid)
    {
      Serial.println("tunned ");
      pid.SetTunings(aTune.GetKp(), aTune.GetKi(), aTune.GetKd());
    }
  }
  int steamState = digitalRead(STEAM_PIN);
  if (steamState == LOW)
  {
    brewMode = STEAM;
  }
  else
  {
    brewMode = BREW;
  }

  lastSteamState = steamState;

  if (brewMode == STEAM)
  {
    setpoint = conf.steamTemp;
  }
  else
  {
    setpoint = conf.brewTemp;
  }
  pid.Compute();
  int pwmValue = output;
  if (pwmValue > 0 and input < setpoint and input > 0)
  {
    analogWrite(RELAY_PIN, pwmValue);
    relayState = true;
  }
  else
  {
    pwmValue = 0;
    analogWrite(RELAY_PIN, 0);
    relayState = false;
  }

  sprite.fillSprite(TFT_BLACK);
  sprite.setTextColor(TFT_WHITE, blue);
  sprite.fillRoundRect(6, 5, 38, 32, 4, blue);
  sprite.fillRoundRect(52, 5, 38, 32, 4, blue);
  sprite.fillRoundRect(6, 40, 84, 42, 4, green);
  sprite.fillRoundRect(6, 80, 84, 76, 4, purple);
  sprite.drawString(String(10), 10, 24, 4);
  sprite.drawString(String(30), 56, 24, 4);
  sprite.setTextColor(TFT_WHITE, green);

  if ((currentMenuState != TUNNING_MODE || textVisible))
  {
    sprite.drawString("KP: " + String(conf.kp), 9, 50, 1);
    sprite.drawString("KI: " + String(conf.ki), 9, 60, 1);
    sprite.drawString("KD: " + String(conf.kd), 9, 70, 1);
  }

  sprite.setTextColor(TFT_RED, purple);
  sprite.drawString("TEMP: " + String(tempc, 1), 9, 90, 2);
  sprite.setTextColor(TFT_CYAN, purple);
  if ((currentMenuState != BREW_TEMP_MODE || textVisible))
  {
    sprite.drawString("BW: " + String(conf.brewTemp, 1), 9, 105, 2);
  }
  sprite.setTextColor(TFT_GREEN, purple);
  if ((currentMenuState != STEAM_TEMP_MODE || textVisible))
  {
    sprite.drawString("ST: " + String(conf.steamTemp, 1), 9, 120, 2);
  }
  sprite.drawString("PWM: " + String(pwmValue), 9, 135, 2);
  sprite.setTextColor(TFT_YELLOW, TFT_BLACK);
  sprite.drawString("Mode: " + brewModeString() + " ST: " + String(setpoint, 1) + "°C", gx + 5, 16, 2);
  if (tunePid)
  {
    sprite.drawString("Tuning PID", gx + 95, 16, 2);
  }
  if (textVisible)
  {
    sprite.drawString("_", gx + 165, 16, 2);
  }
  sprite.setFreeFont();

  int ngx = gx + 5;

  int tempScale[5] = {0, 50, 100, 150, 200};

  float tempRange = tempScale[4] - tempScale[0];
  float pixelRange = gh;
  float y0 = gy;

  for (int i = 0; i <= 4; i++)
  {
    float temperature = tempScale[i];
    float y = y0 - (temperature - tempScale[0]) * pixelRange / tempRange;
    sprite.drawLine(ngx, y, ngx + gw, y, gray);
    sprite.drawString(String(temperature, 0), gx - 16, y);
  }

  float brewSetPoint = y0 - (conf.brewTemp - tempScale[0]) * pixelRange / tempRange;
  float steamSetPoint = y0 - (conf.steamTemp - tempScale[0]) * pixelRange / tempRange;

  sprite.drawLine(ngx, steamSetPoint, ngx + gw, steamSetPoint, TFT_GREEN);
  sprite.drawLine(ngx, brewSetPoint, ngx + gw, brewSetPoint, TFT_CYAN);

  sprite.drawLine(ngx, gy, ngx + gw, gy, TFT_WHITE);
  sprite.drawLine(ngx, gy, ngx, gy - gh, TFT_WHITE);

  for (int i = 0; i < lastIndexTemp; i++)
  {
    if (temps[i] > 0)
    {
      float y = y0 - (temps[i] - tempScale[0]) * pixelRange / tempRange;
      sprite.drawLine(ngx + (i * 17), y, ngx + ((i + 1) * 17), y, TFT_RED);
    }
  }

  sprite.pushSprite(0, 0);

  if (currentMenuState == MENU_OFF || currentMenuState == TUNNING_MODE)
  {
    delay(sampleTime);
  }
  else
  {
    delay(100);
  }
}