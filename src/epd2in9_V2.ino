#include <SPI.h>
#include <Arduino.h>
#include "epd2in9_V2.h"
#include "epdpaint.h"
#include "imagedata.h"
#include "daly-bms-uart.h"

#define MIN_SOC 20.0        //  Stop charge and shutdown when this is reached
//#define MIN_VOLTAGE 24.0      Not currently implemented   
//#define MAX_DSCH_I 90.0   //  Not currently implemented
#define SAFETY_TIMER 25200000  //7h in milliseconds
#define MIN_CELL_MV 2700    //2500mV is absolute min, not yet implemented
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
#define MAX_NUMBER_OF_DATA_LOSS 10

#define RELAY_PIN CONTROLLINO_SCREW_TERMINAL_DIGITAL_OUT_07
#define AC_ENABLE CONTROLLINO_RELAY_01
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

char status_string[20];   // = {'e', 'r', 'r', 'o', 'r', '\0'};
char soc_string[10];      // = {'1', '0', '0', '%', '\0'};
char curr_string[10];     // = {'+', 'X', 'X', '.', 'X', ' ', 'A', '\0'};

char time_string[10];     // = {'0', '0', ':', '0', '0', ':', '0', '0', '\0'};
char volt_string[10];     // = {'2', '1', '.', '4', 'V', '\0'};
char min_volt_string[10]; // = {'3', '.', '2', '4', '4', 'V', '\0'};
char max_volt_string[10];
char volt_diff[10];

void error_led(int delay_time){
  int max_number_of_blinks = 100;
  digitalWrite(ERROR_LED, error_led_state);
  error_led_state = !error_led_state;
  delay(delay_time);

  // shutdown if max number of iterations is reached
  if (--max_number_of_blinks = 0){
    // stop charging (should already have been stopped anyways)
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(AC_ENABLE, LOW);
  }
}

void setup() {
  // configure the holding relay first
  pinMode(AC_ENABLE, OUTPUT);
  digitalWrite(AC_ENABLE, HIGH);

  // configure charging relay
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  // configure error LED
  pinMode(ERROR_LED, OUTPUT);
  digitalWrite(ERROR_LED, LOW);

  bms_data_drop_number = 0;

  Serial.begin(115200);
  Serial.println("Setup");

  while(epd.Init() != 0) {
    Serial.print("e-Paper init failed");
    error_led(100);
  }

  epd.ClearFrameMemory(0xFF);   // bit set = white, bit reset = black
  epd.DisplayFrame();
  delay(100);
  epd.ClearFrameMemory(0xFF);   // bit set = white, bit reset = black
  epd.DisplayFrame();

  while(!bms.Init()){
    Serial.println("BMS not connected");
    error_led(100);
    bms_data_drop_number++;
    delay(1000);
    if (bms_data_drop_number > MAX_NUMBER_OF_DATA_LOSS){
      while (1)
      {
        error_led(100);
      } 
    }
  }
  //bms_data_drop_number = 0;
  
  while(!bms.update()){
    Serial.println("BMS update fail");
    error_led(1000);
    bms_data_drop_number++;
    if (bms_data_drop_number > MAX_NUMBER_OF_DATA_LOSS){
      while (1)
      {
        error_led(100);
      } 
    }
  }
  //bms_data_drop_number = 0;

  Serial.println("Setup complete");
  
  sprintf(status_string, "setup ok");
  paint.SetRotate(ROTATE_180);
  paint.SetWidth(128);
  paint.SetHeight(64);
  paint.Clear(COLORED);
  paint.DrawStringAt(4, 4, status_string, &Font12, UNCOLORED);
  epd.SetFrameMemory_Partial(paint.GetImage(), 0, 64, paint.GetWidth(), paint.GetHeight());
  epd.DisplayFrame_Partial();
  delay(100);
  epd.SetFrameMemory_Partial(paint.GetImage(), 0, 64, paint.GetWidth(), paint.GetHeight());
  epd.DisplayFrame_Partial();
}


void loop(){
  while (!bms.update()){
    bms_data_drop_number++;
    Serial.println("BMS connection drop");
    delay(1000);

    if (bms_data_drop_number > MAX_NUMBER_OF_DATA_LOSS){
      // Turn off the relay, when the connection was dropped to often
      digitalWrite(RELAY_PIN, LOW);
      Serial.println("BMS connection error");
      sprintf(status_string, "BMS connex err");
      paint.SetRotate(ROTATE_180);
      paint.SetWidth(128);
      paint.SetHeight(64);
      paint.Clear(COLORED);
      paint.DrawStringAt(4, 4+48, status_string, &Font12, UNCOLORED);
      epd.SetFrameMemory_Partial(paint.GetImage(), 0, 64, paint.GetWidth(), paint.GetHeight());
      epd.DisplayFrame_Partial();
      delay(100);
      epd.SetFrameMemory_Partial(paint.GetImage(), 0, 64, paint.GetWidth(), paint.GetHeight());
      epd.DisplayFrame_Partial();

      while (1){
        error_led(100);
      }
      
      // bms connection error, vals cannot be trusted!
      // TODO deal with problematic vals and try to reconnect or shut down
    }
  }
  bms_data_drop_number = 0; // Reset the number of dropped msgs after one successful msg decode

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
  
  if (millis() > SAFETY_TIMER){
    //Timer ran out, shut down for safety - the car must be fully charged by this point
    digitalWrite(RELAY_PIN, LOW);
    Serial.println("[OFF] Empty");

    sprintf(status_string, "Timer");
    paint.SetRotate(ROTATE_180);
    paint.SetWidth(128);
    paint.SetHeight(64);
    paint.Clear(COLORED);
    paint.DrawStringAt(4, 4+48, status_string, &Font12, UNCOLORED);
    epd.SetFrameMemory_Partial(paint.GetImage(), 0, 64, paint.GetWidth(), paint.GetHeight());
    epd.DisplayFrame_Partial();
    delay(100);
    epd.SetFrameMemory_Partial(paint.GetImage(), 0, 64, paint.GetWidth(), paint.GetHeight());
    epd.DisplayFrame_Partial();

    while(true){
      error_led(1000);
    }
  }
  

  if ((bms.get.minCellmV < MIN_CELL_MV) || (bms.get.packSOC < MIN_SOC))
  {
    digitalWrite(RELAY_PIN, LOW);
    Serial.println("[OFF] Empty");

    sprintf(status_string, "Batt empty");
    paint.SetRotate(ROTATE_180);
    paint.SetWidth(128);
    paint.SetHeight(64);
    paint.Clear(COLORED);
    paint.DrawStringAt(4, 4+48, status_string, &Font12, UNCOLORED);
    epd.SetFrameMemory_Partial(paint.GetImage(), 0, 64, paint.GetWidth(), paint.GetHeight());
    epd.DisplayFrame_Partial();
    delay(100);
    epd.SetFrameMemory_Partial(paint.GetImage(), 0, 64, paint.GetWidth(), paint.GetHeight());
    epd.DisplayFrame_Partial();

    //delay(10000);

    while(true){
      delay(1000);
    }
  }

  if(((bms.get.maxCellmV - bms.get.minCellmV) > MAX_V_DIFF ) && (bms.get.packVoltage < 26.4)){
    // high voltage imbalance is purposefuly excluded here, as this is not due to overdischarge
    //  and will be improved by discharging
    digitalWrite(RELAY_PIN, LOW);
    Serial.println("[OFF] Vdiff too high");

    sprintf(status_string, "Vdiff=%dmV", round(bms.get.maxCellmV - bms.get.minCellmV));
    paint.SetRotate(ROTATE_180);
    paint.SetWidth(128);
    paint.SetHeight(64);
    paint.Clear(COLORED);
    paint.DrawStringAt(4, 4+48, status_string, &Font12, UNCOLORED);
    epd.SetFrameMemory_Partial(paint.GetImage(), 0, 64, paint.GetWidth(), paint.GetHeight());
    epd.DisplayFrame_Partial();
    delay(100);
    epd.SetFrameMemory_Partial(paint.GetImage(), 0, 64, paint.GetWidth(), paint.GetHeight());
    epd.DisplayFrame_Partial();

    while(true){
      error_led(1000);
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

