#include <QueueList.h>
#include <SD.h>
#include <SPI.h>
#include <Adafruit_LSM9DS0.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>


//State Machine Definition
#define DORMANT_CRUISE  1
#define INITALIZATION   2
#define INIT_SLEEP      3
#define NORMAL_OPS      4
#define ECLIPSE         5
#define DEPLOY_ARMED    6
#define DEPLOY_VERIF    7
#define DEPLOY_DOWN_LK  8
#define LOW_POWER       9
#define SAFE_MODE      10
#define IMAGE_REQUEST  11
#define IMAGE_DOWNLINK 12
#define TEST_GYRO_DEPLOY  13

bool WireConnected = true;
bool LED_NOT_DOOR = false;
unsigned long LastTimeTime = 0;
int TimeTime = 4000;
bool ST = false;

////Constant Initialization
unsigned long cruiseEnd = 30 * 60 * 1000;
unsigned long ledLastTime = millis();
long cycle = 0;
bool ledState = LOW;
unsigned long manualTimeout = 10 * 1000;
unsigned int SlaveResets = 0;
unsigned long deployTimeOut = 30 * 1000;

//State Machine Transition Flags and Times
unsigned long eclipseEntry;
unsigned long lowPowerEntry;
unsigned long normOpEntry;
unsigned long initEntry;
unsigned long initSleepEntry;
unsigned long deployArmedEntry;
unsigned long deployVEntry;
unsigned long forceExitEclipseTime = 50 * 60 * 1000;
unsigned long forceLPEclipseTime = 180 * 60 * 1000;
unsigned long lastAccelTime;

//Threshold Values
uint8_t LightThreshold = 80; //0-100
float LV_Threshold = 3.20; //Volts
float HV_Threshold = 3.40; //Volts
float EclipseAmp_Threshold = 0.01; //10mA
int8_t LT_Threshold = -10; //C
uint8_t HT_Threshold = 60; //C
float gyroThresholdX = 3; //dps
float gyroThresholdY = 3; //dps
float gyroThresholdZ = 3; //dps
int gyroSpeed = 500;
int gyroTime = 500;

//Battery Test
bool POWER_SAVE = true;
unsigned long LastBattCheck = 0;
uint16_t BattCheckTime = 8100;
unsigned long LastSolarCheck = 0;
int SolarCheckTime = 9000;
//RockBlock Test
unsigned long lastRBCheck = 0;
long RBCheckTime = 6000;
unsigned int RBPings = 0;
unsigned long DLTime = (2 * 60 * 1000); //2 min
unsigned long lastDLTime = 0;
bool RBPingOut = false;

//IMU and Sensor Test
Adafruit_LSM9DS0 imu = Adafruit_LSM9DS0();
int imuSensorDwell = 10; //100; //Averaging Time Non-BLOCKING!
unsigned long lastIMUTime = 0;
int IMUrecords = 0;

//Slave Communication Test
long recentSlaveCom = 0;
long lastSComTime = 0;
long lastSComAttempt = 0;
int SComTime = 2237;
unsigned long SlaveResetTimeOut = 30 * 1000;

//ADCS Test
unsigned long LastSpinCheckT = 0;
unsigned long SpinCheckTime = 7010;
float OmegaThreshold = 30; //Degrees per second

//Thermal Test
long LastThermalCheck = 0;
int ThermalCheck = 6100;

//Serial Command Test
int popTime = 4000;
unsigned long lastPopTime = 0;

//RockBlock Test
bool testRDL = false;

//Commanded Action Flags
bool commandedSC = false;
bool commandedDL = false;

//Pinout Numbers
//TO DO
const int DoorSensePin = 13;
const int DoorTrig = 5;
const int BatteryPin = A0;
const int RBRx = 0; //RockBlock Serial Into FCom
const int RBTx = 1; //RockBlock Serial Out of FCom
const int RBSleep = 6;
const int RB_RI = 23;
const int RB_RTS = 22;
const int RB_CTS = 23;
const int SDApin = 20; //I2C Data
const int SCLpin = 21; //I2C Clock
//const int SolarXPlusPin = A4; //Solar Current X+
//const int SolarXMinusPin = A3; //Solar Current X-
//const int SolarYPlusPin = A2; //Solar Current Y+
//const int SolarYMinusPin = A1; //Solar Current Y-
//const int SolarZPlusPin = A5; //Solar Current Z+
const int DoorSenseGnd = A1;
const int CamEnable = A2;
const int LightSenseVcc = A3; //Light Sensor Power
const int TempSenseVcc = A4; //Temp Sensor Power
const int TempSenseGnd = A5; //Temp Sensor Ground
const int SolarPin = 9; //Solar Current Z-
const int SlaveReset = 10; //Slave Fault Handing (via Hard Reset)
const int DoorMagEnable = 11; //Allow Door Magnetorquer to work

//Image Downlink Test
#define chipSelect 4
#define DLSize 320
unsigned long lastCamTime = 0;
unsigned long CamActivatedTime = 0;
int camCheckTime = 4112;
#define WireTransferSize 32
static const char b64chars[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; //Remove before Flight //TODO


bool DA_Initialize;

class floatTuple
{
  public:
    float x;
    float y;
    float z;

    floatTuple(float a, float b, float c) {
      x = a;
      y = b;
      z = c;
    }
    void print() {
      Serial.print(x); Serial.print(F(" "));
      Serial.print(y); Serial.print(F(" "));
      Serial.print(z); Serial.println(F(" "));
    }
};

void printArray(uint8_t arr[], int s) {
    for (int i = 0; i < s; i++) {
      print_binary(arr[i], 8);
      Serial.print(" ");
    }
    Serial.println("");
}

float fmap(float x, float in_min, float in_max, float out_min, float out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}


class RAMImage {
  public:
    int finalIndex;
    String Filename;
    int sizeofimage = 0;
    int lastdataindex = 0;
    QueueList<uint8_t*> queue;
    QueueList<int> sizeQueue;


    RAMImage() {
      finalIndex = 0;
      Filename = "";
      //list = new LinkedList();
      //intlist = new LinkedListInt();
    }

    uint8_t* getArr(int index){
      int queueSize = queue.count();
      uint8_t* arr = 0;
      for(int i = 0; i < queueSize; i++){
        uint8_t* a = queue.pop();
        if(i == index){
          arr = a;
        }
        queue.push(a);
      }
      return arr;
    }

    int getInt(int index){
      int queueSize = sizeQueue.count();
      int size_of_image = 0;
      for(int i = 0; i < queueSize; i++){
        int tempSize = sizeQueue.pop();
        if(i == index){
          size_of_image = tempSize;
        }
        sizeQueue.push(tempSize);
      }
      return size_of_image;
    }
    

    void printRI() {
      delay(100);
      Serial.println("\nRAM Loaded Image: " + Filename);
      Serial.println("   Segments: " + String(queue.count()));
      Serial.println("   Total Size: " + String(photoSize()));
      Serial.println("\nBinary Form:\n");
      int queueSize = queue.count();
      for(int i = 0; i < queueSize; i++){
        uint8_t* arr= queue.pop();//returns pointer of first element of arr
        if(i == queueSize - 1){
          printArray(arr, sizeofimage-lastdataindex - 1);
        }
        else{
          printArray(arr, DLSize);
        }
        queue.push(arr);
      }
    }

    int photoSize() {
      int sum = 0;
      int queue_l = sizeQueue.count();
      int num;
      for(int i = 0; i < queue_l; i++){
         num = sizeQueue.pop();
         sum += num;
         sizeQueue.push(num);
      }
      return sum;
    }

    /**
       stores entire image data into linked list, dataSize is length of linked list
    */
    void store(uint8_t* data, int dataSize) { //parameters are pointer to data array and size of data array and pointer contains address of element 0 of data array
      //get and add methods work properly
      int dsize = dataSize;
      sizeofimage = dataSize;
      Serial.print("DSize: ");
      Serial.println(dsize);
      int index = 0;
      while (true) {
        if (dsize - DLSize > 0) {
          sizeQueue.push(DLSize);
          //intlist->add(DLSize);
          dsize -= DLSize;
          //Serial.println(sizeArray[index]);
        } else {
          sizeQueue.push(dsize);
          //intlist-> add(dsize);
          finalIndex = index;
          break;
        }
        index++;
      }
      uint8_t dataArray[DLSize];
      uint8_t* dptr = data; //d is a pointer that equals data which points to address of first element of data
      uint8_t* aptr = new uint8_t[DLSize];
      int dataIndex = 0;
      for (int i = 0; i < dataSize; i++) {
        if (((i != 0) && (i % DLSize == 0))) {
          queue.push(aptr - DLSize);
          dataIndex = i;
          aptr = new uint8_t[DLSize];
        }
        if(i == dataSize - 1){
          lastdataindex = dataIndex;
          *aptr++ = *dptr++;
          queue.push(aptr - (i - dataIndex + 1));
          break;
        }
        *aptr++ = *dptr++;
      }
    }
};

bool initializeRB(bool init = false) {
  //Serial1.print(F("AT&F0\r")); //Reset to Factory Config
  Serial1.print(F("AT&K0\r")); //Disable Flow Control
  Serial1.print(F("ATE0\r")); //Disable Echo
  Serial1.print(F("AT+SBDD2\r")); //Clear Buffers
  Serial1.print(F("AT+SBDMTA=0\r")); //Disable RING alerts
  if (init) {
    Serial1.print(F("AT&W0\r")); //Set This as default configuration
    Serial1.print(F("AT&Y0\r")); //Set This as Power Up configuration
  }
  delay(1500);
  int n = 18;
  if (!init) {
    n = 2;
  }
  Serial.print(Serial1.available());
  while (n > 0) {
    char c = (char)Serial1.read();
    Serial.print(c);
    n--;
  }
  return true;//responsePing(); //Ping to Check thats its working
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//IMU Code

floatTuple getMagData(Adafruit_LSM9DS0 imu) {
  //Returns vector of magnetometer data from Adafruit_LSM9DS0 <imu>
  floatTuple mData = floatTuple((float)imu.magData.x * (2.0 / 32768),
                                (float)imu.magData.y * (2.0 / 32768),
                                (float)imu.magData.z * (2.0 / 32768));
  return mData;
}

floatTuple getGyroData(Adafruit_LSM9DS0 imu) {
  //Returns vector of gyro data from Adafruit_LSM9DS0 <imu>
  floatTuple gData = floatTuple((float)(imu.gyroData.y * (245.0 / 32768)),
                                (float)(imu.gyroData.x * (245.0 / 32768)),
                                (float)(imu.gyroData.z * (245.0 / 32768)));
  return gData;
}

int getImuTempData(Adafruit_LSM9DS0 imu) {
  //Returns temp data from Adafruit_LSM9DS0 <imu>
  return (int)imu.temperature ;// * (1.0 / 8.0); // 8 per Degree C
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class commandBuffer {
    //Class which holds a stack of currently waiting commands which have not been processed
  public:
    int commandStack[200][2];
    int openSpot;
    commandBuffer() {
      commandStack[200][2] = { -1};
      openSpot = 0;
    }
    void print() {
      //Serial formatting and Serial output
      int i = 0;
      Serial.print(F("cBuf = ["));
      int endT = millis() + manualTimeout;
      while (i < 200 && millis() < endT) {
        if (commandStack[i][0] == -1 && commandStack[i][1] == -1) {
          break;
        }
        Serial.print(commandStack[i][0]);
        Serial.print(F(":"));
        Serial.print(commandStack[i][1]);
        Serial.print(F("|"));
        i++;
      }
      Serial.println(F("]"));
    }
};
commandBuffer cBuf;

class masterStatus {
    //Class to hold entire State of Spacecraft Operation except timers
  public:

    RAMImage imageR;
    int currentSegment;
    int RequestingImageStatus;

    bool hardwareAvTable[11];//Hardware Avaliability Table
    //[Imu, SX+,SX-,SY+, SY-, SZ+, SZ-,Temp,DoorSense,LightSense,Slave]
    //Fix PopCommand Av Swap Limit if changed

    // change final string to binary. get bytes. find upper and lower limits. round floats and set value for maxes (like MAX)
    int State;
    int NextState;
    bool ADCS_Active;

    //IMU Data Variables
    float Mag[3]; // max 0.65 gauss min -0.65 gauss
    float Gyro[3];  // max 245 dps min -245 dps
    float Accel[3]; //?
    float MagAcc[3]; //Accumulator
    float GyroAcc[3]; //Accumulator
    float AccelAcc[3]; //Accumulator
    float GyroZero[3]; //Gyro Origin
    float MagZero[3]; //Mag Origin
    int TempAcc; // max 70C min -50C
    int ImuTemp; // max ? min ? //TODO

    //Sensor Variables
    float Battery; // max 4.2V min 2.75V
    float BatteryAcc; //Accumulator
    //    float SolarXPlus; //Current in Amps
    //    float SolarXMinus; //Current in Amps
    //    float SolarYPlus; //Current in Amps
    //    float SolarYMinus; //Current in Amps
    //    float SolarZPlus; //Current in Amps
    //    float SolarZMinus; //Current in Amps
    float Solar; //Single Solar Input
    int DoorSense; //0 = Door Closed, 1 = Door Open
    float LightSense; //Sent From Slave
    float AnalogTemp; //Sent From Slave

    //Slave State Variables
    int numPhotos; //Photos Stored in Slave SD Card
    int Resets; //Number of Times Slave has been Reset
    bool CameraStatus;
    bool CameraBurst;
    unsigned long BurstDuration;

    //Deployment Variables
    int missionStatus; //0=incomplete, 1=success, 2=failure
    int deploySetting; //0=DoorSensor OR Light, 1 = Just Light, 2 = Just DoorSensor

    //ADCS State Variables
    int MResets; //Number of Times Master has been reset
    int CurXDir; //-1 or 1 for Coil Current Direction
    int CurXPWM; // 0 to 255 for Coil Current Level
    int CurYDir; //-1 or 1 for Coil Current Direction
    int CurYPWM; // 0 to 255 for Coil Current Level
    int CurZDir; //-1 or 1 for Coil Current Direction
    int CurZPWM; // 0 to 255 for Coil Current Level

    //ROCKBLOCK Message Variables
    bool AttemptingLink; //True if waiting for SBDIX to return
    bool MessageStaged; //True if message waiting in Mobile Originated Buffer
    int RBCheckType; //State of Outgoing Communication with RockBlock. 0=ping, 1=Send SBDIX, 2=Fetch Incomming Command
    int MOStatus; //0 if No Outgoing message, 1 if outgoing message success, 2 if error
    int MOMSN; //Outgoing Message #
    int MTStatus; //0 if No Incoming message, 1 if Incoming message success, 2 if error
    int MTMSN; //Incoming Message #
    int MTLength; //Incoming Message Length in bytes
    int MTQueued; //# of messages waiting in iridium
    String SBDRT;
    int LastMsgType; //0 = inval, 1 = ok, 2 = ring, 3 = error, 4 = ready //TODO
    int LastSMsgType; //Only Update on NON EMPTY reads from RB: 0 = inval, 1 = ok, 2 = ring, 3 = error, 4 = ready //TODO
    int SBDIXFails;


    masterStatus() {
      //Constructor
      State = 4; //Normal Ops //TODO
      NextState = State;
      deploySetting = 0;

      bool hardwareAvTable[10] = {true}; // Hardware Avaliability Table
      //[Imu, SX+,SX-,SY+, SY-, SZ+, SZ-,Temp,DoorSense,LightSense]

      Gyro[3] = {0};
      Mag[3] = {0};
      GyroAcc[3] = {0};
      MagAcc[3] = {0};
      GyroZero[3] = {0};
      MagZero[3] = {0}; //TODO
      TempAcc = 0;
      ImuTemp = 0;
      Battery = 3.8;
      BatteryAcc = 0;
      //      SolarXPlus = 0;
      //      SolarXMinus = 0;
      //      SolarYPlus = 0;
      //      SolarYMinus = 0;
      //      SolarZPlus = 0;
      //      SolarZMinus = 0;
      Solar = 0;
      DoorSense = 0;
      LightSense = 0;
      AnalogTemp = 0;
      numPhotos = 0;

      Resets = 0;
      missionStatus = 0;


      imageR = RAMImage();
      currentSegment = 0;
      RequestingImageStatus = 0;
      CameraBurst = false;
      BurstDuration = 15 * 1000;

      MessageStaged = false;
      AttemptingLink = false;
      RBCheckType = 0;
      MOStatus = 0;
      MOMSN = 0;
      MTStatus = 0;
      MTMSN = 0;
      MTLength = 0;
      MTQueued = 0;
      LastMsgType = 0;
      LastSMsgType = 0;
      SBDIXFails = 0;

      ADCS_Active = 1;
      MResets = 0;
      CurXDir = 0;
      CurXPWM = 0;
      CurYDir = 0;
      CurYPWM = 0;
      CurZDir = 0;
      CurZPWM = 0;

    }
    void updateSensors() {
      //Update Internal Sensor Variables
      if (hardwareAvTable[0]) {
        imu.read(); //Comment out if not working
        floatTuple M = getMagData(imu);
        floatTuple g = getGyroData(imu);
        GyroAcc[0] += (g.x - GyroZero[0]); GyroAcc[1] += (g.y - GyroZero[1]); GyroAcc[2] += (g.z - GyroZero[2]);
        MagAcc[0] += M.x; MagAcc[1] += M.y; MagAcc[2] += M.z;
        TempAcc += getImuTempData(imu);
      }
      //      SolarXPlus = getCurrentAmp(1); //X+
      //      SolarXMinus = getCurrentAmp(2); //X-
      //      SolarYPlus = getCurrentAmp(3); //Y+
      //      SolarYMinus = getCurrentAmp(4); //Y-
      //      SolarZPlus = getCurrentAmp(5); //Z+
      //      SolarZMinus = getCurrentAmp(6); //Z-
      Solar = getCurrentAmp(1); //1 Current Sensor

      BatteryAcc += 2 * fmap((float)analogRead(BatteryPin), 0.0, 1023.0, 0.0, 3.3);
      DoorSense = digitalRead(DoorSensePin);
    }

    String toString() {
      //Produces JSON Output in ASCII for Printing and Downlink
      String output = "";
      output += "{";
      output += "S:" + String(State) + ",";
      output += "PS:" + String(imageR.photoSize()) + ",";
      output += "GX:" + String(Gyro[0]) + ",GY:" + String(Gyro[1]) + ",GZ:" + String(Gyro[2]) + ",";
      output += "MX:" + String(Mag[0]) + ",MY:" + String(Mag[1]) + ",MZ:" + String(Mag[2]) + ",";
      output += "IT:" + String(ImuTemp) + ",";
      output += "AT:" + String(AnalogTemp) + ",";
      output += "B:" + String(Battery) + ",";
      //      output += "SX+:" + String(SolarXPlus) + ",SX-:" + String(SolarXMinus) +
      //                ",SY+:" + String(SolarYPlus) + ",SY-:" + String(SolarYMinus) +
      //                ",SZ+:" + String(SolarZPlus) + ",SZ-:" + String(SolarZMinus) + ",";
      output += "SL:" + String(Solar) + ",";
      output += "DS:" + String(DoorSense) + ",";
      output += "LS:" + String(LightSense) + ",";
      output += "nP:" + String(numPhotos) + ",";
      output += "IW:" + String(hardwareAvTable[0]) + ",";
      output += "SW:" + String(hardwareAvTable[10]) + ",";
      output += "Rs:" + String(Resets) + ",";
      output += "AA:" + String(ADCS_Active) + ",";
      output += "XD:" + String(CurXDir) + ",";
      output += "XP:" + String(CurXPWM) + ",";
      output += "YD:" + String(CurYDir) + ",";
      output += "YP:" + String(CurYPWM) + ",";
      output += "ZD:" + String(CurZDir) + ",";
      output += "ZP:" + String(CurZPWM) + "}";
      return output;
    }

    float roundDecimal(float num, int places) {
      int roundedNum = round(pow(10, places) * num);
      return roundedNum / ((float)(pow(10, places)));
    }

    String chop(float num, int p) {
      String s = String(num);
      if (p == 0) {
        return s.substring(0, s.indexOf('.'));
      }
      return s.substring(0, s.indexOf('.') + p + 1);
    }

    String OutputString() {
      //round floats first
      //(round(),2)
      //constrain
      //getbytes for loop
      String OutputString = "";
      OutputString += chop(constrain(roundDecimal(Gyro[0], 1), -245, 245), 1) + ",";
      OutputString += chop (constrain(roundDecimal(Gyro[1], 1), -245, 245), 1) + ",";
      OutputString += chop (constrain(roundDecimal(Gyro[2], 1), -245, 245), 1) + ",";
      OutputString += chop (constrain(roundDecimal(Mag[0], 2), -2, 2), 2) + ",";
      OutputString += chop (constrain(roundDecimal(Mag[1], 2), -2, 2), 2) + ",";
      OutputString += chop (constrain(roundDecimal(Mag[2], 2), -2, 2), 2) + ",";
      OutputString += chop (constrain(roundDecimal(ImuTemp, 0), -60, 90), 0) + ",";
      OutputString += chop (constrain(roundDecimal(AnalogTemp, 0), -60, 90), 0) + ",";
      OutputString += chop (constrain(roundDecimal(Battery, 2), 2.75, 4.2), 2) + ",";
      //      OutputString += chop (constrain(roundDecimal(1000 * SolarXPlus, 0), 0, 300), 0) + ",";
      //      OutputString += chop (constrain(roundDecimal(1000 * SolarXMinus, 0), 0, 300), 0) + ",";
      //      OutputString += chop (constrain(roundDecimal(1000 * SolarYPlus, 0), 0, 300), 0) + ",";
      //      OutputString += chop (constrain(roundDecimal(1000 * SolarYMinus, 0), 0, 300), 0) + ",";
      //      OutputString += chop (constrain(roundDecimal(1000 * SolarZPlus, 0), 0, 300), 0) + ",";
      //      OutputString += chop (constrain(roundDecimal(1000 * SolarZMinus, 0), 0, 300), 0) + ",";
      OutputString += chop (constrain(roundDecimal(1000 * Solar, 0), 0, 300), 0) + ",";
      OutputString += chop (constrain(roundDecimal(DoorSense, 2), 0, 4), 2) + ",";
      OutputString += chop (constrain(roundDecimal(LightSense, 0), 0, 100), 0) + ",";
      OutputString += chop (constrain(roundDecimal(numPhotos, 0), 0, 99), 0) + ",";
      //      String(hardwareAvTable[0]);//TODO
      //      String(ADCS_Active);
      OutputString += chop (constrain(roundDecimal(CurXDir, 0), -1, 1), 0) + ",";
      OutputString += chop (constrain(roundDecimal(CurXPWM, 2), 0, 4), 2) + ",";
      OutputString += chop (constrain(roundDecimal(CurYDir, 0), -1, 1), 0) + ",";
      OutputString += chop (constrain(roundDecimal(CurYPWM, 2), 0, 4), 2) + ",";
      OutputString += chop (constrain(roundDecimal(CurZDir, 0), -1, 1), 0) + ",";
      OutputString += chop (constrain(roundDecimal(CurZPWM, 2), 0, 4), 2);
      //      if string length less thatn max number add random symbols until it is max length

      //      for (int i = 109 - OutputString.length(); i <= (109 - OutputString.length() + 1); i++) {
      //        OutputString[i] = "#";
      //      }
      //
      //      byte DLBIN[OutputString.length()];
      //      OutputString.getBytes(DLBIN, OutputString.length());
      //      Serial.print(OutputString);
      //      for (int i = 0; i < OutputString.length() - 1; i++) {
      //        Serial.print("00");
      //        Serial.print(DLBIN[i], BIN);
      //        Serial.print(" ");
      //      }
      //Serial.println(OutputString);
      return OutputString;
    }
};
masterStatus MSH; //Declare MSH


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//Helper Functions

void initalizePinOut() {
  //Setup Master Pinout
  if (LED_NOT_DOOR) {
    pinMode(DoorSensePin, OUTPUT); //Red LED and Door Sensor (-_-)
  } else {
    pinMode(DoorSensePin, INPUT_PULLUP);
    pinMode(DoorSenseGnd, INPUT); digitalWrite(DoorSenseGnd, LOW);
  }
  pinMode(DoorTrig, OUTPUT);
  pinMode(BatteryPin, INPUT);
  pinMode(RBSleep, OUTPUT); digitalWrite(RBSleep, HIGH); //TODO
  pinMode(RB_RI, INPUT);
  pinMode(RB_RTS, INPUT);
  pinMode(RB_CTS, INPUT);
  pinMode(SolarPin, INPUT);
  pinMode(CamEnable, OUTPUT); //Enable/Disable Camera Old Solar Current Pin
  pinMode(LightSenseVcc, OUTPUT); digitalWrite(LightSenseVcc, HIGH); //LS Power
  pinMode(TempSenseVcc, OUTPUT); digitalWrite(TempSenseVcc, HIGH); //TS Power
  pinMode(TempSenseGnd, INPUT); digitalWrite(TempSenseVcc, LOW); //TS GND
  pinMode(DoorMagEnable, OUTPUT); //Allow Door Magnetorquer to work
}

float getCurrentAmp(int panel) {
  //Returns Amperage of current sensors for panel # <panel>
  //1v/A @10kOhm 8.2V/A @82kOhm
  float current;
  //  switch (panel) {
  //    case 1:
  //      if (MSH.hardwareAvTable[1]) {
  //        current = analogRead(SolarXPlusPin);
  //      } else {
  //        current = 0;
  //      } break;
  //    case 2:
  //      if (MSH.hardwareAvTable[2]) {
  //        current = analogRead(SolarXMinusPin);
  //      } else {
  //        current = 0;
  //      } break;
  //    case 3:
  //      if (MSH.hardwareAvTable[3]) {
  //        current = analogRead(SolarYPlusPin);
  //      } else {
  //        current = 0;
  //      } break;
  //    case 4:
  //      if (MSH.hardwareAvTable[4]) {
  //        current = analogRead(SolarYMinusPin);
  //      } else {
  //        current = 0;
  //      } break;
  //    case 5:
  //      if (MSH.hardwareAvTable[5]) {
  //        current = analogRead(SolarZPlusPin);
  //      } else {
  //        current = 0;
  //      } break;
  //    case 6:
  //      if (MSH.hardwareAvTable[6]) {
  //        current = analogRead(SolarZMinusPin);
  //      } else {
  //        current = 0;
  //      } break;
  //  }
  current = analogRead(SolarPin);
  current = fmap((float)current, 0.0, 1023.0, 0, .40244); //3.3V=.825A
  return current;
}

float getTotalAmperage() {
  //Check the amps from each solar panel and returns total amperage
  float TotalCurrent = 0;
  //  for (int i = 1 ; i <= 6; i++) {
  //    TotalCurrent += getCurrentAmp(i);
  //  }
  getCurrentAmp(1); //1 Current Sensor
  return TotalCurrent;
}

extern "C" char *sbrk(int i);
int freeRam () {
  //Determine Remaining RAM on Master
  char stack_dummy = 0;
  return &stack_dummy - sbrk(0);
}

volatile bool stall = true;
void waitForInterrupt() {
  stall = false;
  //noInterrupts();
}

void SensorDataCollect() { //TODO <-what is the todo for?
  //Collect Sensor Data and Average it if sufficient time has passed
  MSH.updateSensors();
  IMUrecords++;
  if (millis() - lastIMUTime > imuSensorDwell) {
    if (MSH.hardwareAvTable[0]) {
      MSH.Gyro[0] = MSH.GyroAcc[0] / ((float)IMUrecords);
      MSH.Gyro[1] = MSH.GyroAcc[1] / ((float)IMUrecords);
      MSH.Gyro[2] = MSH.GyroAcc[2] / ((float)IMUrecords);
      MSH.Mag[0] = MSH.MagAcc[0] / ((float)IMUrecords);
      MSH.Mag[1] = MSH.MagAcc[1] / ((float)IMUrecords);
      MSH.Mag[2] = MSH.MagAcc[2] / ((float)IMUrecords);
      MSH.ImuTemp = MSH.TempAcc / ((float)IMUrecords);
      MSH.TempAcc = 0;
      MSH.GyroAcc[0] = 0; MSH.GyroAcc[1] = 0; MSH.GyroAcc[2] = 0;
      MSH.MagAcc[0] = 0; MSH.MagAcc[1] = 0; MSH.MagAcc[2] = 0;
    }
    MSH.Battery = MSH.BatteryAcc / ((float)IMUrecords); MSH.BatteryAcc = 0;
    lastIMUTime = millis();
    IMUrecords = 0;
  }
}

void initializeIMU() {
  int i = 0;
  unsigned long start = millis();
  MSH.GyroAcc[0] = 0; MSH.GyroAcc[1] = 0; MSH.GyroAcc[2] = 0;
  while (millis() - start < 4000) {
    imu.read();
    floatTuple g = getGyroData(imu);
    MSH.GyroAcc[0] += g.x; MSH.GyroAcc[1] += g.y; MSH.GyroAcc[2] += g.z;
    i++;
    if (i % 50 == 0) {
      //g.print();
      Serial.print(".");
    }
  }
  MSH.GyroZero[0] = MSH.GyroAcc[0] / ((float)i);
  MSH.GyroZero[1] = MSH.GyroAcc[1] / ((float)i);
  MSH.GyroZero[2] = MSH.GyroAcc[2] / ((float)i);
  MSH.GyroAcc[0] = 0;
  MSH.GyroAcc[1] = 0;
  MSH.GyroAcc[2] = 0;
  Serial.println("\nGyro Origin: (" + String(MSH.GyroZero[0]) + "," +
                 String(MSH.GyroZero[1]) + "," +
                 String(MSH.GyroZero[2]) + ")");

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////Parser Functions

void buildBuffer(String com) {
  //Check if incoming String <com> is valid set of commands and add it to the CommandBuffer
  int commandData;
  int commandType;
  String comRemaining = com;
  bool l = true;
  while (l) {
    commandType = (com.substring(0, com.indexOf(","))).toInt();
    commandData = (com.substring(com.indexOf(",") + 1, com.indexOf("!"))).toInt();
    cBuf.commandStack[cBuf.openSpot][0] = commandType;
    cBuf.commandStack[cBuf.openSpot][1] = commandData;
    if (com.indexOf("!") == com.length() - 1) {
      l = false;
      //Serial.println(F("Finished Adding Commands"));
    } else {
      com = com.substring(com.indexOf("!") + 1);
    }
    cBuf.openSpot++;
  }
}

boolean isInputValid(String input) {
  //Check if incoming command string <input> is valid
  int lastPunc = 0; //1 if ",", 2 if "!", 0 Otherwise
  bool valid = true;
  int q = 0;
  int l = input.length();
  int endT = manualTimeout + millis();
  while (q < l) {
    char currentChar = input[q];
    q++;

    if (millis() > endT) {
      valid = false;
      break;
    }

    if (isPunct(currentChar)) {
      if (currentChar == (',')) {
        //Check if last was a period
        //Serial.println("Comma Found");
        if (lastPunc == 0 || lastPunc == 2) {
          //Serial.println("Comma OK");
          lastPunc = 1;
        } else {
          //Serial.println("2 Commas");
          valid = false;
          break;
        }
      } else if (currentChar == ('!')) {
        if (input[q - 2] == ',') {
          //Serial.println("No Second Command Number");
          valid = false;
          break;
        }
        //Serial.println("Excl Found");
        if (lastPunc == 1) {
          //Serial.println("Period ok");
          lastPunc = 2;
        } else {
          //Serial.println("2 Excl or No prior comma");
          valid = false;
          break;
        }
      } else if (currentChar == ('-')) {
        //Serial.println("Hypen Found");
        if (input[q - 2] == ',') { //q incremented after value capture
          //Serial.println("Negative Sign ok");
        } else {
          //Serial.println("Hyphen in wrong place");
          valid = false;
          break;
        }
      } else {
        //Serial.println("Invalid Punc");
        valid = false;
        break;
      }
    } else if (isAlpha(currentChar)) {
      //Serial.println("Alpha");
      valid = false;
      break;
    } else if (isSpace(currentChar)) {
      //Serial.println("Space");
      valid = false;
      break;
    }

    //Detect no ending exclamation point
    if (q == input.length() - 1) {
      if (input[q] != '!') {
        //Serial.println("No Ending");
        valid = false;
        break;
      }
    }
    //Null Character in the middle
    if (currentChar == '\0' && q != input.length() - 1) {
      valid = false;
      break;
    }
  }
  return valid;
}

void popCommands() {
  //Process all the Incoming Commands
  long start = millis();
  while (cBuf.openSpot > 0 && millis() - start < manualTimeout) {
    if (cBuf.openSpot > 0) {
      //Serial.println (cBuf.openSpot - 1);
      int currentCommand[2] = {cBuf.commandStack[cBuf.openSpot - 1][0], cBuf.commandStack[cBuf.openSpot - 1][1]};
      cBuf.commandStack[cBuf.openSpot - 1][0] = -1;
      cBuf.commandStack[cBuf.openSpot - 1][1] = -1;
      cBuf.openSpot --;

      //Supported Commands
      switch (currentCommand[0]) {
        case (91): //Arm Deployment
          MSH.NextState = DEPLOY_ARMED;
          digitalWrite(CamEnable, HIGH);
          CamActivatedTime = millis();
          DA_Initialize = true;
        case (92): //Set Deploy Timeout (seconds)
          if (currentCommand[1] * 1000 >= 2000) {
            deployTimeOut = (currentCommand[1]) * 1000;
            Serial.println(("\nDeploy Timeout set to ") + String(currentCommand[1]) + (" sec"));
          }
          break;
        case (93): //Set Manual Function Timeout (millis)
          if (currentCommand[1] >= 2000) {
            manualTimeout = (currentCommand[1]);
            Serial.println(("\nManual Function Timeout set to ") + String(currentCommand[1]) + (" ms"));
          }
          break;
        case (94): { //Toggle ADCS
            String com = "91," + String(currentCommand[1]) + "!";
            sendSCommand(com);
            break;
          }
        case (95): { //Set Master-Slave Command Time (millis)
            if (currentCommand[1] > 0) {
              SComTime = currentCommand[1];
              Serial.print(("\nSCom Time set to ") + String(currentCommand[1]) + (" ms"));
            }
            break;
          }
        case (96): //Test Force Deploy
          digitalWrite(DoorTrig, HIGH);
          Serial.println(F("\nForce Deploy"));
          break;
        case (97): //Remove before flight //TODO
          testRDL = (bool)currentCommand[1];
          if (testRDL) {
            Serial.println(F("\nRoutine Downlinks Activated"));
          } else {
            Serial.println(F("\nRoutine Downlinks Deactivated"));
          }
          break;
        case (98): //Begin Image Downlink
          if (MSH.imageR.Filename != F("N/A")) {
            Serial.println(F("\nEntering Image Downlink"));
            MSH.NextState = IMAGE_DOWNLINK;
          } else {
            Serial.println(F("\nNo Image Loaded"));
          }
          break;
        case (99): //Force Full Sleep to Reduce Overheating
          Serial.println("\nForced Thermal Sleep for " + String(currentCommand[1]) + " minutes");
          sendSCommand("92,2!");
          digitalWrite(RBSleep, LOW);
          digitalWrite(CamEnable, LOW);
          //TODO Disable Camera, mode->LOW_POWER?
          delay(60000 * currentCommand[1]);
          digitalWrite(RBSleep, HIGH);
          //Camera Off Unless needed
          delay(1000);
          //initializeRB(); //TODO store default condition in rockblock
          break;
        case (910): //Enable/Disable the Camera
          digitalWrite(CamEnable, (bool)currentCommand[1]);
          if ((bool)currentCommand[1]) {
            CamActivatedTime = millis();
          }
          break;
        case (911): //Force Routine Downlink
          if (!digitalRead(RBSleep)) { //If RB Off -> Turn it on
            Serial.println("\nRockBlock Powered On");
            digitalWrite(RBSleep, HIGH);
            delay(200);
            initializeRB(); //TODO store default condition in rockblock
          }
          commandedDL = true;
          break;
        case (912): //Set Routine Downlink Time in min
          DLTime = (60000 * currentCommand[1]);
          Serial.println("\nRoutine Downlink Time set to " + String(currentCommand[1]) + "minutes");
          break;
        case (913): //Toggle RockBlock Sleep
          if ((bool)currentCommand[1]) {
            Serial.println("\nRockBlock Powered On");
            digitalWrite(RBSleep, HIGH);
            delay(500);
            //initializeRB(); //TODO store default condition in rockblock
          } else {
            Serial.println("\nRockBlock Powered Off");
            digitalWrite(RBSleep, LOW);
          }
          break;
        case (51): //Take Photos
          digitalWrite(CamEnable, HIGH); //TODO Disable When Done
          delay(1000);
          MSH.CameraBurst = true;
          sendSCommand(F("101,1!"));
          CamActivatedTime = millis();
          Serial.println(F("\nPhotoBurst Initiated"));
          break;
        case (52): { //Set PhotoBurst Time in seconds
            String com = "102," + String(currentCommand[1]) + "!";
            MSH.BurstDuration = currentCommand[1] * 1000;
            Serial.println(F("\nPhotoBurst Time Set"));
            sendSCommand(com);
            break;
          }
        case (53): { //Get # of Available Photos
            Serial.println("\nPhotos Available: " + String(MSH.numPhotos));
            break;
          }
        case (54): //Wipe SD Card
          Serial.println(F("\nWiping SD Card"));
          sendSCommand(F("107,1!"));
          break;
        case (61): //Update Master Resets (Recieve from Slave Only)
          MSH.MResets = (currentCommand[1]);
          break;
        case (62): //Update Analog Temp (Recieve from Slave Only)
          MSH.AnalogTemp = (currentCommand[1]);
          break;
        case (63): //Update Light Sense (Recieve from Slave Only)
          MSH.LightSense = (currentCommand[1]);
          break;
        case (64): //Update X coil Direction (Recieve from Slave Only)
          MSH.CurXDir = (currentCommand[1]);
          break;
        case (65): //Update Y coil Direction (Recieve from Slave Only)
          MSH.CurYDir = (currentCommand[1]);
          break;
        case (66): //Update Z coil Direction (Recieve from Slave Only)
          MSH.CurZDir = (currentCommand[1]);
          break;
        case (67): //Update X coil PWM (Recieve from Slave Only)
          MSH.CurXPWM = (currentCommand[1]);
          break;
        case (68): //Update Y coil PWM (Recieve from Slave Only)
          MSH.CurYPWM = (currentCommand[1]);
          break;
        case (69): //Update Z coil PWM (Recieve from Slave Only)
          MSH.CurZPWM = (currentCommand[1]);
          break;
        case (610): //Update Number of Photos Stored (Recieve from Slave Only)
          MSH.numPhotos = (currentCommand[1]);
          break;
        //        case (71):
        //          MSH.MOStatus = (currentCommand[1]);
        //          break;
        //        case (72):
        //          MSH.MOMSN = (currentCommand[1]);
        //          break;
        //        case (73):
        //          MSH.MTStatus = (currentCommand[1]);
        //          break;
        //        case (74):
        //          MSH.MTMSN = (currentCommand[1]);
        //          break;
        //        case (75):
        //          MSH.MTLength = (currentCommand[1]);
        //          break;
        //        case (76):
        //          MSH.MTQueued = (currentCommand[1]);
        //          break;
        //        case (77): //???? TODO what is this
        //          MSH.SBDRT = (currentCommand[1]);
        //          break;
        case (81): { //Move Image from SD->Slave RAM->Master RAM //TODO(and Initiate Downlink)
            MSH.imageR = RAMImage();
            MSH.RequestingImageStatus = 1;
            MSH.NextState = IMAGE_REQUEST;
            String com = "103," + String(currentCommand[1]) + "!";
            sendSCommand(com);
            char filename[9];
            strcpy(filename, "A0000.JPG");
            filename[1] = '0' + currentCommand[1] / 1000;
            filename[2] = '0' + currentCommand[1] % 1000 / 100;
            filename[3] = '0' + currentCommand[1] % 1000 % 100 / 10;
            filename[4] = '0' + currentCommand[1] % 1000 % 100 % 10;
            MSH.imageR.Filename = filename;
            MSH.currentSegment = 0;
            break;
          }
        case (83): { //Testing Command: Force State to Normal Ops
            MSH.RequestingImageStatus = 0;
            MSH.NextState = NORMAL_OPS;
            //DANGER
            break;
          }
        case (111): { //Force Send SBDIX
            Serial.println("\nSent SBDIX");
            sendSBDIX(false); //Does NOT sent Attempting Link to True
            break;
          }
        case (112): { //Force Send Ping
            Serial.println("Sent AT\\r");
            Serial1.print(F("AT\r"));
            break;
          }
        case (113): { //Force Disable Flow Control
            Serial.println("Sent AT&K0\\r");
            Serial1.print(F("AT&K0\r"));
            break;
          }
        case (114): { //Force Diable RING //TODO
            //            Serial.println("Sent ");
            //            Serial1.print();
            break;
          }
        case (120):  //Toggle Broken Components in Hardware Availablity Table
          if (currentCommand[1] <= 11) {
            MSH.hardwareAvTable[currentCommand[1]] =
              !MSH.hardwareAvTable[currentCommand[1]];
          }
      }
    } else {
      //Serial.println("No Command");
    }
  }
}

void readSerialAdd2Buffer() {
  //Read Testing Commands from USB Serial
  if (Serial.available() > 0) {
    //Serial.println("Reading Testing Command");
    String comString = "";
    while (Serial.available() > 0) {
      // get the new byte:
      char inChar = (char)Serial.read();
      // add it to the inputString:
      comString += inChar;
    }
    //Serial.println("TCommand: " + comString);
    if (isInputValid(comString)) {
      buildBuffer(comString);
      popCommands();

    } else {
      Serial.println("\nInvalid Testing Command");
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//Slave Functions

/* Supported Commands Slave Can Recieved
  Send Gyro X to Slave: "11,<(String)(float)>!"
  Send Gyro Y to Slave: "12,<(String)(float)>!"
  Send Gyro Z to Slave: "13,<(String)(float)>!"
  Send Mag X to Slave: "21,<(String)(float)>!"
  Send Mag Y to Slave: "22,<(String)(float)>!"
  Send Mag Z to Slave: "23,<(String)(float)>!"
  Activate ACDS: "91,1!"
  Deactivate ACDS: "91,0!"
  Photo Burst Start: "101,1!"
  Photo Burst Duration: "101,<int>!" (Seconds NOT Millis)
*/

void sendSCommand(String data) {
  //Send Command to Slave Core
  //Serial.print("Command Sent to Slave: <");
  //Serial.println(data + ">");
  char com[data.length() + 1];
  data.toCharArray(com, data.length() + 1);
  if (WireConnected) {
    Wire.beginTransmission(11); // transmit to device #8
    Wire.write(com);   // sends String
    Wire.endTransmission();    // stop transmitting
  }
}

void sectionReadToValue(String s, int * data, int dataSize) {
  //Convert Array of Strings <s> to Array of ints <data> with size <dataSize>
  for (int i = 0; i < dataSize; i++) {
    data[i] = (s.substring(0, s.indexOf(','))).toInt();
    s = s.substring(s.indexOf(',') + 1);
  }
}

uint8_t image[6000];
int dataIndex = 0;
bool lastRead = false;
int readSize = 0;
bool requestFromSlave() {
  String res = "";
  bool success = false;
  if (WireConnected) {
    switch (MSH.RequestingImageStatus) {
      case (1): { //Get Image Size
          Wire.requestFrom(11, 8, true);
          delay(10);
          String res = "";
          while (Wire.available()) {
            res += (char)Wire.read();
          }
          readSize = res.toInt();
          Serial.println("\nIncoming Photo Size: " + String(readSize));
          if (readSize > 7) {
            MSH.RequestingImageStatus = 2;
            sendSCommand("105,1!");
          } else if (readSize == 0) {
            Serial.println("Image Didn't Exist");
            //Update HardwareAv
            MSH.RequestingImageStatus = 0;
          } else {
            Serial.println("SD Card Not Working Transfer Aborted");
            //Update HardwareAv
            MSH.RequestingImageStatus = 0;
          }
          break;
        }
      case (2): { //Get Image Data
          //Serial.print("Requesting Image Data: ");
          (Wire.requestFrom(11, WireTransferSize, true)); // request 64 bytes from slave device #8
          delay(10);
          int i = 0;
          long oldDIndex = dataIndex;
          //Serial.print(Wire.available());
          while (Wire.available()) {
            image[dataIndex] = (uint8_t)Wire.read();
            i++;
            //Serial.println(segment[dataIndex]);
            if (dataIndex >= readSize) {
              break;
            }
            dataIndex++;
          }
          //printArray(segment, dataIndex);
          //Serial.println(dataIndex);
          //Serial.println("i: "+String(i));
          Serial.print("<>");
          if (dataIndex >= readSize - 1 && dataIndex > 16) { //if (i < 16) {
            Serial.println("\nTransfer Complete");
            lastRead = true;
            //printArray(image, dataIndex);
            MSH.RequestingImageStatus = 0;
            MSH.imageR.store(image, dataIndex);
            dataIndex = 0;

            //Will reset Slave from ImageTransmit Mode

          } else {
            //Serial.println("Not Last");
          }
          break;
        }
      case (0): {
          Wire.requestFrom(11, 40, true); // request 10 bytes from slave device #8
          //delay(50);
          int endTime = millis() + manualTimeout;
          //Serial.println("Here");

          //Read and Reformat
          //  ADCS_Active;
          if (Wire.available()) {
            success = true;
          }
          String res = "";
          while (Wire.available()) {
            res += (char)Wire.read();
          }
          res = res.substring(0, res.indexOf('|'));
          if (MSH.State != DEPLOY_ARMED) {
            Serial.print("<SSV>");
            //Serial.println(": " + res);
          }
          int data[12];
          sectionReadToValue(res, data, 12);
          MSH.MResets = data[0];
          MSH.AnalogTemp = data[1];
          MSH.LightSense = data[2];
          MSH.CurXDir = data[3];
          MSH.CurYDir = data[4];
          MSH.CurZDir = data[5];
          MSH.CurXPWM = data[6];
          MSH.CurYPWM = data[7];
          MSH.CurZPWM = data[8];
          if (data[9] - MSH.numPhotos) {
            Serial.println("\n" + String(data[9] - MSH.numPhotos) + " Photos Taken");
          }
          MSH.numPhotos = data[9];
          MSH.CameraStatus = data[10];
          MSH.CameraBurst = data[11];
          break;
        }
    }
    return success;
  }
}

String buildIMUDataCommand() {
  // ex. gyro data: "11,3.653!12,2.553!13,-10!"
  String res = "";
  //Sends Info x1000
  res += "11," + String((long int)(1000 * MSH.Gyro[0])) + "!";
  res += "12," + String((long int)(1000 * MSH.Gyro[1])) + "!";
  res += "13," + String((long int)(1000 * MSH.Gyro[2])) + "!";
  res += "21," + String((long int)(1000 * MSH.Mag[0])) + "!";
  res += "22," + String((long int)(1000 * MSH.Mag[1])) + "!";
  res += "23," + String((long int)(1000 * MSH.Mag[2])) + "!";
  return res;
}

void sendIMUToSlave() {
  String SCommand = buildIMUDataCommand();
  char SComCharA[SCommand.length() + 1];
  SCommand.toCharArray(SComCharA, SCommand.length() + 1);
  sendSCommand(SComCharA);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// RockBlock Uplink/Downlink Functions

int curComL = 0;

String rocResponseRead() {
  long start = millis();
  //Serial.print(Serial1.available());
  while (!Serial1.available() && (millis() - start > 6000));
  delay(10);
  String responseString = "";

  while (Serial1.available() > 0) {
    //NEED TO DETECT \r or Command End
    responseString += (char)Serial1.read();

    if (millis() - start > 4000) {
      Serial.println(F("Com Timeout"));
      break;
    }
  }
  //Serial.print(responseString);
  return responseString;
}

bool rockOKParse() {
  delay(10);
  String input = rocResponseRead();
  Serial.println("#" + input + "#");
  bool valid = false;
  if (input[2] == 'O' && input[3] == 'K') {
    valid = true;
  }
  return valid;
}

void RBData() {
  int swnumber = 0;
  String ReceivedMessage = rocResponseRead(); //determines case

  //TODO Need to Slice if two commands recieved?

  Serial.print("<M:" + ReceivedMessage + ">");
  //Serial.print("ReceivedMessage:");
  //Serial.println(ReceivedMessage);
  int plus = ReceivedMessage.indexOf('+');
  int colon = ReceivedMessage.indexOf(':');
  int S_SBDRING = ReceivedMessage.indexOf('S');
  int E_ERROR = ReceivedMessage.indexOf('E');
  int R_ERROR = ReceivedMessage.lastIndexOf('R');
  int O_OK = ReceivedMessage.indexOf('O');
  int K_OK = ReceivedMessage.indexOf('K');
  int space = ReceivedMessage.indexOf(' ');
  int carReturn = ReceivedMessage.lastIndexOf('\r');
  int R_READY = ReceivedMessage.indexOf('R');
  int Y_READY = ReceivedMessage.lastIndexOf('Y');

  String Ring;
  String OK;
  String error;
  String nomessage;
  String invalid;
  int LengthOfMessage = ReceivedMessage.length();
  //  Serial.print("Length of Message:");
  //  Serial.println(ReceivedMessage.length());
  //  Serial.print("Substring:");
  //  Serial.println(ReceivedMessage.substring(plus, colon));

  if (ReceivedMessage.substring(plus, colon).equals(F("+SBDIX"))) {
    swnumber = 1;
  }
  else if (ReceivedMessage.substring(plus, colon).equals(F("+SBDRT"))) {
    swnumber = 2;
  }
  else if (ReceivedMessage.substring(S_SBDRING).equals(F("SBDRING"))) {
    swnumber = 3;
  }
  else if (ReceivedMessage.substring(E_ERROR, R_ERROR).equals(F("ERRO"))) {
    swnumber = 5;
  }
  else if (ReceivedMessage.substring(R_READY, Y_READY).equals(F("READ"))) {
    swnumber = 7;
  }
  else if (ReceivedMessage.length() == 0) {
    swnumber = 6;
  }
  else if (ReceivedMessage.substring(O_OK, K_OK + 1).equals(F("OK"))) {
    if (swnumber == 0) {
      swnumber = 4;
    }
  } else {
    swnumber = 0;
  }

  String DATA = ReceivedMessage.substring(colon + 1);
  int DATALength = DATA.length();
  String SBDRTDATA = DATA.substring(2, (DATALength - 6));
  int SBDRTDATALength = SBDRTDATA.length();

  int firstcomma, secondcomma, thirdcomma, fourthcomma, fifthcomma;
  String firstnumber, secondnumber, thirdnumber, fourthnumber, fifthnumber, sixthnumber;

  switch (swnumber) {
    case 1: //SBDIX command
      //Serial.println("case 1");
      firstcomma  = DATA.indexOf(',');
      secondcomma = DATA.indexOf(',', firstcomma + 1);
      thirdcomma = DATA.indexOf(',', secondcomma + 1);
      fourthcomma = DATA.indexOf(',', thirdcomma + 1);
      fifthcomma = DATA.indexOf(',', fourthcomma + 1);
      firstnumber = DATA.substring(1, firstcomma);
      //Serial.println(firstnumber);
      secondnumber = DATA.substring(firstcomma + 2, secondcomma);
      //Serial.println(secondnumber);
      thirdnumber = DATA.substring(secondcomma + 2, thirdcomma);
      //Serial.println(thirdnumber);
      fourthnumber = DATA.substring(thirdcomma + 2, fourthcomma);
      //Serial.println(fourthnumber);
      fifthnumber = DATA.substring(fourthcomma + 2, fifthcomma);
      //Serial.println(fifthnumber);
      sixthnumber = DATA.substring(fifthcomma + 2, LengthOfMessage - 17);
      // Serial.print("sixthnumber:");
      //Serial.println(sixthnumber);
      //Valid Command, Invalid -> false
      MSH.MOStatus = firstnumber.toInt();
      MSH.MOMSN = secondnumber.toInt();
      MSH.MTStatus = thirdnumber.toInt();
      MSH.MTMSN = fourthnumber.toInt();
      MSH.MTLength = fifthnumber.toInt();
      MSH.MTQueued = sixthnumber.toInt();

      //Safe to do here????
      MSH.AttemptingLink = false;
      if (MSH.MTStatus == 1) { //Message Recieved by Iridium from RB
        MSH.MessageStaged = false;
      } else {
        //Retry?
      }
      MSH.RBCheckType = 0; //Back to Idle

      switch (MSH.MOStatus) {
        case (32):
          Serial.println("No network service, unable to initiate call");
          break;
        case (33):
          Serial.println("Antenna fault, unable to initiate call");
          break;
        case (0):
          Serial.println("Message Sent Successfully");
          break;
      }

      break;

    case 2: //SBDRT command
      //Valid Command From Ground
      if (isInputValid(SBDRTDATA)) {
        buildBuffer(SBDRTDATA);
        popCommands();
      } else {
        //Invalid Uplink
        MSH.LastMsgType = 0;
      }
      break;
    case 3://SBDRING (Shouldn't be Possible)
      //Message is waiting the Buffer
      MSH.LastMsgType = 2;
      MSH.LastSMsgType = 2;
      //return "";
      break;
    case 4: //OK
      MSH.LastMsgType = 1;
      MSH.LastSMsgType = 1;
      //return "";
      break;
    case 5: // Error
      MSH.LastMsgType = 3;
      MSH.LastSMsgType = 3;
      //return "";
      break;
    case 6: // blank msg //TODO
      MSH.LastMsgType = 0;
      //return "";
      break;
    case 7: // ready
      MSH.LastMsgType = 4;
      MSH.LastSMsgType = 4;
      break;
    case 0: // invalid
      MSH.LastMsgType = 0;
      break;
      //0 = inval
      //1 = ok
      //2 = ring
      //3 = error
      //4 = ready
  }
}

int lastRBcheck = 0;

bool responsePing() {
  bool ping = false;
  Serial1.print(F("AT\r"));
  if (rockOKParse()) {
    ping = true;
  }
  return ping;
}

void sendSBDIX(bool AL) {
  Serial1.print(F("AT+SBDIX\r")); // \r?
  if (AL) {
    MSH.AttemptingLink = true;
  }
}

int downlinkSegment(int segIndex) {
  // 0 For Success, 1 for No Ready, 2 For No OK
  uint8_t* Data = MSH.imageR.getArr(segIndex); 
  int DataSize = MSH.imageR.getInt(segIndex);
  Serial.println("Beginning Downlink");

  Serial1.print("AT+SBDWB=");
  Serial1.print(DataSize);
  Serial1.print("\r");

  //  /////////////////////////////////
  //  Serial.println("Response 1 >>");
  //  while (Serial1.available()) {
  //    Serial.print((char)Serial1.read());
  //  }
  //  Serial.println("\n<<Response 1");
  //  /////////////////////////////////

  RBData();
  if (!(MSH.LastMsgType == 4)) {
    //No Ready Recieved
    return 1;
  }

  uint8_t checksum = 0;
  for (int i = 0; i < DataSize; ++i) {
    //uint8_t x = Data[i];
    Serial1.write(*Data);
    checksum += (uint8_t) (*Data);
    *Data++;
  }
  //printArray(imageBuffer[0], DataSize);
  Serial1.write(checksum >> 8);
  Serial1.write(checksum & 0xFF);
  delay(100);
  //TODO add error response

  //  /////////////////////////////////Ok?
  //  Serial.println("Response 2 >> ");
  //  while (Serial1.available()) {
  //    Serial.print((char)Serial1.read());
  //  }
  //  Serial.println("\n<< Response 2");
  //  /////////////////////////////////

  RBData();
  if (!(MSH.LastMsgType == 1)) {
    //No Ok Recieved
    return 2;
  }

  return 0;
}


void print_binary(int v, int num_places) {
  int mask = 0, n;
  for (n = 1; n <= num_places; n++) {
    mask = (mask << 1) | 0x0001;
  }
  v = v & mask;  // truncate v to specified number of places
  while (num_places) {
    if (v & (0x0001 << num_places - 1)) {
      Serial.print("1");
    } else {
      Serial.print("0");
    }
    --num_places;
  }
}

int routineDownlink() {
  //Follow with SBDIX
  //0 is success, 1 is RB error, 2 is message is too long
  String DLS = MSH.OutputString();
  if (DLS.length() < 120) {
    Serial1.print(("AT+SBDWT=" + DLS + "\r"));
    delay(400);
    if (rockOKParse()) {
      MSH.MessageStaged = true;
      MSH.MOStatus = 5; //Reset so it can go to 0 when success (5 means nothing)
      Serial.print(F("\nRDL Staged for Downlink: "));
      MSH.RBCheckType = 1; //Send Staged Message
      return 0;
    } else {
      Serial.print(F("\nRDL Failed, RB Error: "));
      return 1;
    }
  } else {
    Serial.print(F("\nRDL Failed, Message Too Long: "));
    return 2;
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*  Watchdog timer support for Arduino Zero
  by Richard Hole  December 19, 2015
*/

//setupWDT( 11 );

static void   WDTsync() {
  while (WDT->STATUS.bit.SYNCBUSY == 1); //Just wait till WDT is free
}

//============= resetWDT ===================================================== resetWDT ============
void resetWDT() {
  // reset the WDT watchdog timer.
  // this must be called before the WDT resets the system
  WDT->CLEAR.reg = 0xA5; // reset the WDT
  WDTsync();
}

//============= systemReset ================================================== systemReset ============
void systemReset() {
  // use the WDT watchdog timer to force a system reset.
  // WDT MUST be running for this to work
  WDT->CLEAR.reg = 0x00; // system reset via WDT
  WDTsync();
}

//============= setupWDT ===================================================== setupWDT ============
void setupWDT( uint8_t period) {
  // initialize the WDT watchdog timer

  WDT->CTRL.reg = 0; // disable watchdog
  WDTsync(); // sync is required

  WDT->CONFIG.reg = min(period, 11); // see Table 17-5 Timeout Period (valid values 0-11)

  WDT->CTRL.reg = WDT_CTRL_ENABLE; //enable watchdog
  WDTsync();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Stall() {
  stall = true;
  long start = millis();
  Serial.println("Stall Delay");
  if (LED_NOT_DOOR) {
    while (millis() - start < 3000) { //(stall) {
      digitalWrite(13, HIGH);
      delay(40);
      digitalWrite(13, LOW);
      delay(40);
      Serial.print(".");
    }
  } else {
    while (millis() - start < 3000) { //(stall) {
      delay(80);
      Serial.print(".");
    }
  }
}

//// Main Loop

void setup() {

  digitalWrite(12, HIGH);
  attachInterrupt(digitalPinToInterrupt(12), waitForInterrupt, LOW);
  Serial.begin(9600);
  Serial1.begin(19200);
  Wire.begin(); //Start i2c as master
  //while (!Serial);
  Stall();
  MSH = masterStatus();
  Serial.println(F("\nStarting IMU"));

  if (!imu.begin()) {
    Serial.println(F("IMU Failed"));
    MSH.hardwareAvTable[0] = false;
    Serial.println(F("IMU Initialization Failed"));
  } else {
    delay(100);
    Serial.println(F("Determining IMU Zero Point. Do not move!"));
    MSH.hardwareAvTable[0] = true;
    delay(1000);
    initializeIMU();
    Serial.println(F("IMU Initialization Successful"));
  }

  //MSH.hardwareAvTable[0] = false;
  initalizePinOut();
  digitalWrite(SlaveReset, HIGH); //Enable Slave
  delay(100);

  cBuf = commandBuffer();
  MSH.deploySetting = 1; //Just Light //TODO

  //Try to initialize and warn if we couldn't detect the chip
  int endT = millis() + manualTimeout;

  MSH.RequestingImageStatus = 0;
  MSH.State = NORMAL_OPS;
  MSH.NextState = NORMAL_OPS;


  Serial.println(F("\nInitializing RockBlock"));
  if (initializeRB(true)) {
    Serial.println(F("RockBlock Initialization Successful"));
  } else {
    Serial.println(F("RockBlock Initialization Failure"));
  }
  Serial.println(F("Setup Done\n"));
}

//Testing Variables
bool downlinkJustStaged = false;
//bool ImageDownlink_Active = true;
int Requests = 0;
long linkTime = 0;

void loop() {
  readSerialAdd2Buffer(); //Testing Command Input
  //MSH.State = IMAGE_REQUEST;
  switch (MSH.State) {
    case (777): //Stall State
      Serial.println("Idling in Stall State");
      Stall();
      MSH.NextState = NORMAL_OPS;//NORMAL_OPS;
      MSH.RequestingImageStatus = 0;
      break;
    case (IMAGE_REQUEST): {
        //Serial.println("Here");
        //Serial.println(MSH.RequestingImage);
        //delay(100);
        switch (MSH.RequestingImageStatus) {
          case (2): {
              //Serial.println("Image Data Requested");
              requestFromSlave();
              Requests++;
              if (Requests > 800) {
                Serial.println("Image Transfer Fail");
                Requests = 0;
                lastRead = false;
                MSH.RequestingImageStatus = 0;
                MSH.NextState = NORMAL_OPS;//DOWNLINK_TESTING ?
                sendSCommand("104,1!");
              }
              //delay(5);
              if (lastRead) {
                MSH.RequestingImageStatus = 0;
                Requests = 0;
                lastRead = false;
                MSH.NextState = NORMAL_OPS;//DOWNLINK_TESTING; ?
                sendSCommand("104,1!"); //TODO only send once?
                Serial.println("");
                MSH.imageR.printRI();
                //Stall();
                Serial.println("");
                break;
              }
              break;
            }
          case (1): {
              delay(10);
              requestFromSlave();
              break;
            }
          case (0): {
              //Shouldn't be here
              //Serial.println("State 0");
              MSH.NextState = NORMAL_OPS;
              break;
            }
        }
        break;
      }

    case (IMAGE_DOWNLINK):
      if (MSH.currentSegment <= MSH.imageR.finalIndex) {
        if (MSH.MOStatus == 0 || MSH.currentSegment == 0) {
          //Attempt Segment Downlink
          if (MSH.currentSegment != 0) {
            Serial.println("\nSegment Downlink Successful");
            MSH.SBDIXFails = 0;
          }
          MSH.currentSegment++;

          if (!MSH.AttemptingLink) {
            int error = downlinkSegment(MSH.currentSegment);
            if (error == 0) {
              //Success
              MSH.MOStatus = 5; //??? TODO
              downlinkJustStaged = true;
              Serial.println("\nSegment " + String(MSH.currentSegment) +
                             " Of " + String(MSH.imageR.finalIndex + 1) + " Staged");
            } else {
              //Fail
              Serial.println("\nError Staging Image: " + String(error));
              MSH.currentSegment--;
            }
          } else {
            //            Serial.print("<R2>");
            //            Serial.print("<L:" + String((linkTime - millis()) / 1000) + ">");
            //            delay(1500); //Wait to check
            //Waiting for link???
          }
        } else {
          if (downlinkJustStaged) {
            //SBDIX Call after SBDWB
            Serial.println("\nSBDIX Sent");
            sendSBDIX(true); //Attempting Link is true after this
            linkTime = millis();
            downlinkJustStaged = false;
          }

          if (millis() - lastRBCheck > RBCheckTime) {
            Serial.print("<R2>");
            Serial.print("<F:" + String(MSH.SBDIXFails) + ">");
            RBData(); //Can Reset MOStatus to 0
            lastRBCheck = millis();
            //Check if SBDIX failed
            if ((!MSH.AttemptingLink) && (MSH.MOStatus != 0)) { //SBDIX failed -> Retry
              downlinkJustStaged = true;
              Serial.print(F("\nSegment Downlink Failed. Retrying: "));
              MSH.SBDIXFails++;
              Serial.println(MSH.SBDIXFails);
            }
          }
          if (MSH.SBDIXFails > 8) { //TODO set num of fails = RBCheckTime*100 ~20min
            MSH.currentSegment--;
            Serial.println(F("\nImage Downlink Failed due to no SBDIX Link"));
            MSH.NextState = NORMAL_OPS;
          }
        }
      } else {
        MSH.NextState = NORMAL_OPS;
      }
      //TODO Timeout
      //if(timeout){
      //    MSH.currentSegment--
      //}
      break;

    case (NORMAL_OPS):

      //Collect Sensor Data
      SensorDataCollect();

      //RockBlock Communication //TODO RBSleep -> LOW
      if (millis() - lastRBCheck >= RBCheckTime) {
        //digitalWrite(RBSleep,HIGH);
        if (POWER_SAVE) {
          digitalWrite(RBSleep, HIGH);
          //          Serial1.print("AT&E0\r");//Disable Echo
          //          Serial1.print(F("AT+SBDD2\r")); //Clear Buffers
          //          Serial1.print(F("AT&K0\r")); //Disable Flow Control
          //          Serial1.print(F("AT+SBDMTA=0\r")); //Disable RING alerts
          initializeRB(); //Two Command Conflict
          delay(100);
        }
        switch (MSH.RBCheckType) {
          case (0): //Ping RockBlock to ensure Unit Functionality
            if (!MSH.AttemptingLink) {
              Serial.print("<R");
              Serial1.println("AT\r");
              Serial.print(String(rockOKParse()) + ">");
            } else {
              Serial.print("<R2>"); //Waiting for SBDIX to return
            }
            break;
          case (1): //Ping Iridium and Check for Messages, Send if any are outgoing
            Serial.println("\nSBDIX Sent");
            sendSBDIX(true);
            break;
          case (2): //Fetch Incomming Command
            Serial1.print("AT+SBDRT\r");
            MSH.RBCheckType = 0;
        }
        delay(100);
        RBData(); //Check for incoming commands
        MSH.RBCheckType = 0;
        if (RBPings >= 100) { // ~10min RBCheckTime*100
          MSH.RBCheckType = 1;
          RBPings = 0;
        }
        if (MSH.MTStatus == 1) { //Message Recieved by RB from Iridium
          MSH.RBCheckType = 2; //Prep to Fetch Data
        }
        lastRBCheck = millis();
        RBPings++;
        if (POWER_SAVE && !MSH.AttemptingLink) {
          digitalWrite(RBSleep, LOW);
          //Serial.print("<D>");
        }
      }

      //Print Camera Status for Testing
      if (millis() - lastCamTime > camCheckTime) {
        if (POWER_SAVE) {
          if (!MSH.CameraBurst && (millis() - CamActivatedTime > (MSH.BurstDuration + 2000))) { //If cam forced on, leave it on for BD+2s
            digitalWrite(CamEnable, LOW);
          }
        }
        Serial.print("<C" + String(MSH.CameraStatus) + ">");
        lastCamTime = millis();
      }

      //Routine Downlinks
      if (millis() - lastDLTime >= DLTime || commandedDL) {
        if (testRDL || commandedDL) {
          //String DLSshort = MSH.OutputString();
          //routineDownlink(DLSshort);
          routineDownlink();
          Serial.println("\n" + MSH.toString());
        }
        lastDLTime = millis();
        if (commandedDL) {
          commandedDL = false;
        }
      }

      //Test Slave Communication
      if (true) { //MSH.hardwareAvTable[10]) {
        unsigned long t = millis();
        if (millis() - lastSComAttempt >= SComTime) {
          lastSComAttempt = millis();
          Serial.print("<IMU>");
          sendIMUToSlave();
          bool SlaveResponse = requestFromSlave();
          if (SlaveResponse) {
            lastSComTime = millis(); //Reset Timeout if Com is successful
          } else {
            //No Reply From Slave
            //            if (millis() - lastSComTime > SlaveResetTimeOut) {
            //
            //              //No Communication for (SlaveResetTimeOut) ms
            //              slaveWorking = false;
            //
            //              if (!slaveWorking) {
            //                digitalWrite(SlaveReset, LOW);
            //                delay(50);
            //                digitalWrite(SlaveReset, HIGH);
            //                SlaveResets++;
            //                //delay(1000);
            //                Serial.println("Slave Reset");
            //              }
            //            } else {
            Serial.print(F("No Reply From Slave for "));
            Serial.print((millis() - lastSComTime) / 1000.0);
            Serial.println(F(" seconds"));
            //            }
          }
        }
      }

      //ADCS
      if (millis() - LastSpinCheckT > SpinCheckTime) {
        Serial.print("<G:" + String(MSH.Gyro[0], 2) + "|" +
                     String(MSH.Gyro[1], 2) + "|" +
                     String(MSH.Gyro[2], 2) + ">");
        Serial.print("<MG:" + String(MSH.Mag[0], 2) + "|" +
                     String(MSH.Mag[1], 2) + "|" +
                     String(MSH.Mag[2], 2) + ">");
        //        if (MSH.Gyro[0] > OmegaThreshold || MSH.Gyro[1] > OmegaThreshold) { //TODO
        //          sendSCommand("91,1!"); //Activate Torquers
        //          MSH.ADCS_Active = true;
        //        } else {
        //          sendSCommand("91,0!"); //Deactivate Torquers
        //          MSH.ADCS_Active = false;
        //        }
        LastSpinCheckT = millis();
      }

      //Eclipse Detection
      if (millis() - LastSolarCheck > SolarCheckTime) {
        float s = getTotalAmperage();
        Serial.print("<S:" + String(s) + ">");
        if (s < EclipseAmp_Threshold) {
          //MSH.NextState = ECLIPSE; //TODO
          sendSCommand("91,0!");
          eclipseEntry = millis();
        }
        LastSolarCheck = millis();
      }

      //Low Power Detection
      if (millis() - LastBattCheck > BattCheckTime) {
        Serial.print("<B:" + String(MSH.Battery) + ">");
        if (MSH.Battery <= LV_Threshold) {
          //MSH.NextState = LOW_POWER; //TODO
          lowPowerEntry = millis();
        }
        LastBattCheck = millis();
      }

      //Thermal Protection/Control //TODO put in other Ops?
      if (millis() - LastThermalCheck > ThermalCheck) {
        //        if (MSH.AnalogTemp > HT_Threshold) {
        //          Serial.println("\n<Warning!> Overheat Threshold Passed");
        //          sendSCommand("92,2!");
        //        } else if (MSH.AnalogTemp < LT_Threshold) {
        //          Serial.println("\n<Warning!> Low Temp Threshold Passed");
        //          sendSCommand("92,1!");
        //        } else {
        //          sendSCommand("92,0!");
        //        }
        LastThermalCheck = millis();
        Serial.print("<T" + String((int)MSH.AnalogTemp) + ">");
      }

      //      //Blinker for Testing
      //      if (millis() - ledLastTime >= 100) {
      //        if (LED_NOT_DOOR) {
      //          //unsigned long t = millis() - ledLastTime;
      //          if (ledState == LOW) {
      //            ledState = HIGH;
      //          } else {
      //            ledState = LOW;
      //          }
      //          digitalWrite(13, ledState);
      //        }
      //        Serial.print(F("."));
      //        //        Serial.print("Running: ");
      //        //        Serial.print(t);
      //        //        Serial.print(" -> Cycle Stretch of: ");
      //        //        Serial.println(t - 100);
      //        ledLastTime = millis();
      //      }

      break;

    case (DORMANT_CRUISE):
      //30 min Dormant Cruise
      if (millis() > cruiseEnd) {
        MSH.NextState = INITALIZATION;
        initEntry = millis();
      } else {
        delay(10000);
      }
      break;

    case (INITALIZATION): {
        if (millis() - initEntry < 100) {
          sendSCommand("91,1!"); //TODO Maybe more than once?
        }

        //Collect Sensor Data
        SensorDataCollect();

        //Listen to Status Reports
        if (MSH.Gyro[0] < gyroThresholdX && MSH.Gyro [1] < gyroThresholdY) {
          MSH.NextState = NORMAL_OPS;
        }

        //Check battery ->> INIT_SLEEP
        if (MSH.Battery < LV_Threshold) {
          MSH.NextState = INIT_SLEEP;
        }

        if (millis() - initEntry > (long)2700000) { //Force to Normal Ops
          //call downlink function
          //TODO
          MSH.NextState = NORMAL_OPS;
        }
        break;
      }

    case (INIT_SLEEP): {
        //Check Time
        if (millis() - initSleepEntry > (long)60 * 45 * 1000) {
          MSH.NextState = INITALIZATION;
        }
        //Check battery ->> INITALIZATION
        if (MSH.Battery > HV_Threshold) {
          MSH.NextState = INITALIZATION;
        }
      }
      break;

    case (ECLIPSE): {

        //Check Battery
        //Check Solar Current
        //Check Time
        //Magtorquers off?

        if (MSH.Battery < LV_Threshold) {
          MSH.NextState = LOW_POWER;
        }
        //Check Solar Current
        //Check Time
        float EclipseAmps = getTotalAmperage();
        if (EclipseAmps > .1 || millis() - eclipseEntry > forceExitEclipseTime) {
          MSH.NextState = NORMAL_OPS;
          normOpEntry = millis();
        } else {
          delay(8000);
        }
        break;
      }

    case (DEPLOY_ARMED): { //TODO Broken Door Sensor or Light Sensor Fallback
        SensorDataCollect();
        //Serial.print("after sensor data collect");
        if (millis() - lastSComAttempt >= 5) {
          lastSComAttempt = millis();
          //sendIMUToSlave(); //Do I need this?
          bool SlaveResponse = requestFromSlave();
          if (SlaveResponse) {
            lastSComTime = millis(); //Reset Timeout if Com is successful
          }
        }
        Serial.print("<" + String(digitalRead(DoorSensePin)) + "," + String(MSH.LightSense) + ">");
        if (DA_Initialize) {
          //if (true){
          digitalWrite(DoorTrig, HIGH); //Activate Nichrome
          DA_Initialize = false;
        }
        switch (MSH.deploySetting) {
          case (0): //Use Door OR Light
            if (((true) && (digitalRead(DoorSensePin))) ||
                ((true) && (MSH.LightSense > LightThreshold))) { //wait for door sensor
              //Door is open
              Serial.print(F("Door Release, Start Image Capture"));
              digitalWrite(DoorTrig, LOW); //deactivate nichrome wire
              delay(200); // cameratimer
              sendSCommand("101,1!"); //Trigger Camera
              MSH.NextState = DEPLOY_VERIF;
              deployVEntry = millis();
            }
            break;
          case (1): //Just Use Light
            //if ((MSH.hardwareAvTable[9]) && (MSH.LightSense > LightThreshold)) { //wait for door sensor
            if ((true) && (MSH.LightSense > LightThreshold)) {
              //Door is open
              Serial.print(F("Door Release, Start Image Capture"));
              digitalWrite(DoorTrig, LOW); //deactivate nichrome wire
              delay(0);
              sendSCommand("101,1!"); //Trigger Camera
              MSH.NextState = DEPLOY_VERIF;
              deployVEntry = millis();
            }
            break;
          case (2): //Just Use Door
            if ((true) && digitalRead(DoorSensePin)) { //wait for door sensor
              //Door is open
              Serial.print(F("Door Release, Start Image Capture"));
              digitalWrite(DoorTrig, LOW); //deactivate nichrome wire
              delay(200);
              sendSCommand("101,1!"); //Trigger Camera
              MSH.NextState = DEPLOY_VERIF;
              deployVEntry = millis();
            }
            break;
        }
        if (millis() - deployArmedEntry > (long)(60 * 3 * 1000)) {
          digitalWrite(DoorTrig, LOW);
          MSH.missionStatus = 3;
        }
        break;
      }

    case (DEPLOY_VERIF):
      Serial.print("$");
      delay(10);
      //bool SlaveResponse = requestFromSlave(); //Need inputisvalid
      lastAccelTime = millis();

      if (MSH.LightSense > LightThreshold) { //LightSensor Trigger
        MSH.missionStatus = 2;
      } else {
        //MSH.PayloadDeployed == false;
      }

      if (millis() - deployVEntry > 5000) {
        MSH.NextState = NORMAL_OPS;
      }
      break;

    case (LOW_POWER):
      digitalWrite(RBSleep, LOW);
      MSH.updateSensors();
      if (MSH.Battery >= HV_Threshold || (lowPowerEntry - millis() > ((long)(60 * 1000 * 60 * 2)))) {
        MSH.NextState = NORMAL_OPS;
        normOpEntry = millis();
        digitalWrite(RBSleep, HIGH);
        delay(4000);
        initializeRB();
      } else {
        delay(10000);
      }
      break;

   case(TEST_GYRO_DEPLOY):
      MSH.updateSensors();
      unsigned long startTime = millis();
      while(millis() - startTime <= 500){
        
      }
      int gyro_z = MSH.GyroAcc[2];
      if(abs(gyro_z) >= gyroSpeed){
        MSH.NextState = DEPLOY_ARMED;
      }
      break;
  }
  MSH.State = MSH.NextState;
  //Testing Iterators
  cycle++;
  //Blinker for Testing
  if (millis() - ledLastTime >= 100) {
    if (LED_NOT_DOOR) {
      //unsigned long t = millis() - ledLastTime;
      if (ledState == LOW) {
        ledState = HIGH;
      } else {
        ledState = LOW;
      }
      digitalWrite(13, ledState);
    }
    Serial.print(F("."));
    //        Serial.print("Running: ");
    //        Serial.print(t);
    //        Serial.print(" -> Cycle Stretch of: ");
    //        Serial.println(t - 100);
    ledLastTime = millis();
  }

  //  if (cycle % 600 == 0) {
  //    ST = true;
  //  }
  if (true && ((millis() - LastTimeTime) > TimeTime)) { //Prevent Screen Spam
    long t = millis();
    Serial.print("[" + String((millis() - LastTimeTime) / 1000.0) + "]"); //Cycle Lag
    String s = ("\n[System Time: " + String(t / (long)(60 * 60 * 1000)) + ":" +
                String(t / ((long)60 * 1000) % ((long)60 * 1000)) + ":");
    if (((t / 1000) % ((long)1000) % 60) < 10) {
      s += '0';
    }
    s += (String((t / 1000) % ((long)1000) % 60) +
          "][" + String(MSH.State) + "]");
    s += ("[" + String(freeRam() / 1024.0, 3) + "kB]");
    Serial.print(s);
    LastTimeTime = t;
    ST = false;
    cycle = 0;
  }

}


///////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////












