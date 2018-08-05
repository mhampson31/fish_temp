
///////////////////////////////////////////////////////////////////
// Current Satellite LED+ Controller  V4.0                       //
//   Indychus...Dahammer...mistergreen @ plantedtank.net         //
//   This code is public domain.  Pass it on.                    //
//   Confirmed on Arduino UNO 1.0.5                              //
//   Req. Time, TimeAlarms, RTClib, IRremote                     //
///////////////////////////////////////////////////////////////////

// todo: light as temp alarm 

#include <Wire.h>
//#include <RTClib.h>

#include <DS1307RTC.h>
/* For daylight savings time, just run the DS1307/SetTime example.
   And don't forget to give the RTC some power. */

#include <Time.h>
#include <IRremote.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <U8glib.h>
#include <TimeAlarms.h>

// Temp sensor pin setting
#define ONE_WIRE_BUS A0

//SSD1306 OLED pin settings
#define OLED_DATA 9
#define OLED_CLK 10
#define OLED_DC 11
#define OLED_CS 12
#define OLED_RST 13

#define COLOR_W 0 //white
#define COLOR_B 1 //blue
#define COLOR_GR 2 //green and red, they're always the same value for us


/* There appear to be ~42 steps from Full Spectrum to off. Each color is the same.
 But the count seems to be inconsistent from trial to trial.
 So this might end up being off by a bit, but we reset at each time change, so no biggy.
 */
#define MAX_LIGHT 42
#define TWILIGHT 10

//Device initialization
U8GLIB_SSD1306_128X64 U8G(OLED_CLK, OLED_DATA, OLED_CS, OLED_DC, OLED_RST);

IRsend irsend;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature Sensors(&oneWire);

// Current Satellite+ IR Codes (NEC Protocol)
unsigned long codeHeader = 0x20DF; // Always the same

// Remote buttons listed left to right, top to bottom
PROGMEM unsigned int arrCodes[32] = {
  0x3AC5,  // 1 -  Orange
  0xBA45,  // 2 -  Blue
  0x827D,  // 3 -  Rose
  0x02FD,  // 4 -  Power On/Off
  0x1AE5,  // 5 -  White
  0x9A65,  // 6 -  FullSpec
  0xA25D,  // 7 -  Purple
  0x22DD,  // 8 -  Play/Pause
  0x2AD5,  // 9 -  Red Up
  0xAA55,  // 10 - Green Up
  0x926D,  // 11 - Blue Up
  0x12ED,  // 12 - White Up
  0x0AF5,  // 13 - Red Down
  0x8A75,  // 14 - Green Down
  0xB24D,  // 15 - Blue Down
  0x32CD,  // 16 - White Down
  0x38C7,  // 17 - M1 Custom
  0xB847,  // 18 - M2 Custom
  0x7887,  // 19 - M3 Custom
  0xF807,  // 20 - M4 Custom
  0x18E7,  // 21 - Moon 1
  0x9867,  // 22 - Moon 2
  0x58A7,  // 23 - Moon 3
  0xD827,  // 24 - Dawn/Dusk
  0x28D7,  // 25 - Cloud 1
  0xA857,  // 26 - Cloud 2
  0x6897,  // 27 - Cloud 3
  0xE817,  // 28 - Cloud 4
  0x08F7,  // 29 - Storm 1
  0x8877,  // 30 - Storm 2
  0x48B7,  // 31 - Storm 3
  0xC837   // 32 - Storm 4
}; 

struct Period {
  char name[10];
  char brightness[3];
  void (*default_light)(void);
  int start_hour;
};

#define PERIOD_COUNT 4 //We need to reference the size of this array later; this define saves us some work

/* Be sure there are enough alarms set in TimeAlarms.h */
Period period_list[PERIOD_COUNT] = {
  {"Morning", {TWILIGHT, 0, TWILIGHT}, M2Custom, 7}, 
  {"Afternoon", {MAX_LIGHT, MAX_LIGHT, MAX_LIGHT}, FullSpec, 12}, 
  {"Dusk", {TWILIGHT, 0, TWILIGHT}, M2Custom, 20},
  {"Night", {0, 0, 0}, M1Custom, 22}
};

//Our global variables are all part of an instance of the World structure, for neatness.
struct World {
  byte time_of_day; //we use this as the index to period_list as we cycle through the day
  Period current_time;
  // global variables for display, used in draw()
  char time[20];
  float temp;
  char brightness[3];

  World():
  time_of_day(PERIOD_COUNT - 1),
  current_time(period_list[PERIOD_COUNT - 1])
  {
    brightness[COLOR_W] = 0;
    brightness[COLOR_B] = 0;
    brightness[COLOR_GR] = 0;
  }
};

World world = World();

void setup() {
  // A2 to GND, A3 to 5V for the DS1307
  pinMode(A2, OUTPUT);
  digitalWrite(A2, LOW);
  pinMode(A3, OUTPUT);
  digitalWrite(A3, HIGH);

  //4 to GND for the IR emitter
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);

  // 6 to GND, 7 to 5V for the OLED display
  pinMode(6, OUTPUT);
  digitalWrite(6, LOW);
  pinMode(7, OUTPUT);
  digitalWrite(7, HIGH);

  Wire.begin();
  U8G.begin();
  Sensors.begin();
  Sensors.requestTemperatures(); // get the initial 85 result out of the way
  Serial.begin(9600);

  Serial.print(F("Free RAM: "));
  Serial.println(freeRam());

  setSyncProvider(RTC.get); //reference our syncProvider function instead of RTC_DS1307::get()
  Sensors.requestTemperatures(); //get an initial reading

  int now_h = hour();
  
  //iterate through our time periods and create an alarm for each
  for (byte i = 0; i < PERIOD_COUNT; i++) {
      Alarm.alarmRepeat(period_list[i].start_hour, 0, 0, next_period);
      if (now_h >= period_list[i].start_hour) {
        world.time_of_day = i;
        world.current_time = period_list[i]; 
      }
  }

  //Alarm.timerRepeat(3600, default_light); //make sure the light is correct every hour or so
  Alarm.timerRepeat(20, do_update);
  Alarm.timerRepeat(60, do_light); 
  
  do_update(); //start us off
}


void loop() {
  if (Serial.available() > 0) {
    Alarm.delay(5); //Wait for transmission to finish
    TestCodes(SerialReadInt());
  }
  U8G.firstPage();
  do {
      draw();
  } while (U8G.nextPage()); 
  Alarm.delay(1000);
}

/* *** EVENT FUNCTIONS *** */

void do_update() {
  time_t t_now = now();

  //Display text changes
  char am;
  if (isAM(t_now)) {am = 'A';}
  else {am = 'P';}
  sprintf(world.time, "%d:%02d %cM", hourFormat12(t_now), minute(t_now), am); 
  world.temp = Sensors.getTempFByIndex(0);  
  
  char b[9];
  sprintf(b, "%d/%d/%d", world.brightness[COLOR_W], world.brightness[COLOR_B], world.brightness[COLOR_GR]);
  Serial.println(b);
  Sensors.requestTemperatures(); //get ready for next time
}


void do_light() {
  Serial.println(F("Tick"));
  
  // Reset the light at a quarter to the hour. Otherwise, we do our normal tick.
  if (minute() == 45) {
    world.current_time.default_light();
  }
  else {
    //Light changes
    for (int i = COLOR_W; i <= COLOR_GR; i++) {
      if (world.brightness[i] > world.current_time.brightness[i]) {
        color_change(i, 'D');
      }
      else if (world.brightness[i] < world.current_time.brightness[i]) {
        color_change(i, 'U');
      }
      Alarm.delay(300);
    }
  }
}

/* *** UTILITY FUNCTIONS *** */

/* This won't need to exist if we can pass functions to Alarm as a pointer
void default_light() {
  world.current_time.default_light(); 
} */

void next_period() {
  world.time_of_day = (world.time_of_day + 1) % PERIOD_COUNT;
  world.current_time = period_list[world.time_of_day];
}

void color_change(int color, char dir) {
  /* Basically just a switchboard to pick the right color change function */
  if (dir == 'U') {
    switch (color) {
    case COLOR_W:
      WhiteUp();
      break;
    case COLOR_B:
      BlueUp();
      break;
    case COLOR_GR:
      GRUp();
      break;
    }
  }
  else if (dir == 'D') {
    switch (color) {
    case COLOR_W:
      WhiteDown();
      break;
    case COLOR_B:
      BlueDown();
      break;
    case COLOR_GR:
      GRDown();
      break;
    }    
  }
}

void draw() {
  U8G.setFont(u8g_font_helvR12);

  //period
  U8G.setPrintPos(0, 20);
  U8G.print(world.current_time.name);

  //time
  U8G.setPrintPos(0, 40);
  U8G.print(world.time);

  //temp
  U8G.setPrintPos(0, 60);
  U8G.print(world.temp, 1);
  char deg[3];
  sprintf(deg, "%cF", char(248));
  U8G.print(deg);

  /*
  //brightness levels
   U8G.setFont(u8g_font_helvR1);
   U8G.setPrintPos(0, 60);
   char b[9];
   
   U8G.print(b);*/
}

void SendCode (int cmd, byte numTimes) {
  // cmd = the element of the arrCode[] array that holds the IR code to be sent
  // numTimes = number of times to emit the command
  // Shift header 16 bits to left, fetch code from PROGMEM & add it
  unsigned long irCode = (codeHeader << 16) + pgm_read_word_near(arrCodes + cmd);
  for (byte i = 0; i < numTimes; i++){
    irsend.sendNEC(irCode, 32); // Send/emit code
    Alarm.delay(100);
  }
  //irsend.sendNEC(0xFFFFFFFF, 32);
}

/* *** LED/IR functions *** */

// IR Code functions, called by alarms
/* unused
 void Rose() {SendCode(2,2);}
 void Purple() {SendCode(6,2);}
 void Play() {SendCode(7,1);}
 void M3Custom() {SendCode(18,2);}
 void M4Custom() {SendCode(19,2);}
 void Storm1() {SendCode(28,2);}
 void Storm2() {SendCode(29,2);}
 void Storm3() {SendCode(30,2);}
 void Storm4() {SendCode(31,2);}
 void Moon1() {SendCode(20,2);}
 void Moon2() {  SendCode(21,2);}
 void Moon3() {  SendCode(22,2);}
 void DawnDusk() {  SendCode(23,2);}
 void Cloud1() {  SendCode(24,2);}
 void Cloud2() {  SendCode(25,2);}
 void Cloud3() {  SendCode(26,2);}
 void Cloud4() {  SendCode(27,2);}
 void PowerOnOff() {SendCode(3,1);}
 */

void M1Custom() {
  world.brightness[COLOR_W] = 0;
  world.brightness[COLOR_B] = 0;
  world.brightness[COLOR_GR] = 0;
  SendCode(16, 2);
  Serial.println(F("M1"));
}

void M2Custom() {
  world.brightness[COLOR_W] = TWILIGHT;
  world.brightness[COLOR_B] = 0;
  world.brightness[COLOR_GR] = TWILIGHT;
  SendCode(17, 2);
  Serial.println(F("M2"));
}

void White() {
  world.brightness[COLOR_W] = MAX_LIGHT;
  world.brightness[COLOR_B] = 0;
  world.brightness[COLOR_GR] = 0;
  SendCode(4, 2);
  Serial.println(F("White"));
}

void FullSpec() {
  world.brightness[COLOR_W] = MAX_LIGHT;
  world.brightness[COLOR_B] = MAX_LIGHT;
  world.brightness[COLOR_GR] = MAX_LIGHT;
  SendCode(5, 2);
  Serial.println(F("Full"));
}

void Orange() {
  world.brightness[COLOR_W] = MAX_LIGHT;
  world.brightness[COLOR_B] = 0;
  world.brightness[COLOR_GR] = MAX_LIGHT/2;
  SendCode(0, 2);
  Serial.println(F("Orange"));
}

void Blue() {
  world.brightness[COLOR_W] = MAX_LIGHT;
  world.brightness[COLOR_B] = MAX_LIGHT;
  world.brightness[COLOR_GR] = 0;
  SendCode(1, 2);
  Serial.println(F("Blue"));
}

void WhiteUp() {
  if (world.brightness[COLOR_W] < MAX_LIGHT) {
    world.brightness[COLOR_W] += 1;
  }
  SendCode(11, 1);
  Serial.println(F("WhiteUp"));
}

void WhiteDown() {
  if (world.brightness[COLOR_W] > 0) {
    world.brightness[COLOR_W] -= 1;
  }
  SendCode(15, 1);
  Serial.println(F("WhiteDown"));
}

void BlueUp() {
  if (world.brightness[COLOR_B] < MAX_LIGHT) {
    world.brightness[COLOR_B] += 1;
  }
  SendCode(10, 1);
  Serial.println(F("BlueUp"));
}

void BlueDown() {
  if (world.brightness[COLOR_B] > 0) {
    world.brightness[COLOR_B] -= 1;
  }
  SendCode(14, 1);
  Serial.println(F("BlueDown"));
}


void GRUp() {
  if (world.brightness[COLOR_GR] < MAX_LIGHT) {
    world.brightness[COLOR_GR] += 1;
  }
  SendCode(8,1); //red up
  Alarm.delay(300);
  SendCode(9,1); //green up
  Serial.println(F("GRUp"));
}

void GRDown() {
  if (world.brightness[COLOR_GR] > 0) {
    world.brightness[COLOR_GR] -= 1;
  }
  SendCode(12,1); //red down
  Alarm.delay(300);
  SendCode(13,1); //green down
  Serial.println(F("GRDown"));
}

/* Testing functions*/
int SerialReadInt() {
  // Reads first 2 bytes from serial monitor; anything more is tossed
  byte i;
  char inBytes[3];
  char * inBytesPtr = &inBytes[0];  // Pointer to first element

  for (i=0; i<2; i++)             // Only want first 2 bytes
    inBytes[i] = Serial.read();
  inBytes[i] = '\0';             // Put NULL character at the end
  while (Serial.read() >= 0)      // If anything else is there, throw it away
    ; // do nothing      
  return atoi(inBytesPtr);        // Convert to decimal
}

void TestCodes (int cmd) {
  // Handles commands sent in from the serial monitor
  if (cmd >= 1 && cmd <= 32) {
    // cmd must be 1 - 32
    SendCode(cmd-1, 1);
  }
  else { 
    printf_P(PSTR("Invalid Choice\n")); 
  }
}

int freeRam () {
  // Returns available SRAM
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}
