#define ARDUINOHA_DEBUG

#include "config.h"
#include <Arduino.h>

#include <Syslog.h>
#include <WiFiUdp.h>
// Syslog server connection info
#define SYSLOG_PORT 514
#define SYSLOG_DEVICE_HOSTNAME "hackeron"
#define SYSLOG_APP_NAME "hackeron"

#include "NimBLEDevice.h"

#include <WiFi.h>

// for arduion-HA integration
#include <ArduinoHA.h>
//#include <ArduinoHADefines.h>


//ElegantOTA
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

#include <TaskScheduler.h>
Scheduler timeScheduler;

//SYSLOG
#ifdef SYSLOG_SERVER
  // A UDP instance to let us send and receive packets over UDP
  WiFiUDP udpClient;
  // Create a new syslog instance with LOG_KERN facility
  Syslog syslog(udpClient, SYSLOG_SERVER, SYSLOG_PORT, SYSLOG_DEVICE_HOSTNAME, SYSLOG_APP_NAME, LOG_KERN);
#endif

unsigned long previousMillis = 0;
unsigned long interval = 30000;

void setup_wifi();
void reconnect_wifi();
void setup_telnet();
void setupHaIntegration();
void cb_handleTelnet();
void cb_setupAndScan_ble();
void cb_connectBleServer();
void cb_loopHaIntegration();
void cb_loopAvaibilityMQTT();

Task taskSetup(5000,TASK_FOREVER,&setup_wifi);
Task taskReconnectWifi(interval,TASK_FOREVER,&reconnect_wifi);
Task taskTelnet(5000,TASK_FOREVER,&cb_handleTelnet);
Task taskBleSetupAndScan(30000, TASK_FOREVER, &cb_setupAndScan_ble);
Task taskConnectBleServer(50000, TASK_FOREVER, &cb_connectBleServer);
Task taskloopHaIntegration(5000, TASK_FOREVER,&cb_loopHaIntegration);
Task taskloopAvaibilityMQTT(3000, TASK_FOREVER, &cb_loopAvaibilityMQTT);


//akeron service and char UUID
// The remote service we wish to connect to.
static  BLEUUID serviceUUID("0bd51666-e7cb-469b-8e4d-2742f1ba77cc");
// The characteristic of the remote service we are interested in.
static  BLEUUID    charUUID("E7ADD780-B042-4876-AAE1-112855353CC1");

static int bleIndex = 0;
static uint8_t bleBuffer[2048] ={};
static uint8_t trame[20]={};
static uint8_t trame77[20]={};
static uint8_t trame69[20]={};
static uint8_t trame83[20]={};
static uint8_t trame65[20]={};
static uint8_t tramecmd[17]={};

static bool trameInitiale = false;

//static bool doConnect = false;
static bool connected = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static bool doConnectBle = false;

//Nimble
static BLEAdvertisedDevice* myDevice;
static boolean doScan = false;

AsyncWebServer webServer(80);

WiFiClient wifiMQTT;
WiFiClient telnet;
WiFiServer telnetServer(23);

// Now define in config.h
// byte deviceUniqID[] = { 0xDF, 0xBE, 0xEB, 0xFE, 0xEF, 0xF0 };
HADevice deviceHA;
// dernier paramètre pour le nombre de sensorMQTT à lister
HAMqtt  mqtt(wifiMQTT, deviceHA, 30);

//List of sensor for HA
HASensorNumber wifiStrength("pool_wifi_strength", HASensorNumber::PrecisionP2);
HASensor hackeronIp("pool_ip");

HASensorNumber temp("pool_temp",HASensorNumber::PrecisionP2) ; 
HASensorNumber ph("pool_ph",HASensorNumber::PrecisionP2);
HASensorNumber redox("pool_redox",HASensorNumber::PrecisionP2);
HASensorNumber sel("pool_sel",HASensorNumber::PrecisionP2);
HASensorNumber alarme("pool_alarme");
HASensorNumber alarmeRdx("pool_alarmeRdx");
HASensorNumber warning("pool_warning");
HABinarySensor pompeMoinsActive("pool_pompeMoinsActive");

HASensorNumber  phConsigne("pool_ph_consigne",HASensorNumber::PrecisionP2);
HASensorNumber  redoxConsigne("pool_redox_consigne");
HABinarySensor boostActif("pool_boostActif");

HASensorNumber  boostDuration("pool_boos_duration");

HABinarySensor pompeChlElxActive("pool_pompeChlElxActive");

HASensorNumber alarmeElx("pool_alarmeElx");

HABinarySensor pompeForcees("pool_pompeForcees");
HABinarySensor voletForce("pool_voletForce");
HABinarySensor voletActif("pool_voletActif");


HASensorNumber elx("pool_elx_value");

HANumber redoxConsigneNumber("pool_redox_consigne_number");
HANumber phConsigneNumber("pool_ph_consigne_number",HANumber::PrecisionP2);
HANumber poolProdElx("pool_prod_elx_number");

HASwitch boostFor2h("pool_boost_2h");
HASwitch volet("pool_volet");

HABinarySensor bluetoothConnected("pool_bluetooth_connected");



struct Mesure {
  float Value;
  char  Unite;
  float SeuilCalibrationMin;
  float SeuilCalibrationMax;
  float SeuilAbsolusMin;
  float SeuilAbsolusMax;
  float DefaultCalibration1;
  float DefaultCalibration2;
  float WarningMin;
  float WarningMax;
  float ErreurMin;
  float ErreurMax;

  float Consigne;
  float SeuilsErreurMax;
  float SeuilsErreurMin;

};

struct HackeronHw {
  struct Mesure ph;
  struct Mesure redox;
  struct Mesure temp;
  struct Mesure sel;
  struct Mesure elx;

  uint8_t alarme;
  uint8_t warning;
  uint8_t alarmRdx;

  bool pompeMoinsActive;
  bool pompeChlElxActive; 
  bool pompeMoins;
	bool CapteurTemp;
	bool CapteurSel;
	bool FlowSwitch;
	bool PompesForcees;
  bool VoletActif;
  bool VoletForce;
  uint8_t rawFieldA10;
  uint8_t DureeBoost;
  bool BoostActif;

  uint8_t alarmeElx;


};

struct HackeronHw hackeron;



void cb_handleTelnet() {
  if (telnetServer.hasClient()) {
    if (!telnet || !telnet.connected()) {
      if (telnet) telnet.stop();
      telnet = telnetServer.available();
    } else {
      telnetServer.available().stop();
    }
  }
}


void setup_telnet(){

  telnetServer.begin();
  telnetServer.setNoDelay(true); 

  Serial.print("Ready! Use 'telnet ");
  Serial.print(WiFi.localIP());
  Serial.println(" 23' to connect");

  timeScheduler.addTask(taskTelnet);
  taskTelnet.enable();
  Serial.println("add Task telnet handle");

}

void reconnect_wifi(){
   unsigned long currentMillis = millis();
  // if WiFi is down, try reconnecting every CHECK_WIFI_TIME seconds
  if ((WiFi.status() != WL_CONNECTED) && (currentMillis - previousMillis >=interval)) {
    Serial.print(millis());
    Serial.println("Reconnecting to WiFi...");
    WiFi.disconnect();
    WiFi.reconnect();
    #ifdef SYSLOG_SERVER
      syslog.log(LOG_INFO, "WIFI lost and reconnect automatically");
    #endif
    previousMillis = currentMillis;
  }
}

void setup_wifi() {
    // Disable this task to avoid further iterations
    taskSetup.disable();

    // We start by connecting to a WiFi network
    Serial.println();
    Serial.print("Connecting to ");

    WiFi.mode(WIFI_STA);
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    randomSeed(micros());
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    //Reconnect wifi Task
    timeScheduler.addTask(taskReconnectWifi);
    taskReconnectWifi.enable();
    Serial.print("Add task to monitor and reconnect wifi");

    //Elegant OTA
    webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Hi! I am ESP32 Hackeron to update use http://[yourIP]/update.");
    });

    AsyncElegantOTA.begin(&webServer);    // Start ElegantOTA
    webServer.begin();
    Serial.println("HTTP server started");

    //telnet setup
    setup_telnet();
    //launch handle telnet
    timeScheduler.addTask(taskTelnet);
    taskTelnet.enable();
    Serial.print("Add task to Handle telnet");

    //setup mqtt
    setupHaIntegration();
    //loop avaibility for mqtt
    timeScheduler.addTask(taskloopAvaibilityMQTT);
    taskloopAvaibilityMQTT.enable();
    telnet.print("Add task to Handle telnet");

    //ble setup
    #ifdef SYSLOG_SERVER
      syslog.log(LOG_INFO, "BLE Setup cb setupAnd Scan BLE");
    #endif
    Serial.println("BLE Setup cb setupAnd Scan BLE");
    //cb_setupAndScan_ble();
    doScan = true;
    timeScheduler.addTask(taskConnectBleServer);
    taskConnectBleServer.enable();
    // taskConnectBleServer.forceNextIteration();
    Serial.print("Add task to Connec Ble server!!!!!!");


}



uint8_t calculCRC( uint8_t table[], int count ){
  uint8_t bi = 0;
    for (int c=0 ; c<count; c++){
      bi =(uint8_t) (bi ^ table[c]);     // ^ou eclusif bit a bit  
      }
  return bi;
}

bool parseBufferReception(){
  if (bleIndex > 468){
    bleIndex = 0;
  }
  if (bleIndex < 16)
  {
    return false;
  }
  for ( int i=0; i<bleIndex; i++){
    if (bleBuffer[i] == 42 && bleBuffer[i+16]== 42){
      for (int j=0; j<17;j++ ){
        trame[j] = bleBuffer[i+j];
      }
      if (trame[15] == calculCRC(trame,15) ){
        bleIndex = 0;
        bleBuffer[0]=207;

        //copie de la trame dans un une autre trame de chaque type
        // if (trame[1] == 77){
        //  memmove(trame77, trame, sizeof(trame));
        //}
        switch (trame[1]){
          case 77:
            memmove(trame77, trame, sizeof(trame));
            break;
          case 69:
            memmove(trame69, trame, sizeof(trame));
            break;
          case 83:
            memmove(trame83, trame, sizeof(trame));
            break;
          case 65:
            memmove(trame65, trame, sizeof(trame));
            break;
        }
        return true;
      }
    }
  }
  return false;
}

uint16_t byteToDouble( uint8_t msb, uint8_t lsb){
  return msb * 256.0 + lsb;
}

bool byteToBool( uint8_t toconvert, int bit){
  uint16_t y = bit;
  uint16_t num = pow(2.0,y);

  toconvert = toconvert & (uint8_t)num;

  if (toconvert == 0){
    return false;
  }
  return true;
}

uint8_t byteSet( boolean val, int position, uint8_t cible){

  uint8_t b = 1;
  b = (uint8_t) (b << position);
  uint8_t b2 = (uint8_t) (~b);
  cible = ((!val) ? ((uint8_t)(cible & b2)) : ((uint8_t)(cible | b)));

  return cible;
}

void extractTrameM(uint8_t ltrame[]){
  //77

  //Fix pour les piques de mesures a ne pas prendre en compte
  float ph = byteToDouble(ltrame[2],ltrame[3]) / 100.0;
  if ((ph <= 9.5 ) && ( ph >= 3.5)){
    hackeron.ph.Value = ph;
  }

  //Fix pour les piques de mesures a ne pas prendre en compte
  float redox = byteToDouble(ltrame[4],ltrame[5]);
  if (( redox <= 1000) && (redox >= 350)){
    hackeron.redox.Value = redox;
  }

  //Fix pour les piques de mesures a ne pas prendre en compte
  float temp = byteToDouble(ltrame[6],ltrame[7]) / 10.0 ;
  if (( temp <= 50 ) && (temp >= 0)){
    hackeron.temp.Value = temp;
  }

  //Fix pour les piques de mesures a ne pas prendre en compte
  float sel = byteToDouble(ltrame[8],ltrame[9]) / 10.0;
  if (( sel <= 10) && (sel >= 0)){
    hackeron.sel.Value = sel;
  }

  hackeron.alarme = ltrame[10];
  hackeron.warning = ltrame[11] & 0xF;
  hackeron.alarmRdx = ltrame[11] >> 4;
	hackeron.pompeMoinsActive = byteToBool(ltrame[12], 6);
  hackeron.pompeChlElxActive = byteToBool(ltrame[12],5);
	hackeron.PompesForcees = byteToBool(ltrame[13], 7);

}

void extractTrameS(uint8_t ltrame[]){
  //83

  hackeron.ph.Consigne = byteToDouble(ltrame[2],ltrame[3]) / 100.0;
  hackeron.ph.SeuilsErreurMax  = byteToDouble(ltrame[10],ltrame[11]) / 100.0;
  hackeron.ph.SeuilsErreurMin  = byteToDouble(ltrame[12],ltrame[13]) / 100.0;

}

void extractTrameE(uint8_t ltrame[]){

  hackeron.redox.Consigne = byteToDouble(ltrame[2], ltrame[3]);
  
}

void extractTrameD(){
  //remonte les seuils erreur et warning du sel et de la températeure
  //inutile pour le moment

}
void extractTrameA(uint8_t ltrame[]){

  hackeron.FlowSwitch = byteToBool(trame[10],2);
  hackeron.alarmeElx = trame[12] & 0xF;
  hackeron.elx.Value = trame[2];

  hackeron.DureeBoost = (uint8_t)byteToDouble(ltrame[2], ltrame[3]);
  if (hackeron.DureeBoost > 0 ){
    hackeron.BoostActif = true;
  }else{
    hackeron.BoostActif = false;
  }

  hackeron.VoletActif = byteToBool(trame[10],4);
  hackeron.VoletForce = byteToBool(trame[10],3);
  hackeron.rawFieldA10 = trame[10];

}

void extractionTrame( uint8_t mnemo, uint8_t ltrame[], String appareil){

  bool doPublishMQTT = false;
  switch (mnemo){
    case 77:
      //M->dec 77
      telnet.printf("trame de type: M %d", mnemo);
      telnet.println(" ");
      Serial.println("trame de type M"); 
      extractTrameM(trame77);
      doPublishMQTT = true;
      break;
    case 69:
      //E->69
      telnet.printf("trame de type: E %d", mnemo);
      telnet.println(" ");
      Serial.println("trame de type E"); 
      extractTrameE(trame69);
      doPublishMQTT = true;
      break;
    case 83:
      //S->83
      telnet.printf("trame de type: S %d", mnemo);
      telnet.println(" ");
      Serial.println("trame de type S"); 
      extractTrameS(trame83);
      doPublishMQTT = true;
      break;
    case 65:
      //A->65
      telnet.printf("trame de type: A %d", mnemo);
      telnet.println(" ");
      Serial.println("trame de type A"); 
      extractTrameA(trame65);
      doPublishMQTT = true;
      break;
    case 68:
      //D->68
      telnet.printf("trame de type: D %d", mnemo);
      telnet.println("trame non traite !!!!");
      break;
    case 74:
      //J->74
      telnet.printf("trame de type: J %d", mnemo);
      telnet.println("trame non traite !!!!");
      break;
    case 66:
      //B->66
      telnet.printf("trame de type: B %d", mnemo);
      telnet.println("trame non traite !!!!");
      break;
  }
  if (doPublishMQTT == true ){
      telnet.println("Publish on MQTT");
      timeScheduler.addTask(taskloopHaIntegration);
      taskloopHaIntegration.enable();
      Serial.println("Add task to publish on MQTT results of notifyCallback BLE device");

  }
}

static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
//callback effectué a chaque fois que je recois une "indication BLE"
  telnet.println( "on a reçu une notification BLE ********************");
  for (int b = 0; b < length; b++) {
    bleBuffer[bleIndex++] = pData[b];
    bleBuffer[bleIndex]=207;
    if (parseBufferReception()){
      trameInitiale = true;
        extractionTrame(trame[1],trame,"akeron");
    }
  }
  Serial.println();
}

//Connect to Ble server aka akeron device
bool connectToServer() {
  Serial.print("Forming a connection to ");
  Serial.println(myDevice->toString().c_str());

  BLEClient*  pClient  = BLEDevice::createClient();
  Serial.println(" - Created client");

  // Connect to the remove BLE Server.
  pClient->connect(myDevice); 
  Serial.println(" - Connected to server");

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our service");


  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(charUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our characteristic");

  // Read the value of the characteristic.
  if(pRemoteCharacteristic->canRead()) {
    std::string value = pRemoteCharacteristic->readValue();
    Serial.print("The characteristic value was: ");
    Serial.println(value.c_str());
  }

  //il faut s'abonner la la charactisitic "indication" afin de recevoir les notification de publication
  if(pRemoteCharacteristic->canIndicate())
    pRemoteCharacteristic->subscribe(false, notifyCallback);

  connected = true;
  return true;
  
}
/**
   Scan for BLE servers and find the first one that advertises the service we are looking for.
*/
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    /**
        Called for each advertising BLE server.
    */
    void onResult(BLEAdvertisedDevice* advertisedDevice) {
      Serial.println("BLE Advertised Device found: ");
      Serial.println(advertisedDevice->toString().c_str());
      telnet.println("BLE Advertised Device found: ");
      telnet.println(advertisedDevice->toString().c_str());

      // We have found a device, let us now see if it contains the service we are looking for.
      if (advertisedDevice->haveServiceUUID() && advertisedDevice->getServiceUUID().equals(serviceUUID)) {

        BLEDevice::getScan()->stop();
        myDevice = advertisedDevice; /** Just save the reference now, no need to copy the object */
        
        Serial.print("Found our device!  address: ");

        doConnectBle = true;
        doScan = false;

        // timeScheduler.addTask(taskConnectBleServer);
        taskConnectBleServer.forceNextIteration();
        Serial.println("add Task Connect Ble server");

      } // Found our server
    } // onResult
}; // MyAdvertisedDeviceCallbacks


uint8_t *Ask(uint8_t ask){
  //composition des trames d'interrogations.
  static uint8_t obj[6] = {42,82,63,0,255,42};
  obj[3] = ask;
  //4 = lenth(6)-2
  obj[4] = calculCRC(obj,4);    
  return obj;
}

bool writeOnBle( uint8_t *trame, int size){

    trame[size-2] = calculCRC(trame, size-2);

    bool writeResponse;
    writeResponse = pRemoteCharacteristic->writeValue(trame, size, true);
    
    return writeResponse;
}


void commandePh(float consigne){
  //commande de type trame S=83

  telnet.print("la consigne PH : ");
  telnet.print(consigne);
  telnet.println("");
  tramecmd[0] = 42;
  tramecmd[1] = 83;
  for ( int i=2 ; i < 16 ; i++) {
    tramecmd[i]=255;
  }

  uint16_t consigneTmp = round(consigne * 100.0);
  
  telnet.print("la consigneTEMP PH : ");
  telnet.print(consigne);
  telnet.println("");

  tramecmd[2]= (consigneTmp >> 8);
  tramecmd[3]= consigneTmp & 0xFFu;

  tramecmd[16] = 42;

  //ensuite écrire sur le bluetooth tramecmd
  writeOnBle(tramecmd, 17);

  //on relance une interrogation du hackeron afin de mettre a jour les valeurs
  taskConnectBleServer.forceNextIteration();
}

void commandeRedox(uint16_t consigne){
  //commande de type trame E=69

  telnet.println("Dans la commande REdox");
  telnet.print("la consigne : ");
  telnet.print(consigne);
  telnet.println("");
  tramecmd[0] = 42;
  tramecmd[1] = 69;
  for ( int i=2 ; i < 16 ; i++) {
    tramecmd[i]=255;
  }

  tramecmd[2]= (consigne >> 8);
  tramecmd[3]= consigne & 0xFFu;
  tramecmd[16] = 42;

  //ensuite écrire sur le bluetooth tramecmd
  writeOnBle(tramecmd, 17);

  //on relance une interrogation du hackeron afin de mettre a jour les valeurs
  taskConnectBleServer.forceNextIteration();
}

void commandeElx(uint8_t consigne){
  // commande de type trame A=65
  telnet.println("Dans la commande Prod ELX");
  telnet.print("la consigne : ");
  telnet.print(consigne);
  telnet.println("");
  tramecmd[0] = 42;
  tramecmd[1] = 65;
  for ( int i=2 ; i < 16 ; i++) {
    tramecmd[i]=255;
  }

  tramecmd[2]= consigne;
  
  tramecmd[16] = 42;
   
  //ensuite écrire sur le bluetooth tramecmd
  writeOnBle(tramecmd, 17);

  //on relance une interrogation du hackeron afin de mettre a jour les valeurs
  taskConnectBleServer.forceNextIteration();
}

void commandeBoost(uint16_t consigne){
  // commande de type trame A=65
  telnet.println("Dans la commande Boost");
  telnet.print("la consigne : ");
  telnet.print(consigne);
  telnet.println("");
  tramecmd[0] = 42;
  tramecmd[1] = 65;
  for ( int i=2 ; i < 16 ; i++) {
    tramecmd[i]=255;
  }
  tramecmd[3]= (consigne >> 8);
  tramecmd[4]= consigne & 0xFFu;
  tramecmd[16] = 42;

  //ensuite écrire sur le bluetooth tramecmd
  writeOnBle(tramecmd, 17);
  
  //on relance une interrogation du hackeron afin de mettre a jour les valeurs
  taskConnectBleServer.forceNextIteration();

}

void commandeVolet( bool state){
    // commande de type trame A=65
  telnet.println("Dans la commande Volet");
  telnet.print("etat demande Volet : ");
  telnet.print(state);
  telnet.println("");
  tramecmd[0] = 42;
  tramecmd[1] = 65;
  for ( int i=2 ; i < 16 ; i++) {
    tramecmd[i]=255;
  }

  tramecmd[10] = byteSet(state, 3, hackeron.rawFieldA10);

  //ensuite écrire sur le bluetooth tramecmd
  writeOnBle(tramecmd, 17);
  
  //on relance une interrogation du hackeron afin de mettre a jour les valeurs
  taskConnectBleServer.forceNextIteration();


}

void cb_connectBleServer(){
  //connection au serveur Ble
  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
  // connected we set the connected flag to be true.
    Serial.println("connectBleServer");
    #ifdef SYSLOG_SERVER
      syslog.log(LOG_INFO, "connectBleServer");
    #endif


  if (connected == true ){
    Serial.println("connected = true");

    // on fait les requêtes aux serveur Bluetoth, qui va ensuite nous répondre via une "indication"
    // sur le le callback bluetooth
    bool rep;
    telnet.println("Write BLE 77");
    #ifdef SYSLOG_SERVER
      syslog.log(LOG_INFO, "Write BLE M=77");
    #endif
    rep = writeOnBle( Ask((uint8_t)77), sizeof(trame77));
    delay(500); // delay between write to ask on hackeron device
    //Interoger le serveur sur S=83
    telnet.println("Write BLE 83");
    #ifdef SYSLOG_SERVER
      syslog.log(LOG_INFO, "Write BLE S=83");
    #endif
    rep = writeOnBle( Ask((uint8_t)83), sizeof(trame83) );
    delay(500);
    //Interoger le serveur sur A=65
    telnet.println("Write BLE 65");
    #ifdef SYSLOG_SERVER
      syslog.log(LOG_INFO, "Write BLE A=65");
    #endif
    rep = writeOnBle( Ask((uint8_t)65), sizeof(trame65) );
    delay(500);
    //Interoger le serveur sur E=69
    telnet.println("Write BLE 69");
    #ifdef SYSLOG_SERVER
      syslog.log(LOG_INFO, "Write BLE E=69");
    #endif
    rep = writeOnBle( Ask((uint8_t)69), sizeof(trame69) );
    delay(500);

    #ifdef SYSLOG_SERVER
      syslog.log(LOG_INFO, "End of write for question for Akeron");
    #endif

    if (rep == false){
      connected = false;
      doConnectBle = true;
    }
  }else{
    if (doConnectBle == true) {
      Serial.println("doConnectBle is true");
      #ifdef SYSLOG_SERVER
        syslog.log(LOG_INFO, "doConnectBle is true");
      #endif
        if (connectToServer()) {
          Serial.println("We are now connected to the BLE Server.");
          #ifdef SYSLOG_SERVER
            syslog.log(LOG_INFO, "We are now connected to the BLE Server.");
          #endif
          //write sur la prochaine itération de la task
          taskConnectBleServer.forceNextIteration();
        } else {
          Serial.println("We have failed to connect to the server; there is nothin more we will do.");
          #ifdef SYSLOG_SERVER
            syslog.log(LOG_INFO, "We have failed to connect to the server; there is nothin more we will do.");
          #endif
          doScan = true;
        }
      doConnectBle = false;
      Serial.println("doConnectBle ********************* to false ");
      #ifdef SYSLOG_SERVER
        syslog.log(LOG_INFO, "doConnectBle ********************* to false ");
      #endif
      //on relance un scan
    }
    if (doScan == true){
      Serial.println( "add Task to Scan Ble devices");
      #ifdef SYSLOG_SERVER
        syslog.log(LOG_INFO, "add Task to Scan Ble devices");
      #endif
      timeScheduler.addTask(taskBleSetupAndScan);
      taskBleSetupAndScan.enable();
    }
    Serial.println("connected = false");
    #ifdef SYSLOG_SERVER
      syslog.log(LOG_INFO, "connected = false");
    #endif
  } 
}

void cb_setupAndScan_ble() {

  #ifdef SYSLOG_SERVER
    syslog.log(LOG_INFO, "cb setupAnd Scan BLE");
  #endif

  taskBleSetupAndScan.disable();
  BLEDevice::init("");

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 10 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(10, false);
  Serial.println("End Of BLE scan : any device found");
  #ifdef SYSLOG_SERVER
    syslog.log(LOG_INFO, "End Of BLE scan : any device found");
  #endif

  
}

void cb_loopAvaibilityMQTT(){
  mqtt.loop();
  deviceHA.setAvailability(true);

  //savoir si la connexion bluetooth est OK ou si le akeron n'est pas sous tension.
  bluetoothConnected.setState(connected);

}

void cb_loopHaIntegration(){

  taskloopHaIntegration.disable();
  mqtt.loop();

  deviceHA.setAvailability(true);

  wifiStrength.setValue(WiFi.RSSI());
  hackeronIp.setValue(WiFi.localIP().toString().c_str());

  //Fix pour les piques de mesures a ne pas prendre en compte
  if ((hackeron.temp.Value <= 50) && (hackeron.temp.Value >= 0)){
    temp.setValue(hackeron.temp.Value,2);
  }
  if ((hackeron.redox.Value <= 1000) && (hackeron.redox.Value >= 0)){
    redox.setValue(hackeron.redox.Value,2);
  }
  if ((hackeron.ph.Value <= 9.5) && (hackeron.ph.Value >= 3.5)){
    ph.setValue(hackeron.ph.Value);
  }
  if ((hackeron.sel.Value <= 10) && (hackeron.sel.Value >= 0)){
    sel.setValue(hackeron.sel.Value,2);
  }

  alarme.setValue((float)hackeron.alarme);
  alarmeRdx.setValue((float)hackeron.alarmRdx);
  warning.setValue((float)hackeron.warning);
  pompeMoinsActive.setState(hackeron.pompeMoinsActive);

  phConsigne.setValue(hackeron.ph.Consigne);
  redoxConsigne.setValue(hackeron.redox.Consigne);

  boostActif.setState(hackeron.BoostActif);
  boostDuration.setValue(hackeron.DureeBoost);

  pompeChlElxActive.setState(hackeron.pompeChlElxActive);
  alarmeElx.setValue(hackeron.alarmeElx);

  pompeForcees.setState(hackeron.PompesForcees);

  elx.setValue(hackeron.elx.Value);

  redoxConsigneNumber.setState(hackeron.redox.Consigne);
  phConsigneNumber.setState(hackeron.ph.Consigne);
  poolProdElx.setState(hackeron.elx.Value); //(value = consigne)
 
  voletActif.setState(hackeron.VoletActif);
  voletForce.setState(hackeron.VoletForce);
  volet.setState(hackeron.VoletForce);

}

void onValueConsigneRedoxChanged( HANumeric number, HANumber* sender){
  if (!number.isSet()) {
        // the reset command was send by Home Assistant
    } else {
        uint16_t numberUInt16 = number.toUInt16();
        telnet.println("Value of Redox changed: ");
        telnet.print(numberUInt16);
        telnet.println("");

          //Changer la conf sur le akeron->bleWrite
        commandeRedox(numberUInt16);
    }
    sender->setState(number); // report the selected option back to the HA panel
}

void onValueConsignePhChanged( HANumeric number, HANumber* sender){
  if (!number.isSet()) {
        // the reset command was send by Home Assistant
    } else {
        float numberFloat = number.toFloat();
        telnet.println("Value of Redox changed: ");
        telnet.print(numberFloat);
        telnet.println("");

         //Changer la conf sur le akeron->bleWrite
        commandePh(numberFloat);
    }
    sender->setState(number); // report the selected option back to the HA panel
  
}

void onValueProdElxChanged (HANumeric number, HANumber* sender){
    if (!number.isSet()) {
        // the reset command was send by Home Assistant
    } else {
        uint8_t numberUint_8 = number.toUInt8();
        telnet.println("Value of Redox changed: ");
        telnet.print(numberUint_8);
        telnet.println("");

         //Changer la conf sur le akeron->bleWrite
        commandeElx(numberUint_8);
    }
    sender->setState(number); // report the selected option back to the HA panel
  
}

void onStateChangedBoost2H (bool state, HASwitch* s){
  
  uint16_t duration = 0;
  if (state == true){
    //lancer le boost pour 2H
     duration = 2 * 60 ; //convert into min
  }else{
     duration = 0;
  }

  commandeBoost(duration);

}

void onStateChangedVolet (bool state, HASwitch* sender){
  
  commandeVolet(state);

}


void setupHaIntegration(){

  #ifdef SYSLOG_SERVER
    syslog.log(LOG_INFO, "Setup HA Integration");
  #endif
  //taskSetupHaIntegration.disable();

  //HA integration
  deviceHA.setUniqueId(deviceUniqID, sizeof(deviceUniqID));
  deviceHA.setName("Hackeron");
  deviceHA.setSoftwareVersion("2.0.0");
  deviceHA.setModel("regul 4 RX");
  deviceHA.setManufacturer("Isynet");
  // This method enables availability for all device types registered on the device.
  // For example, if you have 5 sensors on the same device, you can enable
  // shared availability and change availability state of all sensors using
  // single method call "device.setAvailability(false|true)"
  deviceHA.enableSharedAvailability();

  // Optionally, you can enable MQTT LWT feature. If device will lose connection
  // to the broker, all device types related to it will be marked as offline in
  // the Home Assistant Panel.
  deviceHA.enableLastWill();


  wifiStrength.setName("Pool wifi Strength");
  wifiStrength.setDeviceClass("signal_strength");
  wifiStrength.setUnitOfMeasurement("dB");

  hackeronIp.setName("hackeron IP");
  hackeronIp.setIcon("mdi:ip-network");

    // HA integration List of Sensor
  temp.setName("Water temp");
  temp.setUnitOfMeasurement("°C");
  temp.setDeviceClass("temperature");
  temp.setIcon("mdi:thermometer");
  
  redox.setName("Redox");
  redox.setUnitOfMeasurement("mV");
  //redox.setDeviceClass("temperature"); //no device class
  redox.setIcon("mdi:alpha-r-box-outline");

  ph.setName("PH");
  ph.setIcon("mdi:ph");
  ph.setUnitOfMeasurement("ph");


  sel.setName("Sel");
  sel.setUnitOfMeasurement("g");
  sel.setIcon("mdi:alpha-s-box-outline");

  alarme.setName("Alarme");
  alarme.setIcon("mdi:alpha-a-box-outline");

  alarmeRdx.setName("Alarme Rdox");
  alarmeRdx.setIcon("mdi:alpha-a-box-outline");

  warning.setName("Warning");
  warning.setIcon("mdi:alpha-w-box-outline");

  pompeMoinsActive.setName("Pompe Ph-");

  phConsigne.setName("PH Consigne");
  phConsigne.setIcon("mdi:ph");
  phConsigne.setUnitOfMeasurement("ph");


  redoxConsigne.setName("Redox Consigne");
  redoxConsigne.setUnitOfMeasurement("mV");
  redoxConsigne.setIcon("mdi:alpha-r-box-outline");
  
  boostActif.setName("Boost");

  boostDuration.setName("Boost Duree");
  boostDuration.setUnitOfMeasurement("s");
  boostDuration.setIcon("mdi:alpha-b-box-outline");
  

  pompeChlElxActive.setName("Elx Active");
  
  alarmeElx.setName("Alarme Elx");
  alarmeElx.setIcon("mdi:alpha-a-box-outline");

  pompeForcees.setName("Pompes forcees");

  elx.setName("Elx");
  elx.setUnitOfMeasurement("%");
  elx.setIcon("mdi:electron-framework");

  redoxConsigneNumber.setName("Consigne Redox");
  redoxConsigneNumber.setIcon("mdi:alpha-r-box-outline");
  redoxConsigneNumber.setStep(10);
  redoxConsigneNumber.setMin(400);
  redoxConsigneNumber.setMax(1100);
  redoxConsigneNumber.setUnitOfMeasurement("mV");
  redoxConsigneNumber.onCommand(onValueConsigneRedoxChanged);

  phConsigneNumber.setName("Consigne PH");
  phConsigneNumber.setIcon("mdi:ph");
  phConsigneNumber.setStep(0.05);
  phConsigneNumber.setMin(6.5);
  phConsigneNumber.setMax(7.8);
  phConsigneNumber.setUnitOfMeasurement("ph");
  phConsigneNumber.onCommand(onValueConsignePhChanged);


  poolProdElx.setName("Production Elx");
  poolProdElx.setIcon("mdi:electron-framework");
  poolProdElx.setStep(10);
  poolProdElx.setUnitOfMeasurement("%");
  poolProdElx.onCommand(onValueProdElxChanged);
  
  boostFor2h.setName("Start Boost For 2H");
  boostFor2h.setIcon("mdi:alpha-b-box-outline");
  boostFor2h.onCommand(onStateChangedBoost2H);

  bluetoothConnected.setName("Bluetooth Status");

  voletActif.setName("Volet Actif");
  voletForce.setName("Volet Force");

  volet.setName("Volet");
  volet.setIcon("mdi:window-shutter");
  volet.onCommand(onStateChangedVolet);

  mqtt.begin( BROKER_ADDR, BROKER_USERNAME, BROKER_PASSWORD );


}

void setup() {

  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");
  #define ARDUINOHA_DEBUG
  timeScheduler.init();

  timeScheduler.addTask(taskSetup);
  taskSetup.enable();
  Serial.println("add Task to setup Hackeron");

} // End of setup.

void loop() {
  //Serial.println("in the loop");
  //syslog.log(LOG_INFO, "Begin loop");
  timeScheduler.execute();

} // End of loop