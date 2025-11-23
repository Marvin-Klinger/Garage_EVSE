#include <SPI.h>
#include <Arduino.h>
#include "epd2in9_V2.h"
#include "epdpaint.h"
#include "imagedata.h"
#include "daly-bms-uart.h"

#define MIN_SOC 20.0
#define MIN_VOLTAGE 24.0
#define MAX_DSCH_I 90.0
#define SAFETY_TIMER 43200  //12h in seconds
#define MIN_CELL_MV 2700    //2500mV is absolute min
#define MAX_V_DIFF 120      //mV difference between min and max cell


// --- States for state machine  --- //
#define IDLE 1    //Ready to charge, started
#define CHARGE 3  //Relay on, SOC & Vdiff OK
#define OFF 4     //Charging temporarily suspended (low SOC but otherwise fine)
#define LOCKOUT 5 //Error, init failed, retried to often

#define COLORED     0
#define UNCOLORED   1

#define BMS_SERIAL Serial2
Daly_BMS_UART bms(BMS_SERIAL);
#define MAX_NUMBER_OF_DATA_LOSS 5

#define RELAY_PIN CONTROLLINO_SCREW_TERMINAL_DIGITAL_OUT_07
#define ERROR_LED CONTROLLINO_SCREW_TERMINAL_DIGITAL_OUT_06
char error_led_state = LOW;

#define YZERO 64
#define HEIGHT 127
#define WIDTH 255


byte bms_data_drop_number;
unsigned char image[1024];
Paint paint(image, 0, 0);    // width should be the multiple of 8 
Epd epd;
unsigned long time_start_ms;
unsigned long time_now_s;

char status_string[10];   // = {'e', 'r', 'r', 'o', 'r', '\0'};
char soc_string[10];      // = {'1', '0', '0', '%', '\0'};
char curr_string[10];     // = {'+', 'X', 'X', '.', 'X', ' ', 'A', '\0'};

char time_string[10];     // = {'0', '0', ':', '0', '0', ':', '0', '0', '\0'};
char volt_string[10];     // = {'2', '1', '.', '4', 'V', '\0'};
char min_volt_string[10]; // = {'3', '.', '2', '4', '4', 'V', '\0'};
char max_volt_string[10];
char volt_diff[10];

void error_led(int delay_time){
  digitalWrite(ERROR_LED, error_led_state);
  error_led_state = !error_led_state;
  delay(delay_time);
}

void setup() {
  bms_data_drop_number = 0;
  Serial.begin(115200);
  Serial.println("Setup");

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  pinMode(ERROR_LED, OUTPUT);
  digitalWrite(ERROR_LED, LOW);

  while(!bms.Init()){
    Serial.println("BMS not connected");
    error_led(100);
  }
  while(!bms.update()){
    Serial.println("BMS not connected");
    error_led(100);
  }

  while(epd.Init() != 0) {
    Serial.print("e-Paper init failed");
    error_led(100);
  }

  epd.ClearFrameMemory(0xFF);   // bit set = white, bit reset = black
  epd.DisplayFrame();
  epd.ClearFrameMemory(0xFF);   // bit set = white, bit reset = black
  epd.DisplayFrame();

  Serial.println("Setup complete");
}

void loop(){
  while (!bms.update()){
    bms_data_drop_number++;
    Serial.println("BMS connection drop");

    if (bms_data_drop_number > MAX_NUMBER_OF_DATA_LOSS){
      // Turn off the relay, when the connection was dropped to often
      digitalWrite(RELAY_PIN, LOW);
      Serial.println("BMS connection error");
      while (1){
        error_led(100);
      }
      
      // bms connection error, vals cannot be trusted!
      // TODO deal with problematic vals and try to reconnect or shut down
    }
  }

  sprintf(curr_string, "%d A", round(bms.get.packCurrent));
  sprintf(soc_string, "%d %%", round(bms.get.packSOC));
  sprintf(max_volt_string, "%d mV", round(bms.get.maxCellmV));
  sprintf(min_volt_string, "%d mV", round(bms.get.minCellmV));
  sprintf(volt_diff, "%d mV", round(bms.get.maxCellmV - bms.get.minCellmV));

  paint.SetRotate(ROTATE_180);
  paint.SetWidth(128);
  paint.SetHeight(64);

  paint.Clear(COLORED);
  //paint.DrawStringAt(4, 4, "status", &Font12, UNCOLORED);
  paint.DrawStringAt(4, 4, soc_string, &Font12, UNCOLORED);
  paint.DrawStringAt(4, 4+12, curr_string, &Font12, UNCOLORED);
  paint.DrawStringAt(4, 4+24, max_volt_string, &Font12, UNCOLORED);
  paint.DrawStringAt(4, 4+36, min_volt_string, &Font12, UNCOLORED);
  paint.DrawStringAt(4, 4+48, volt_diff, &Font12, UNCOLORED);

  epd.SetFrameMemory_Partial(paint.GetImage(), 0, 0, paint.GetWidth(), paint.GetHeight());
  epd.DisplayFrame_Partial();

  Serial.println("Basic BMS Data:              " + (String)bms.get.packVoltage + "V " + (String)bms.get.packCurrent + "I " + (String)bms.get.packSOC + "\% ");
  Serial.println("Package Temperature (C):     " + (String)bms.get.tempAverage);
  Serial.println("Highest Cell Voltage:        #" + (String)bms.get.maxCellVNum + " with voltage " + (String)(bms.get.maxCellmV / 1000));
  Serial.println("Lowest Cell Voltage:         #" + (String)bms.get.minCellVNum + " with voltage " + (String)(bms.get.minCellmV / 1000));
  Serial.println("Number of Cells:             " + (String)bms.get.numberOfCells);
  Serial.println("Number of Temp Sensors:      " + (String)bms.get.numOfTempSensors);
  Serial.println("BMS Chrg / Dischrg Cycles:   " + (String)bms.get.bmsCycles);
  Serial.println("BMS Heartbeat:               " + (String)bms.get.bmsHeartBeat); // cycle 0-255
  Serial.println("Discharge MOSFet Status:     " + (String)bms.get.disChargeFetState);
  Serial.println("Charge MOSFet Status:        " + (String)bms.get.chargeFetState);
  Serial.println("Remaining Capacity mAh:      " + (String)bms.get.resCapacitymAh);

  //for (size_t i = 0; i < size_t(bms.get.numberOfCells); i++)
  //{
  //  Serial.println("Cell voltage:      " + (String)bms.get.cellVmV[i]);
  //}

  if (bms.get.packSOC > 100)
  {
    Serial.println("[ERROR] Pack SOC over 100%");
  }
  

  if ((bms.get.minCellmV < MIN_CELL_MV) || (bms.get.packSOC < MIN_SOC))
  {
    digitalWrite(RELAY_PIN, LOW);
    Serial.println("[OFF] Empty");
    while(true){
      delay(100);
    }
  }

  if((bms.get.maxCellmV - bms.get.minCellmV) > MAX_V_DIFF){
    digitalWrite(RELAY_PIN, LOW);
    Serial.println("[OFF] Vdiff too high");
    while(true){
      error_led(100);
    }
  }

  //Safe area, only reached when all checks are OK
  digitalWrite(RELAY_PIN, HIGH);
  Serial.println("[ON] Relay on");
  delay(4000);
}


// epd.ClearFrameMemory(0xFF);
// epd.DisplayFrame();

// paint.SetRotate(ROTATE_0);
// paint.SetWidth(8); //needs to be multiple of 8
// paint.SetHeight(128); //needs to be multiple of 8
// paint.Clear(UNCOLORED);

// paint.DrawFilledRectangle(0, YZERO, 0, YZERO+63, COLORED);
// paint.DrawFilledRectangle(1, YZERO, 1, YZERO+20, COLORED);
// paint.DrawFilledRectangle(3, YZERO, 3, YZERO+30, COLORED);
// paint.DrawFilledRectangle(4, YZERO, 4, YZERO+33, COLORED);
// paint.DrawFilledRectangle(5, YZERO, 5, YZERO+45, COLORED);
// paint.DrawFilledRectangle(6, YZERO, 6, YZERO+45, COLORED);
// paint.DrawFilledRectangle(7, YZERO, 7, YZERO+45, COLORED);
// epd.SetFrameMemory(paint.GetImage(), HEIGHT-0, 0, paint.GetWidth(), paint.GetHeight());
// epd.DisplayFrame();

#if 0
  epd.ClearFrameMemory(0xFF);   // bit set = white, bit reset = black
  epd.DisplayFrame();
  
  paint.SetRotate(ROTATE_0);
  paint.SetWidth(128);
  paint.SetHeight(24);

  /* For simplicity, the arguments are explicit numerical coordinates */
  paint.Clear(COLORED);
  paint.DrawStringAt(0, 4, "Hello world!", &Font12, UNCOLORED);
  epd.SetFrameMemory(paint.GetImage(), 0, 10, paint.GetWidth(), paint.GetHeight());
  
  paint.Clear(UNCOLORED);
  paint.DrawStringAt(0, 4, "e-Paper Demo", &Font12, COLORED);
  epd.SetFrameMemory(paint.GetImage(), 0, 30, paint.GetWidth(), paint.GetHeight());

  paint.SetWidth(64);
  paint.SetHeight(64);
  
  paint.Clear(UNCOLORED);
  paint.DrawRectangle(0, 0, 40, 50, COLORED);
  paint.DrawLine(0, 0, 40, 50, COLORED);
  paint.DrawLine(40, 0, 0, 50, COLORED);
  epd.SetFrameMemory(paint.GetImage(), 8, 60, paint.GetWidth(), paint.GetHeight());

  paint.Clear(UNCOLORED);
  paint.DrawCircle(32, 32, 30, COLORED);
  epd.SetFrameMemory(paint.GetImage(), 56, 60, paint.GetWidth(), paint.GetHeight());

  paint.Clear(UNCOLORED);
  paint.DrawFilledRectangle(0, 0, 40, 50, COLORED);
  epd.SetFrameMemory(paint.GetImage(), 8, 130, paint.GetWidth(), paint.GetHeight());

  paint.Clear(UNCOLORED);
  paint.DrawFilledCircle(32, 32, 30, COLORED);
  epd.SetFrameMemory(paint.GetImage(), 56, 130, paint.GetWidth(), paint.GetHeight());
  epd.DisplayFrame();

  delay(2000);
#endif

#if 0
  if (epd.Init() != 0) {
      Serial.print("e-Paper init failed ");
      return;
  }

  /** 
   *  there are 2 memory areas embedded in the e-paper display
   *  and once the display is refreshed, the memory area will be auto-toggled,
   *  i.e. the next action of SetFrameMemory will set the other memory area
   *  therefore you have to set the frame memory and refresh the display twice.
   */
  epd.SetFrameMemory_Base(IMAGE_DATA);
  epd.DisplayFrame();
#endif

#if 0
  // Quick brush demo
  if (epd.Init_Fast() != 0) {
      Serial.print("e-Paper init failed ");
      return;
  }

  /** 
   *  there are 2 memory areas embedded in the e-paper display
   *  and once the display is refreshed, the memory area will be auto-toggled,
   *  i.e. the next action of SetFrameMemory will set the other memory area
   *  therefore you have to set the frame memory and refresh the display twice.
   */
  epd.SetFrameMemory_Base(IMAGE_DATA);
  epd.DisplayFrame();
  delay(2000);
#endif

#if 0
  Serial.print("show 4-gray image\r\n");
  if (epd.Init_4Gray() != 0) {
      Serial.print("e-Paper init failed ");
      return;
  }
  epd.Display4Gray(IMAGE_DATA_4Gray);
  delay(2000);
#endif

#if 0
  //if (epd.Init() != 0) {
  //    Serial.print("e-Paper init failed");
  //    return;
  //}

  epd.ClearFrameMemory(0xFF);   // bit set = white, bit reset = black
  epd.DisplayFrame();
  
  time_start_ms = millis();
  
  // put your main code here, to run repeatedly:

  for(;;){
    time_now_s = (millis() - time_start_ms) / 1000;
    time_string[0] = time_now_s / 60 / 10 + '0';
    time_string[1] = time_now_s / 60 % 10 + '0';
    time_string[3] = time_now_s % 60 / 10 + '0';
    time_string[4] = time_now_s % 60 % 10 + '0';

    paint.SetWidth(32);
    paint.SetHeight(96);
    paint.SetRotate(ROTATE_90);
  
    paint.Clear(UNCOLORED);
    paint.DrawStringAt(0, 4, time_string, &Font12, COLORED);
    epd.SetFrameMemory_Partial(paint.GetImage(), 0, 0, paint.GetWidth(), paint.GetHeight());
    epd.DisplayFrame_Partial();
  }
#endif

