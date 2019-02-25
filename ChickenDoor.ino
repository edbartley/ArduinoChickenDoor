#include <Wire.h>

#include <math.h>

// RTClib - Version: Latest
#include <RTClib.h>
#include <Dusk2Dawn.h>


#define DEBUG
#define RTC_ADDR 0x68
#define RTC_RAM_ADDR 7

RTC_DS3231 rtc;
Dusk2Dawn fillmore(34.419691, -118.923892, -8);

bool reInit = true;
int INPUT_PIN = 2;
int OUTPUT_PIN = 4;
int LED_PIN = 13;
int EEPROMADDR = 0;

enum DOOR_CMD
{
  OPEN,
  CLOSE
};

enum DOOR_STATE
{
  CLOSED = 0,
  OPENED = 1,
  CLOSING = 2,
  OPENING = 3,
  PAUSE_CLOSED = 4,
  PAUSE_OPENED = 5
};

enum LED_STATE
{
  HEART_BEAT,
  LOST_POWER,
};

struct DOOR_DATA
{
  DOOR_STATE state = CLOSED;
  DateTime openTimeOn, openTimeOff, moveTimeStart, pauseStartTime;
  DateTime closeTimeOn, closeTimeOff;
  DateTime scheduledOpenTime, scheduledCloseTime;

  DOOR_CMD stack[8];
  int stackPointer = 0;
} door;

struct BUTTON
{
  int state = HIGH;
  int buttonPress;
  int previous = LOW;
  long time = 0;
  long debounce = 200;
} button;

LED_STATE led_state = HEART_BEAT;

#ifdef DEBUG
DateTime nextHeartBeat, nextLEDOffTime;

#define PRT_STR(str) Serial.print(str);
#define PRT_INT(x)   Serial.print(x, DEC);
#define PRT_LN_STR(str) Serial.println(str);
#else
#define PRT_STR(str)
#define PRT_INT(x)
#define PRT_LN_STR(str)
#endif

byte writeRTC(byte value)
{
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(RTC_RAM_ADDR);
  Wire.write(value);

  byte result = Wire.endTransmission();
  return result;
}

byte readRTC()
{
  byte result;

  Wire.beginTransmission(RTC_ADDR);
  Wire.write(RTC_RAM_ADDR);

  if (Wire.endTransmission() != 0)
  {
    return -1;
  }

  Wire.requestFrom((uint8_t)RTC_ADDR, 1);
  result = Wire.read();

  return result;
}

void sendDoorCommand(DOOR_CMD cmd)
{
  PRT_STR("Command Received: ");
  PRT_STR(cmd == OPEN ? "OPEN" : "CLOSE");

  if (door.stackPointer < 8)
  {
    door.stackPointer++;
    door.stack[door.stackPointer] = cmd;
    PRT_LN_STR(" ");
  }
  else
  {
    PRT_LN_STR(" but stackPointer is full");
  }
}

int dayOfWeek(int y, int m, int d)
{
  int xRef[7] = {0, 6, 5, 4, 3, 2, 1};

  if (d + m < 3)
  {
    y = y - 1;
  }
  else
  {
    y = y - 2;
  }

  int dayNum = (23 * m / 9 + d + 4 + y / 4 - y / 100 + y / 400) % 7;

  int result = xRef[dayNum];

  return result;
}
bool checkIfDST()
{
#define MARCH             3
#define NOVEMBER         11
#define FIRST_DAY         1
#define NUM_DAYS_IN_WEEK  7

  bool DST = false;

  DateTime now = rtc.now();

  int year = now.year();
  int month = now.month();
  int day = now.day();
  int hour = now.hour();

  // DST begins 2nd sunday of march
  // DST ends  1st sunday of Nov

  uint32_t startDayDST = DateTime(year, MARCH, FIRST_DAY + NUM_DAYS_IN_WEEK + dayOfWeek(year, MARCH, FIRST_DAY), 2, 0, 0).unixtime();
  uint32_t endDayDST = DateTime(year, NOVEMBER, FIRST_DAY + dayOfWeek(year, NOVEMBER, FIRST_DAY), 2, 0, 0).unixtime();
  uint32_t currTime = now.unixtime();

  if (currTime >= startDayDST && currTime <= endDayDST)
  {
    DST = true;
  }

  return DST;
}

void doorMgr()
{
  if (door.state == OPENED || door.state == CLOSED)
  {
    if (door.stackPointer > 0)
    {

      DOOR_CMD cmd = door.stack[door.stackPointer];
      door.stackPointer --;

      if (cmd == OPEN && door.state == CLOSED)
      {
        digitalWrite(OUTPUT_PIN, HIGH);
        door.state = OPENING;
        door.moveTimeStart = rtc.now();

        writeRTC(OPENING);

        PRT_LN_STR("Executing OPEN cmd");
      }
      else if (cmd == CLOSE && door.state == OPENED)
      {
        digitalWrite(OUTPUT_PIN, HIGH);
        door.state = CLOSING;
        door.moveTimeStart = rtc.now();

        writeRTC(CLOSING);

        PRT_LN_STR("Executing CLOSE cmd");
      }
    }
  }
  else if (door.state == OPENING)
  {
    if (rtc.now().secondstime() >= (door.moveTimeStart + TimeSpan(0, 0, 0, 15)).secondstime())
    {
      digitalWrite(OUTPUT_PIN, LOW);
      door.state = PAUSE_OPENED;
      door.pauseStartTime = rtc.now();

      writeRTC(OPENED);

      PRT_LN_STR("Set door state to PAUSE_OPENED");
    }
  }
  else if (door.state == CLOSING)
  {
    if (rtc.now().secondstime() >= (door.moveTimeStart + TimeSpan(0, 0, 0, 15)).secondstime())
    {
      digitalWrite(OUTPUT_PIN, LOW);
      door.state = PAUSE_CLOSED;
      door.pauseStartTime = rtc.now();

      writeRTC(CLOSED);

      PRT_LN_STR("Set door state to PAUSE_CLOSED");
    }
  }
  else if (door.state == PAUSE_OPENED)
  {
    if (rtc.now().secondstime() >= (door.pauseStartTime + TimeSpan(0, 0, 0, 5)).secondstime())
    {
      door.state = OPENED;
      PRT_LN_STR("Set door state to OPENED");
    }
  }
  else if (door.state == PAUSE_CLOSED)
  {
    if (rtc.now().secondstime() >= (door.pauseStartTime + TimeSpan(0, 0, 0, 5)).secondstime())
    {
      door.state = CLOSED;
      PRT_LN_STR("Set door state to CLOSED");
    }
  }
}

void ledMgr()
{
  if (led_state == HEART_BEAT)
  {
    if (rtc.now().secondstime() >= nextHeartBeat.secondstime())
    {
      nextHeartBeat = rtc.now() + TimeSpan(0, 0, 0, 30);
      nextLEDOffTime = rtc.now() + TimeSpan(0, 0, 0, 1);
      digitalWrite(LED_PIN, HIGH);
    }
    else if (rtc.now().secondstime() >= nextLEDOffTime.secondstime())
    {
      digitalWrite(LED_PIN, LOW);
    }
  }
  else if (led_state == LOST_POWER)
  {
    if (rtc.now().secondstime() >= nextHeartBeat.secondstime())
    {
      nextHeartBeat = rtc.now() + TimeSpan(0, 0, 0, 3);
      nextLEDOffTime = rtc.now() + TimeSpan(0, 0, 0, 1);
      digitalWrite(LED_PIN, HIGH);
    }
    else if (rtc.now().secondstime() >= nextLEDOffTime.secondstime())
    {
      digitalWrite(LED_PIN, LOW);
    }
  }
}

DateTime getSunSet(DateTime day)
{
  int sunset = fillmore.sunset(day.year(), day.month(), day.day(), false);
  int hour = sunset / 60;
  int min = sunset % 60;

  return DateTime(day.year(), day.month(), day.day(), 0, 0, 0) + TimeSpan(0, hour, min + 45, 0);
}

DateTime getSunRise(DateTime day)
{
  int sunrise = fillmore.sunrise(day.year(), day.month(), day.day(), false);
  int hour = sunrise / 60;
  int min = sunrise % 60;

  return DateTime(day.year(), day.month(), day.day(), 0, 0, 0) + TimeSpan(0, hour, min - 30, 0);
}

void buttonPressed()
{
  PRT_LN_STR("Button was pressed.");
  if (door.state == CLOSED)
  {
    sendDoorCommand(OPEN);
  }
  else if (door.state == OPENED)
  {
    sendDoorCommand(CLOSE);
  }
}

void checkButtonPress()
{
  button.buttonPress = digitalRead(INPUT_PIN);

  if (button.buttonPress == HIGH && button.previous == LOW && (long)millis() - button.time > button.debounce)
  {
    if (button.state == HIGH)
    {
      button.state = LOW;
    }
    else
    {
      button.state = HIGH;
    }

    button.time = millis();
  }

  button.previous = button.buttonPress;

  if (button.buttonPress)
  {
    buttonPressed();
  }
}

void setup()
{
#ifdef DEBUG
  {
    Serial.begin(9600);

    delay(3000); // wait for console opening
  }
#endif

  PRT_LN_STR("Entered Setup()");
  if (! rtc.begin())
  {
    PRT_LN_STR("Couldn't find RTC");
    while (! rtc.begin());
  }

  if (rtc.lostPower())
  {
    PRT_LN_STR("Power was lost");

    led_state = LOST_POWER;
  }

  pinMode(OUTPUT_PIN, OUTPUT);
  digitalWrite(OUTPUT_PIN, LOW);
  pinMode(INPUT_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

#ifdef DEBUG
  nextHeartBeat = rtc.now() + TimeSpan(0, 0, 0, 30);
  nextLEDOffTime = nextHeartBeat + TimeSpan(0, 0, 0, 1);
#else
  digitalWrite(LED_PIN, LOW);
#endif

  int storedDoorState = readRTC();
  if (storedDoorState > OPENING)
  {
    PRT_LN_STR("Setting RTC RAM to DOOR_STATE = CLOSED");
    writeRTC(CLOSED);
  }
  else
  {
    door.state = storedDoorState;
  }
}

void printDateTime(DateTime t)
{
  PRT_INT(t.month());
  PRT_STR("/");
  PRT_INT(t.day());
  PRT_STR(" ");

  if (t.hour() < 10)
  {
    PRT_STR("0");
  }

  PRT_STR(t.hour());
  PRT_STR(":");

  if (t.minute() < 10)
  {
    PRT_STR("0");
  }

  PRT_INT(t.minute());

  PRT_LN_STR(" UTC");
}

#ifdef DEBUG
void handleSerial()
{
  while (Serial.available() > 0)
  {
    char incomingCharacter = Serial.read();
    switch (incomingCharacter)
    {
      case 't':
        {
          PRT_STR("The UTC time is: ");
          printDateTime(rtc.now());

          PRT_STR("Scheduled door to close at UTC: ");
          printDateTime(door.scheduledCloseTime);

          PRT_STR("Scheduled door to open at UTC: ");
          printDateTime(door.scheduledOpenTime);
          break;
        }
      case 'b':
        {
          buttonPressed();
          break;
        }
      case 'r':
        {
          DateTime setTime = DateTime(F(__DATE__), F(__TIME__));
          reInit = true;
          rtc.adjust(setTime);

          if (checkIfDST())
          {
            rtc.adjust(setTime - TimeSpan(1, 0, 0, 0));
          }

          PRT_STR("Reset time to UTC: ");
          printDateTime(rtc.now());
          break;
        }
      case 's':
        {
          int storedDoorState = readRTC();

          if (storedDoorState == 0)
          {
            PRT_LN_STR("RTC door state is closed");
          }
          if (storedDoorState == 1)
          {
            PRT_LN_STR("RTC door state is open");
          }
          if (storedDoorState == 2)
          {
            PRT_LN_STR("RTC door state is closing");
          }
          if (storedDoorState == 3)
          {
            PRT_LN_STR("RTC door state is opening");
          }

          break;
        }
    }
  }
}
#endif

void loop()
{
  DateTime now = rtc.now();

#ifdef DEBUG
  handleSerial();
#endif

  if (reInit)
  {
    PRT_LN_STR("Reinit");

    reInit = false;

    DateTime sunset = getSunSet(now);
    DateTime sunrise = getSunRise(now);

    PRT_STR("Sunrise is UTC: ");
    printDateTime(sunrise);

    PRT_STR("Sunset is UTC: ");
    printDateTime(sunset);

    if (now.secondstime() < sunrise.secondstime())
    {
      door.scheduledOpenTime = sunrise;
      PRT_STR("Scheduled door to open at UTC: ");
      printDateTime(door.scheduledOpenTime);
    }

    if (now.secondstime() < sunset.secondstime())
    {
      door.scheduledCloseTime = sunset;
      PRT_STR("Scheduled door to close at UTC: ");
      printDateTime(door.scheduledCloseTime);
    }
  }

  if (now.secondstime() > door.scheduledOpenTime.secondstime())
  {
    sendDoorCommand(OPEN);

    DateTime sunriseTomorrow = getSunRise(now + TimeSpan(1, 0, 0, 0));
    door.scheduledOpenTime = sunriseTomorrow;

    PRT_STR("Rescheduled door open time to UTC: ");
    printDateTime(door.scheduledOpenTime);
  }
  else if (now.secondstime() > door.scheduledCloseTime.secondstime())
  {
    sendDoorCommand(CLOSE);

    DateTime sunsetTomorrow = getSunSet(now + TimeSpan(1, 0, 0, 0));
    door.scheduledCloseTime = sunsetTomorrow;

    PRT_STR("Rescheduled door close time to UTC: ");
    printDateTime(door.scheduledCloseTime);
  }

  doorMgr();
  ledMgr();
}
