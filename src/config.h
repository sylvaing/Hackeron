

// Update these with values suitable for your wifi network.
const char* ssid = "YOURSSID";
const char* password = "YOURWIFIPASS";


//Home Assistant integration
// configure here your HA params for connection to MQTT server
#define BROKER_ADDR IPAddress(192,168,1,105)
#define BROKER_USERNAME     "YOURMQTTLOGIN" // replace with your credentials
#define BROKER_PASSWORD     "YOURMQTTPASSWORD"


//Unique device for HA integration
const byte deviceUniqID[] = { 0xDF, 0xBE, 0xEB, 0xFE, 0xEF, 0xF0 };

//uncomment and complete to log on syslog server
// #define SYSLOG_SERVER "192.168.1.101"
