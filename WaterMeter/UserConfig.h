// ****************************************************************
// WATER METER READER by Craig Hoffmann
// 
// User configuration - UserConfig.h
//
// ****************************************************************


// Access Point SSID name and password
// hostname is also used for mDNS name ie  http://watermeter.local/
// and also used as the MQTT client name
#define MY_HOSTNAME "watermeter"     
#define AP_PASSWORD "wifisetup"      // Access Point Password

// Pull this pin to GND on power up to erase wifi settings & reconfigure
#define RESET_WIFI_PIN D7            

// MQTT Settings - max string lengths for server ip, username, password
#define MAX_SERVER_STR_LEN 32
#define MAX_USER_STR_LEN 32
#define MAX_PASSWORD_STR_LEN 32

// MQTT topic strings
#define meter_kLiters_topic "sensor/water-meter/kLiters"
#define meter_flowrate_topic "sensor/water-meter/FlowRate"
