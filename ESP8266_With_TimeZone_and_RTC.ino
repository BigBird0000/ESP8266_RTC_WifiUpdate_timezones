
// this uses a button to cycle through the US Timezones
// and store the timezone in eprom
// connect one side of button to HIGH


// ITEMS TO CHANGE
/*********  EPROM ***************************/
// 4 bytes is used to store Eprom SIgnature
#define Num_Signature 4      
// change these I just used DMC <decimal 10> to indicate if eprom is good value
const uint8_t EEPROM_ADDRESS[Num_Signature] = {   0,   1,   2,   3};
const byte    EEPROM_SIGNATURE[Num_Signature] = {'D', 'M', 'C', 10};
const uint8_t EEPROM_VALUE = 8; // actual value saved in EEPROM location 8

// for ESP8266 microcontroller
int PIN_RTC_SCL = D1;  // Yellow
int PIN_RTC_SDA = D2;  // Green
int PIN_BUTTON = D8;   // Purple - the 8266 pulls to ground, no boot if high
// connect other side to high (3V3)


/***** Button To Cancel, Commit or Change TZ */
// button can be used to change TZ, Cancel TZ change, or Commit Change to EPROM
// simple press, less than CommitToEpromIfHeldForMS, change to next TZ
//   CANCEL
int CancelButtonPresses = 3;    // cancel TZ change if pressed this many times
int CancelIfPressedWithinMS = 2000; // this many ms
//   COMMIT
int CommitToEpromIfHeldForMS = 3000;    // commit to eprom if button held for x ms

uint8 debounce = 100;


/* * * * * TimeZones * * * * * * * * * * * * * * * * *  * * * * * * * * * * */
// library from https://github.com/JChristensen/Timezone
// read readme.md for info about changing time zones

// change this to match dst/std. index in dstNames[] /stdNames[]
// 0: PDT/PST, 1: Arizona (no daylight savings) etc
#define DefaultTimeZoneIndex 0

const char* dstNames[] = {"PDT", "AzT", "MDT", "CDT", "EDT", "AST", "HST"};
const char* stdNames[] = {"PST", "AzT", "MST", "CST", "EST", "ADT", "HST"};
const int dstOffsets[] = { -420, -420, -360,   -300,  -240,   -480, -600};
const int stdOffsets[] = { -480, -420, -420,   -360,  -300,   -540, -600};

/*
  Create a Credentials.h file under Credentials Directory in Arduino Libs
  /documents/arduino/libraries/Credentials/Credentials.h

  #ifndef CREDENTIALS_INC
  #define CREDENTIALS_INC
  const char* ssid     = "put your SSID for your wifi here";
  const char* password = "put your wifi password here";
  #endif

*/

/* no configuration changes below this point */
/* no configuration changes below this point */
/* no configuration changes below this point */



#include <EasyButton.h>
#include <EasyButtonBase.h>
#include <EasyButtonTouch.h>
#include <EasyButtonVirtual.h>
#include <Sequence.h>

#include "Credentials.h"
#include <ESP8266WiFi.h>
#include <NTPClient.h> //NTPClient by Arduino
#include <WiFiUdp.h>
#include <Wire.h>
#include <EEPROM.h>
#include <RTClib.h> //RTClib by Adafruit
#include <Timezone.h>

/*****************************************
   tm.display(9876)->blink(1500)

*/


#define NTP_SERVER "0.us.pool.ntp.org"

RTC_DS1307 rtc;
WiFiUDP ntpUDP;

// You can specify the time server pool and the offset, (in seconds)
// additionaly you can specify the update interval (in milliseconds).
// save as UTC
NTPClient timeClient(ntpUDP, NTP_SERVER, 0);//, 60000);





String MyString;


bool bPullup = false; // false=do not pullup high
bool bLogic = false;    // false = do not invert logic
//  use High as pressed
EasyButton ButtonTZ(PIN_BUTTON, debounce, bPullup, bLogic);



TimeChangeRule dstRule = {"EDT", Second, Sun, Mar, 2, -240};
TimeChangeRule stdRule = {"EST", First, Sun, Nov, 2, -300};
Timezone tz(dstRule, stdRule);
char TZAbbrev[6];

bool LOADING = true;  // only display progress on TM1637 during initial startup
int timeUpdated = 0;
int gHour = 30;
int gMinute = 90;
int gYear = 9999;
int gMonth = 13;
int gDay = 32;
int IntChange = 0;
int PrsChange = 0;
int gLastYear = 9999;
int gLastMinute = 9999;
int gLastHour = 9999;
int gLastDay = 9999;
bool TimeWasSynched = false;
bool OldLEDPMState;
bool OldLEDAMState;
int CommitInMilliSeconds = 0;
int bSetCommit = false;
uint8_t tzIndex = 0;          //index current TimeZone



/* * * * * *   ISR  * * * * * * * * * */
void ButtonISR()
{
  //When button is being used through external interrupts, parameter INTERRUPT must be passed to read() function
  ButtonTZ.read();
  IntChange = 1;
}
void ButtonPressed()
{
  PrsChange = 2;
}

// called on Hold 5 secons
void ButtonCommit()
{
  Serial.println("Commit");
  bSetCommit = false;
  EEPromCommit();
}

// called on sequence (3 presses with 2 seconds)
void ButtonCancel()
{
  Serial.println("Cancelled");
  bSetCommit = false;
}






uint8_t ReadTimeZoneIndex()
{
  uint8_t RetValue;
  RetValue = EEPROM.read(EEPROM_VALUE);
  // Serial.print("Read EEPROM["); Serial.print(EEPROM_VALUE); Serial.print("]="); Serial.println(RetValue);
  return RetValue;
}


void EEPromCommit()
{
  Serial.println("@EEPromCommit");
  EEPROM.commit();
}


bool CompareEEpromData(int ADDR, uint8_t VAL)
{
  if (EEPROM.read(ADDR) == VAL)
    return true;
  Serial.println("<< EEProm Invalid >>");
  return false;
}

void EEPROMWrite(int ADDR, uint8_t VAL)
{
  EEPROM.write(ADDR, VAL);
  int Verify = EEPROM.read(ADDR);
  if (Verify == VAL)
    Serial.println("  OK");
  else
    Serial.println(" * * * * * * * * * Error");
}

void RepairEEpromSignature()
{
  for (int I = 0; I < Num_Signature; I++)
    EEPROMWrite(EEPROM_ADDRESS[I], EEPROM_SIGNATURE[I]);
  // set timezone to default (in my case, it is PDT/PST)
  EEPROMWrite(EEPROM_VALUE, DefaultTimeZoneIndex);
  EEPromCommit();
}

void CheckEEpromSignature()
{
  for (int I = 0; I < Num_Signature; I++)
    if (!CompareEEpromData(EEPROM_ADDRESS[I], EEPROM_SIGNATURE[I]))
    {
      RepairEEpromSignature();
      return;
    }
}

/*************************************************/

void CheckIfResyncNeeded()
{
  if (gHour != 3)
  {
    TimeWasSynched = false;       // if not hour  3 then set TimeSynched = false so we will run at next 3:00
    return;
  }

  // sync during 3:00 hour
  if (!TimeWasSynched)
  {
    Serial.println("Going To SyncTime");
    SyncTime();
    TimeWasSynched = true;
  }
}


void DisplayOnModuleTime(String Msg)
{
  Serial.println("Module Hour:" + Msg);
}




void CharToStringL(const char S[], String & D)
{
  byte at = 0;
  const char *p = S;
  D = "";

  while (*p++)
    D.concat(S[at++]);
}
String PadLeft (int Val, unsigned int NumChars)
{
  String sVal;
  char buff[10];
  /*int N = */sprintf(buff, "%d", Val);
  CharToStringL(buff, sVal);
  //Serial.println("PadLeft ("); Serial.print(Val);
  //Serial.print("  sEpromValue='"); Serial.print(sVal); Serial.println("'");
  while (sVal.length() < NumChars)
  {
    sVal = " " + sVal;
  }
  return sVal;
}
String PadLeft (String sVal, unsigned int NumChars)
{
  // Serial.print("PadLeft ("); Serial.print(sVal);
  // Serial.print(", "); Serial.print(NumChars); Serial.println(")");
  //Serial.print("CurLen="); Serial.println(sVal.length());

  while (sVal.length() < NumChars)
  {
    sVal = " " + sVal;
  }
  return sVal;
}


void ChangeTimeZone()
{
  Serial.println(IntChange);

  tzIndex = ReadTimeZoneIndex();

  if ( ++tzIndex >= sizeof(stdOffsets) / sizeof(stdOffsets[0]) )
    tzIndex = 0;

  SetCorrectTimeZoneParameters();
  EEPROMWrite(EEPROM_VALUE, tzIndex);    // save new index

  // Set Display to show TZ
  /* convert to padded string */
  String sTZAbbrev;
  CharToStringL(TZAbbrev, MyString);
  Serial.println("TimeZone: " + sTZAbbrev);
  DateTime now;
  // display Time

  now = rtc.now();


  time_t utc = now.unixtime();
  TimeChangeRule *tcr;        // pointer to the time change rule, use to get the TZ abbrev
  time_t t = tz.toLocal(utc, &tcr);

  gHour = hour(t);
  gMinute = minute(t);
  DisplayTimeOnLCD(true);

  delay(5000);// delay 5 seconds
  gLastYear = 0; // so year display will update from TimeZone abbrev
  IntChange = 0;
  CommitInMilliSeconds = 5000;
  bSetCommit = true;
}


void SetCorrectTimeZoneParameters()
{
  Serial.print("ChangeTZ to index: "); Serial.println(tzIndex);
  EEPROM.write(8, tzIndex );
  dstRule.offset = dstOffsets[tzIndex];
  stdRule.offset = stdOffsets[tzIndex];
  strcpy(dstRule.abbrev, dstNames[tzIndex]);
  strcpy(stdRule.abbrev, stdNames[tzIndex]);
  tz.setRules(dstRule, stdRule);

  // set
  DateTime now = rtc.now();
  time_t utc = now.unixtime();


  // given a Timezone object, UTC and a string description, convert and print local time with time zone
  TimeChangeRule *tcr;        // pointer to the time change rule, use to get the TZ abbrev

  time_t t = tz.toLocal(utc, &tcr);

  Serial.print("TCR Abbrev: '");
  Serial.print(tcr -> abbrev);
  Serial.println("'");

  if (tz.locIsDST(t))
    strncpy (TZAbbrev, dstRule.abbrev, sizeof(dstRule.abbrev) );
  else
    strncpy (TZAbbrev, stdRule.abbrev, sizeof(stdRule.abbrev) );
}



void DisplayTimeOnLCD(bool ForceUpdate)
{
  if (!ForceUpdate)
  {
    if ((gLastHour == gHour) && (gLastMinute == gMinute))
      return;
  }

  gLastMinute = gMinute;
  gLastHour = gHour;

  Serial.print("Hour: "); Serial.print(gHour);
  Serial.print(" - Minute: "); Serial.println(gMinute);
}


void DisplayTime(bool ForceUpdate)
{
  DateTime now;
  now = rtc.now();

  //  Timezone tz = usPT;
  time_t utc = now.unixtime();
  // given a Timezone object, UTC and a string description, convert and print local time with time zone

  TimeChangeRule *tcr;        // pointer to the time change rule, use to get the TZ abbrev

  time_t t = tz.toLocal(utc, &tcr);

  gHour = hour(t);
  gMinute = minute(t);



  Serial.print(" ");

  PrintHoursAs12Hour();

  Print2Digits(gHour);
  Serial.print(":");
  Print2Digits(gMinute);
  Serial.print(":");
  Print2Digits(now.second());
  Serial.print(" ");

  DisplayTimeOnLCD(ForceUpdate);
  Serial.println("");
}





void SetUpButton()
{
  ButtonTZ.begin();
  // call ButtonCancel if x button presses in y ms
  ButtonTZ.onSequence(CancelButtonPresses, CancelIfPressedWithinMS , ButtonCancel);
  // commit to eprom if held for x seconds
  ButtonTZ.onPressedFor(CommitToEpromIfHeldForMS, ButtonCommit);


  if (ButtonTZ.supportsInterrupt())
  {
    ButtonTZ.enableInterrupt(ButtonISR);
    Serial.println("Button will be used through interrupts");
  }
  else
    ButtonTZ.onPressed(ButtonPressed);
}

void SyncTime(void)
{
  Serial.println("@SyncTime");
  //Connect to Wifi
  // ScanForNetworks();
  Serial.println("@Connecting To Wifi");
  WiFi.begin(ssid, password);
  Serial.println("...waiting");
  while ( WiFi.status() != WL_CONNECTED ) {
    delay (500);
    Serial.print ( "." );
  }
  Serial.println("\r\nConnected");


  timeClient.begin();
  timeClient.forceUpdate();
  Serial.println("@5"); delay(250);

  long actualTime = -1;
  int Try = 0;
  while ((actualTime < 0) && (Try < 40))
  {
    delay(500);
    timeClient.forceUpdate();
    actualTime = timeClient.getEpochTime();
    Serial.print("Internet Epoch Time: ");
    Serial.println(actualTime);
    Try++;
  }


  if (actualTime < 0)  // No Wifi
  {
    if (LOADING)
    {
      Serial.println("No Wifi"); delay(250);
    }
  }
  else // Have Wifi, set RTC to Wifi Time

    rtc.adjust(DateTime(actualTime));
  DisplayTime(true);
  WiFi.disconnect();
}

void Print2Digits(int Num)
{
  if (Num < 10)
    Serial.print("0");
  Serial.print(Num);
}






void PrintHoursAs12Hour()
{
  if (gHour > 12)
    gHour = gHour - 12;
  if (gHour == 0)
    gHour = 12;
}

void  VerifyNVMemory(uint8_t address, uint8_t data)
{
  uint8_t ReadData;
  Serial.print("Write["); Serial.print(address);
  Serial.print("]="); Serial.print(data);
  rtc.writenvram(address, data);
  ReadData = rtc.readnvram(address);
  Serial.print(", Verify: ");
  if (ReadData == data)
    Serial.println("OK");
  else
  {
    Serial.print(" ** ** ** FAILED: ");
    Serial.println(ReadData);
    delay(1000);
  }
}

void TestNVRam()
{
  for (int ADDR = 0; ADDR < 55; ADDR++)
  {

    VerifyNVMemory(ADDR, 0);
    VerifyNVMemory(ADDR, 10);

  }
  while (1) delay(1000);
}

/* * * * * * * * * * * * * * * *  SETUP * * * * * * * * * * * * * * * */
void setup()
{
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  delay(1000);
  Serial.println("\r\n\r\nDMC CLOCK - Middle / Current Time / (Green) Vers 0.3");
  Serial.print("TimeCompiled: "); Serial.println(__TIME__);

  Serial.println("Pins Set");

  LOADING = true;



  EEPROM.begin(50);
  CheckEEpromSignature();

  tzIndex = ReadTimeZoneIndex();

  SetCorrectTimeZoneParameters();

  if (!rtc.begin())
  {
    Serial.println("Couldn't find RTC");
    delay(3000);
  }
  uint8_t RTCRunning = rtc.isrunning();
  if (RTCRunning == 0)
  {
    Serial.println("RTC Not Running");

    // write a time
    rtc.adjust(DateTime(__DATE__, __TIME__));

    // rtc.​adjust(DateTime(__DATE__, __TIME__));
    RTCRunning = rtc.isrunning();
    if (RTCRunning == 0)
    {
      Serial.println("RTC Not Running after set");
      delay(5000);
    }
  }

  Serial.println("Waiting for sync, can take about a minute or so");
  SyncTime();
  Serial.println("Synch OK");

                 gLastMinute = 99;

                 LOADING = false;

                 SetUpButton();

               }

                 void loop ()
                 {
#define UpdateEveryXms 1000
                 CheckIfResyncNeeded();
                 DisplayTime(false);
                 delay(UpdateEveryXms);
                 if (IntChange != 0)
                 ChangeTimeZone();
                 if (PrsChange != 0)
                 ChangeTimeZone();

                 if (bSetCommit)
                 {
                 CommitInMilliSeconds = CommitInMilliSeconds - UpdateEveryXms;
                 Serial.print("after - CommitInMS = ");
                     Serial.println(CommitInMilliSeconds);

                     if (CommitInMilliSeconds < 0)
                     {
                     Serial.println("Fix AJS Commit");
                     //   EEPROM.commit;
                     bSetCommit = false;
                   }


                   }


                   }
