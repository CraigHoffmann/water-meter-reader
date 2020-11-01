// ****************************************************************
// WATER METER READER by Craig Hoffmann
// 
// Written for WeMOS D1 mini R3
//
// Do not remove this comment block
// ****************************************************************

#include <ESP8266WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#include "UserConfig.h"


// ****************************************************************
// Some defines that are used
// ****************************************************************

#define ONE_WIRE_BUS 0                         // Data wire (1 wire bus) is plugged into pin GPIO 0
#define PROCESS_PERIOD_mS (30*1000)             // how often to do some work
#define SAMPLE_PERIOD_mS 25                   // how often to sample the ADC
#define MQTT_CONN_RETRY_ms (10*1000)            // Only try to reconnect every 10 seconds
#define MQTT_PERIOD_mS (60*1000)                   // Send MQTT updates every minute
#define LED_ON_TIME_mS 30                      // How long to flash the LED on
#define MAX_CHART_POINTS 400                    // Number of readings to remember for plotting 288 * 5min
#define LED_PIN 2                              // GPIO 2 is the LED on WeMOS D1 Mini
#define CHART_LOGIC_LOW_VALUE 0
#define CHART_LOGIC_LOW_TO_HIGH_DELTA 15
#define VALID_DATA_CODE_ADDR 0                 // eeprom address used to store int value code
#define VALID_DATA_CODE ((int)12256)     // just a value used to flag if eeprom has been written before
#define SETUP_DATA_ADDR 4                      // Setup Data Structure starting address in flash/eeprom
#define ADC_BUFFER_SIZE 100


// ****************************************************************
// EEPROM data structure
// ****************************************************************

struct EEPROMDataStruct
{
  int CountsPerLiter;    // How many sensor toggles for 1 liter
  int MinThreshold;      // Sensor threshold for a high
  int MaxThreshold;     // Sensor threshold for a low
  char MQTTHost[MAX_SERVER_STR_LEN+2];       // mqtt host ip address
  char MQTTUser[MAX_USER_STR_LEN+2];       // mqtt user name
  char MQTTPassword[MAX_PASSWORD_STR_LEN+2];    // mqtt password
  int MQTTPort;       // mqtt port number
} SetupData = { 1, 45, 65, "0.0.0.0", "user", "secret", 1883 };


// ****************************************************************
// Setup the global variables
// ****************************************************************

const int ANALOG_PIN = A0;  // ESP8266 Analog Pin ADC0 = A0
char TempStr[20];
float SensorLevels[MAX_CHART_POINTS + 1];
float TriggerLow[MAX_CHART_POINTS + 1];
float TriggerHigh[MAX_CHART_POINTS + 1];
char TriggerLevels[MAX_CHART_POINTS + 1];
uint16_t HistoryBufferIn;
uint16_t PreviousHistoryBufferIn;
float MeterReading_kL = -1.0;
float LastMeterReading_kL = 0;
float FlowRate = 0;

volatile uint16_t ADCBuffer[ADC_BUFFER_SIZE];
volatile uint16_t ADCBufferIn;  
uint16_t ADCBufferOut;
uint16_t NextADCBufferOut;

unsigned long PreviousMillis = 0;
unsigned long PreviousMQTTMillis = 0;
unsigned long LastMQTTReconnectMillis = 0;
unsigned long LED_OnMillis = 0;


// ****************************************************************
// Start some things up
// ****************************************************************

//multicast DNS responder
MDNSResponder MDNS;

//Web server object. Will be listening in port 80 (default for HTTP)
ESP8266WebServer server(80);   
 
// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature DS18B20(&oneWire);

// MQTT client
WiFiClient espClient;
PubSubClient client(espClient);


// ****************************************************************
// Power on/reset setup
// ****************************************************************

void setup() 
{
  int i;
  
  // Start setup
  Serial.begin(115200);
  Serial.println("\n");
  delay(10);
  pinMode(LED_PIN, OUTPUT);          
  digitalWrite(LED_PIN, LOW);      // Low LED ON

  // Load the water meter settings from EEPROM/Flash
  EEPROM.begin(512);
  EEPROM.get(VALID_DATA_CODE_ADDR,i);
  if (i==VALID_DATA_CODE)  // Does it look like data has been written to the eeprom before??
  {
    EEPROM.get(SETUP_DATA_ADDR,SetupData);
    if ((SetupData.CountsPerLiter < 1) || (SetupData.CountsPerLiter > 10))
    {
      SetupData.CountsPerLiter = 1;   // if out of range then reset to default
    }
    if ((SetupData.MinThreshold < 0) || (SetupData.MinThreshold > 100))
    {
      SetupData.MinThreshold = 45;   // if out of range then reset to default
    }
    if ((SetupData.MaxThreshold < 0) || (SetupData.MaxThreshold > 100))
    {
      SetupData.MaxThreshold = 65;   // if out of range then reset to default
    }
  }
  else
  {
    EEPROM.put(VALID_DATA_CODE_ADDR,VALID_DATA_CODE);
    EEPROM.put(SETUP_DATA_ADDR,SetupData);

    noInterrupts();
    EEPROM.commit();
    interrupts();
  }
 
  // Make sure AP mode still not active from previous session seems to mess with mDNS
  WiFi.softAPdisconnect();
  //WiFi.disconnect();
  //WiFi.mode(WIFI_STA);
  delay(10);

  // Force wifimanager to reset config by pulling this pin to GND during startup
  pinMode(RESET_WIFI_PIN, INPUT_PULLUP);      

  // WiFiManager for configuring/managing the wifi settings
  WiFiManager wifiManager;
  wifiManager.setTimeout(180);     // 3min (180second) timeout for Access Point configuration
  wifiManager.setDebugOutput(false);   // Note WiFiManager prints password on serial if debug enabled!! 

  // Check if wifi config needs to be reset - forcing Access Point Config portal
  if (digitalRead(RESET_WIFI_PIN) == LOW)
  {
    Serial.println("Pin LOW - Resetting Wifi setings");
    wifiManager.resetSettings();
  }
  
  // Connect using wifimanager
  if(!wifiManager.autoConnect(MY_HOSTNAME,AP_PASSWORD)) 
  {
    Serial.println("Failed to connect - resetting");
    ESP.reset();  //reset and try again
  }

  // Made it past wifimanager so must be connected
  Serial.println("WiFi connected");
  WiFi.softAPdisconnect();     // ensure softAP is stopped so mDNS works reliably
  delay(10);
  
  // Setup mDNS for easy address resolution
  if (!MDNS.begin(MY_HOSTNAME))   // NOTE: In windows need bonjour installed for mDNS to work, linux install Avahi
  {
    Serial.println("mDNS setup failed!");
  }
  else
  {
    Serial.print("mDNS setup http://");
    Serial.print(MY_HOSTNAME);
    Serial.println(".local/");
    MDNS.addService("http","tcp",80);
    delay(100);           // not sure why but mDNS was unreliable without this
  }

  // Start the server
  server.on("/", HandleRootPath);         //Associate the handler function to the path
  server.on("/sensorsetup", HandleSetupPath);         //Associate the handler function to the path
  server.on("/ConfirmSave", HandleSaveConfirmation);   //Associate the handler function to the path
  server.on("/mqttsetup", HandleMQTTSetupPath);   //Associate the handler function to the path
  server.on("/mqttConfirmSave", HTTP_POST, HandleMQTTSaveConfirmation);
  server.begin();                         //Start the server
 
  // Print the IP address
  Serial.print("Use this URL to connect: ");
  Serial.print("http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");

  // Start the MQTT
  client.setServer(SetupData.MQTTHost, SetupData.MQTTPort);
  client.setCallback(MQTTcallback);

  DS18B20.begin();      // IC Default 9 bit.

  // Initialise some data
  for (i=0;i<=MAX_CHART_POINTS;i++)
  {
    SensorLevels[i]=0;
    TriggerLow[i]=SetupData.MinThreshold;
    TriggerHigh[i]=SetupData.MaxThreshold;
    TriggerLevels[i] = 0;
  }

  ADCBuffer[0] = 0;
  ADCBuffer[1] = 0;
  ADCBufferIn = 1; // We will always let input stay one ahead of output so no risk of reading value as get changed by ISR
  ADCBufferOut = 0;
  SetupInterrupts();

  PreviousMillis = millis();
  PreviousMQTTMillis = millis();
  digitalWrite(LED_PIN, HIGH);      // Low LED OFF
}


// ****************************************************************
// Use Timer Interrupt to read the analog input
// Initialize timer interrupt to trigger every 25ms (SAMPLE_PERIOD_mS)
// ****************************************************************

void ICACHE_RAM_ATTR MyTimerInterrupt();
void SetupInterrupts()
{
  timer1_attachInterrupt(MyTimerInterrupt); // Add ISR Function
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);
  // Dividers:
  //  TIM_DIV1 = 0,   //80MHz (80 ticks/us - 104857.588 us max)
  //  TIM_DIV16 = 1,  //5MHz (5 ticks/us - 1677721.4 us max)
  //  TIM_DIV256 = 3  //312.5Khz (1 tick = 3.2us - 26843542.4 us max)
  // Reloads:
  //  TIM_SINGLE  0 //on interrupt routine you need to write a new value to start the timer again
  //  TIM_LOOP  1 //on interrupt the counter will start with the same value again
  //
  // Arm the Timer for our SAMPLE_PERIOD_mS Interval
  timer1_write(SAMPLE_PERIOD_mS * 5 * 1000);   // 5 Ticks for TIM_DIV16 so use 5 * MicroSeconds  
}


// ****************************************************************
// The Timer Interrupt handler
// ****************************************************************

void ICACHE_RAM_ATTR MyTimerInterrupt()
{
  ADCBufferIn++;
  if (ADCBufferIn >= ADC_BUFFER_SIZE)  // Buffer is way bigger than it needs to be so no need to check for overruns
  {
    ADCBufferIn = 0;
  }
  ADCBuffer[ADCBufferIn] = analogRead(ANALOG_PIN);
}


// ****************************************************************
// Reconnect MQTT if necessary
// ****************************************************************

void ReconnectMQTT() 
{
  // Only attempt to reconnect every MQTT_CONN_RETRY_ms
  if ((millis() - LastMQTTReconnectMillis) >= MQTT_CONN_RETRY_ms)
  {
    Serial.println("X");
    //Serial.println(SetupData.MQTTHost);
    //Serial.println(SetupData.MQTTUser);
    //Serial.println(SetupData.MQTTPassword);
    // Attempt to connect
    if (client.connect(MY_HOSTNAME, SetupData.MQTTUser, SetupData.MQTTPassword)) 
    {
      Serial.println("Y");
      client.subscribe(meter_kLiters_topic);  // Success connecting so subscribe to topic
    } 
    else 
    {
      // Failed - Reset reconnect timeout so wait a while before retrying
      LastMQTTReconnectMillis = millis();
    }
  }
}


// ****************************************************************
// Process incoming MQTT
// ****************************************************************

void MQTTcallback(char* topic, byte* payload, unsigned int length)
{
  char SetkLiters[20];

  if (length < 19)    // if payload is too long ignore it
  {
    for (int i = 0; i < length; i++) 
    {
      SetkLiters[i]=(char)payload[i];
    }
    SetkLiters[length] = NULL;
    MeterReading_kL = atof(SetkLiters);
    LastMeterReading_kL = MeterReading_kL;
  }
}


// ****************************************************************
// MAIN LOOP
// ****************************************************************
 
void loop() 
{
  int i;
  int analog_sample;

  // Check if MQTT is connected
  if (!client.connected()) 
  {
    ReconnectMQTT();
  }
  else
  {
    client.loop();
    LastMQTTReconnectMillis = millis();      // This is to ensure the last reconnect doesnt roll over   
    
    // The following block only gets processed approx every MQTT_PERIOD_mS
    if ((millis() - PreviousMQTTMillis) >= MQTT_PERIOD_mS) 
    {
      //LastMQTTReconnectMillis = PreviousMQTTMillis;      // This is to ensure the last reconnect doesnt roll over   
      PreviousMQTTMillis += MQTT_PERIOD_mS;            // Prepare for the next MQTT update time
      // Do MQTT Updates here

      if (MeterReading_kL > 0.0)    // only calculate kWhrs if we already have a starting value
      {
        dtostrf(MeterReading_kL, -12, 3, TempStr);   // The negative means left justify
        client.publish(meter_kLiters_topic, TempStr, true);    // we want this retained so we update after reset/power out
        FlowRate = ((MeterReading_kL - LastMeterReading_kL) * 1000 / (MQTT_PERIOD_mS / 60000));  // Calculation gives Liters per minute
        dtostrf(FlowRate, -12, 0, TempStr);   // The negative means left justify
        client.publish(meter_flowrate_topic, TempStr, false);    // we dont need this one retained
        LastMeterReading_kL = MeterReading_kL;
      }
    }
  }

  // The following block only gets processed approx every PROCESS_PERIOD_mS
  if ((millis() - PreviousMillis) >= PROCESS_PERIOD_mS) 
  {
    PreviousMillis += PROCESS_PERIOD_mS;
    // Do stuff here - currently not required
  }

  // Check if the LED has been on long enough and turn off
  if ((millis() - LED_OnMillis) >= LED_ON_TIME_mS) 
  {
      digitalWrite(LED_PIN, HIGH);      // High for LED OFF
  }

  // The following block only gets processed when ADC samples are ready - usually every 25ms unless que'd up
  NextADCBufferOut = ADCBufferOut + 1;
  if (NextADCBufferOut >= ADC_BUFFER_SIZE)
  {
    NextADCBufferOut = 0;
  }
  if (NextADCBufferOut != ADCBufferIn)  // Process the next sample - this will always leave one sample in buffer
  {
    ADCBufferOut = NextADCBufferOut;  
    analog_sample = ADCBuffer[ADCBufferOut];
   
    // Save the history buffer index for later comparison/ calculation
    PreviousHistoryBufferIn = HistoryBufferIn;
    
    // Find the next index in the history values ring buffer
    HistoryBufferIn++;
    if (HistoryBufferIn >= MAX_CHART_POINTS)
    {
      HistoryBufferIn = 0;
    }
    
    SensorLevels[HistoryBufferIn] = (float)analog_sample / 10.23;  // Scale 0 to 100

    // Check if triggered count
    if (SensorLevels[HistoryBufferIn] > SetupData.MaxThreshold)
    {
      TriggerLevels[HistoryBufferIn] = 1;      // Use this for the high / low schmitt trigger level
      if (TriggerLevels[PreviousHistoryBufferIn] == 0)
      {
        digitalWrite(LED_PIN, LOW);      // Low LED ON
        LED_OnMillis = millis();
        if (MeterReading_kL > 0.0)  // Only calculate kLiters if we already have a starting value
        {
          MeterReading_kL = MeterReading_kL + (0.001 / SetupData.CountsPerLiter);
        }
      }
    }
    else if (SensorLevels[HistoryBufferIn] < SetupData.MinThreshold)
    {
      TriggerLevels[HistoryBufferIn] = 0;      // Use this for the high / low schmitt trigger level
    }
    else
    {
      TriggerLevels[HistoryBufferIn] = TriggerLevels[PreviousHistoryBufferIn];    // This is in the deadzone - no change
    }

    TriggerLow[HistoryBufferIn] = SetupData.MinThreshold;
    TriggerHigh[HistoryBufferIn] = SetupData.MaxThreshold;
  }
  MDNS.update();  
  server.handleClient();         // Handling of incoming web requests
}


// ****************************************************************
// Read temperature from Dallas DS18B20
// ****************************************************************
 
float getTemperature(int Sensor) 
{
  float tempC;
  do {
    DS18B20.requestTemperatures(); 
    tempC = DS18B20.getTempCByIndex(Sensor);
    delay(100);
  } while (tempC == 85.0 || tempC == (-127.0));
  return tempC;
}


// ****************************************************************
// Handle default web page request (Home page)
// ****************************************************************

void HandleRootPath()
{
  int i;

  server.sendContent("HTTP/1.1 200 OK\r\n");    // start the page header
  server.sendContent("Content-Type: text/html\r\n");
  server.sendContent("Connection: close\r\n");  // the connection will be closed after completion of the response
  server.sendContent("Refresh: 60\r\n");       // tell browser to refresh the page automatically every 60sec
  server.sendContent("\r\n");                   // this separates header from content that follows
  server.sendContent("<!DOCTYPE HTML>");
  server.sendContent("<html>");
  server.sendContent("<head><title>Water Meter</title></head>");
  server.sendContent("<body style=\"font-family:Verdana;\"><center><h1>Water Meter</h1>");
  server.sendContent("by Craig Hoffmann");
  server.sendContent("<p><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">");

  server.sendContent("<br><hr><br>Water Usage");
  dtostrf(MeterReading_kL, -12, 3, TempStr);  // negative width means left justify
  server.sendContent(String("<br><h1 style=\"font-family:Courier;\">") + TempStr + " kL</h1>");

  server.sendContent("<p>Average Flowrate last 1min");
  dtostrf(FlowRate, -12, 0, TempStr);   // The negative means left justify, Calculation gives Liters per minute
  server.sendContent(String("<br><h2 style=\"font-family:Courier;\">") + TempStr + " L/min</h2>");
  
  server.sendContent("<br><hr><br><form><input type=\"button\" value=\"Sensor Setup\" onclick=\"window.location.href='/sensorsetup'\"/></form>");
  server.sendContent("<br><form><input type=\"button\" value=\"MQTT Setup\" onclick=\"window.location.href='/mqttsetup'\"/></form>");

  //server.sendContent("<br><hr><br><form action=\"/sensorsetup\"><input type=\"submit\" value=\"Sensor Setup\"></form>");
  //server.sendContent("<br><form action=\"/mqttsetup\"><input type=\"submit\" value=\"MQTT Setup\"></form>");

  server.sendContent("</center></body>");
  server.sendContent("</html>");
  server.sendContent("\r\n");
  server.client().stop(); // Stop is needed because we sent no content length
}


// ****************************************************************
// Handle default web page request (Setup Page)
// ****************************************************************

void HandleSetupPath()
{
  int i;
  uint16_t HistoryBufferOut;

  server.sendContent("HTTP/1.1 200 OK\r\n");    // start the page header
  server.sendContent("Content-Type: text/html\r\n");
  server.sendContent("Connection: close\r\n");  // the connection will be closed after completion of the response
  // server.sendContent("Refresh: 60\r\n");       // tell browser to refresh the page automatically every 60sec
  server.sendContent("\r\n");                   // this separates header from content that follows
  server.sendContent("<!DOCTYPE HTML>");
  server.sendContent("<html>");
  server.sendContent("<head><title>Sensor Setup</title>");
  server.sendContent("  <script type=\"text/javascript\" src=\"https://www.gstatic.com/charts/loader.js\"></script>");
  server.sendContent("  <script type=\"text/javascript\">");
  server.sendContent("    google.charts.load('current', {'packages':['corechart']});");
  server.sendContent("    google.charts.setOnLoadCallback(drawChart);");
  server.sendContent("    function drawChart() {");
  server.sendContent("      var data = google.visualization.arrayToDataTable([");
  server.sendContent("        ['Reading', 'Trigger', 'Sensor', 'Min', 'Max'],");
  HistoryBufferOut = HistoryBufferIn;
  for (i=0;i<(MAX_CHART_POINTS-2);i++)
  {
    HistoryBufferOut++;
    if (HistoryBufferOut >= MAX_CHART_POINTS)
    {
      HistoryBufferOut = 0;
    }
    server.sendContent("        [' ',");  
    dtostrf(TriggerLevels[HistoryBufferOut] * CHART_LOGIC_LOW_TO_HIGH_DELTA + CHART_LOGIC_LOW_VALUE, 6, 1, TempStr);
    server.sendContent(TempStr);   
    server.sendContent(",");
    dtostrf(SensorLevels[HistoryBufferOut], 6, 1, TempStr);
    server.sendContent(TempStr);
    server.sendContent(",");
    dtostrf(TriggerLow[HistoryBufferOut], 6, 1, TempStr);
    server.sendContent(TempStr);
    server.sendContent(",");
    dtostrf(TriggerHigh[HistoryBufferOut], 6, 1, TempStr);
    server.sendContent(TempStr);
    server.sendContent("],");
  }
  HistoryBufferOut++;
  if (HistoryBufferOut >= MAX_CHART_POINTS)
  {
    HistoryBufferOut = 0;
  }
  server.sendContent("        [' ',");
  dtostrf(TriggerLevels[HistoryBufferOut] * CHART_LOGIC_LOW_TO_HIGH_DELTA + CHART_LOGIC_LOW_VALUE, 6, 1, TempStr);
  server.sendContent(TempStr);
  server.sendContent(",");
  dtostrf(SensorLevels[HistoryBufferOut], 6, 1, TempStr);
  server.sendContent(TempStr);
  server.sendContent(",");
  dtostrf(TriggerLow[HistoryBufferOut], 6, 1, TempStr);
  server.sendContent(TempStr);
  server.sendContent(",");
  dtostrf(TriggerHigh[HistoryBufferOut], 6, 1, TempStr);
  server.sendContent(TempStr);
  server.sendContent("]");
  server.sendContent("      ]);");
  server.sendContent("      var options = {");
  server.sendContent("        title: '10 Second Window',");
  server.sendContent("        curveType: 'none',");
  server.sendContent("        vAxis: { minValue: '0', maxValue: '100' },");
  server.sendContent("        curveType: 'none',");
  server.sendContent("        series: { 2: { lineDashStyle:[4, 4] }, 3: { lineDashStyle:[4, 4] } },");
  server.sendContent("        legend: { position: 'bottom' }");
  server.sendContent("      };");
  server.sendContent("      var chart = new google.visualization.LineChart(document.getElementById('curve_chart'));");   // was curve_chart
  server.sendContent("      chart.draw(data, options);");
  server.sendContent("    }");
  server.sendContent("  </script>");
  server.sendContent("</head>");
  server.sendContent("<body style=\"font-family:Verdana;\"><center><h1>Water Meter Sensor Setup</h1>");
  server.sendContent("by Craig Hoffmann");
  server.sendContent("<div id=\"curve_chart\" style=\"width: 100%; height: 400px\"></div>");    // was fixed at 900px and 400px

  server.sendContent("<p><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><form action=\"/ConfirmSave\">");
  dtostrf(SetupData.MaxThreshold, -4, 0, TempStr);  // negative width means left justify
  server.sendContent(String("Max Threshold: <input type=\"number\" name=\"MaxThreshold\" value=\"") + TempStr + "\" min=\"0\" max=\"100\">");

  dtostrf(SetupData.MinThreshold, -4, 0, TempStr);  // negative width means left justify
  server.sendContent(String("<p>Min Threshold: <input type=\"number\" name=\"MinThreshold\" value=\"") + TempStr + "\" min=\"0\" max=\"100\">");

  dtostrf(SetupData.CountsPerLiter, -4, 0, TempStr);  // negative width means left justify
  server.sendContent(String("<p>Counts per Liter: <input type=\"number\" name=\"CountsPerLiter\" value=\"") + TempStr + "\" min=\"1\" max=\"10\">");

  dtostrf(MeterReading_kL, -12, 3, TempStr);  // negative width means left justify
  server.sendContent(String("<p>Meter Reading (kL): <input type=\"number\" step=\"0.001\" name=\"MeterReading_kL\" value=\"") + TempStr + "\" min=\"-1\" max=\"999999\">");
  server.sendContent("<p><input type=\"submit\" value=\"Update\">");
  server.sendContent("</form>");

  server.sendContent("</center></body>");
  server.sendContent("</html>");
  server.sendContent("\r\n");
  server.client().stop(); // Stop is needed because we sent no content length
}


// ****************************************************************
// Present a web page to confirm settings saved
// ****************************************************************

void HandleSaveConfirmation()
{
  int i;
  float j;
  String ArgsString = "";
  
  if (server.arg("MinThreshold") != "")
  {
    ArgsString = server.arg("MinThreshold");     //Gets the value of the query parameter
    i = ArgsString.toInt();
    if ((i>=0) && (i<=100))
    {
      SetupData.MinThreshold = i;
    }
  }

  if (server.arg("MaxThreshold") != "")
  {
    ArgsString = server.arg("MaxThreshold");     //Gets the value of the query parameter
    i = ArgsString.toInt();
    if ((i>=0) && (i<=100))
    {
      SetupData.MaxThreshold = i;
    }
  }

  if (server.arg("CountsPerLiter") != "")
  {
    ArgsString = server.arg("CountsPerLiter");     //Gets the value of the query parameter
    i = ArgsString.toInt();
    if ((i>=1) && (i<=10))
    {
      SetupData.CountsPerLiter = i;
    }
  }

  if (server.arg("MeterReading_kL") != "")
  {
    ArgsString = server.arg("MeterReading_kL");     //Gets the value of the query parameter
    j = ArgsString.toFloat();
    if ((j>=1) && (j<=999999))
    {
      MeterReading_kL = j;    // **** NOTE: we don't save this one to FLASH/EEPROM
    }
  }
  
  EEPROM.put(SETUP_DATA_ADDR,SetupData);

  noInterrupts();
  EEPROM.commit();
  interrupts();
  
  server.sendContent("HTTP/1.1 200 OK\r\n");
  server.sendContent("Content-Type: text/html\r\n");
  server.sendContent("Connection: close\r\n");  // the connection will be closed after completion of the response
  server.sendContent("\r\n");                   // this separates header from content that follows
  server.sendContent("<!DOCTYPE HTML>");
  server.sendContent("<html>");
  server.sendContent("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><title>Sensor Setup Saved</title></head>");
  server.sendContent("<body style=\"font-family:Verdana;\"><center><h1>Save Complete</h1>");
  server.sendContent("<p>");

  dtostrf(SetupData.MaxThreshold, -4, 0, TempStr);  // negative width means left justify
  server.sendContent(String("Max Threshold: <input type=\"number\" name=\"MaxThreshold\" value=\"") + TempStr + "\" readonly>");

  dtostrf(SetupData.MinThreshold, -4, 0, TempStr);  // negative width means left justify
  server.sendContent(String("<p>Min Threshold: <input type=\"number\" name=\"MinThreshold\" value=\"") + TempStr + "\" readonly>");

  dtostrf(SetupData.CountsPerLiter, -4, 0, TempStr);  // negative width means left justify
  server.sendContent(String("<p>Counts per Liter: <input type=\"number\" name=\"CountsPerLiter\" value=\"") + TempStr + "\" readonly>");

  dtostrf(MeterReading_kL, -12, 3, TempStr);  // negative width means left justify
  server.sendContent(String("<p>Meter Reading (kL): <input type=\"number\" step=\"0.001\" name=\"MeterReading_kL\" value=\"") + TempStr + "\" readonly>");

  server.sendContent("<p><form><input type=\"button\" value=\"Return\" onclick=\"window.location.href='/'\"/></form>");
  server.sendContent("</center></body>");
  server.sendContent("</html>");
  server.sendContent("\r\n");
  server.client().stop(); // Stop is needed because we sent no content length
}


// ****************************************************************
// Handle MQTT Setup Web Page Request
// ****************************************************************

void HandleMQTTSetupPath()
{
  server.sendContent("HTTP/1.1 200 OK\r\n");    // start the page header
  server.sendContent("Content-Type: text/html\r\n");
  server.sendContent("Connection: close\r\n");  // the connection will be closed after completion of the response
  server.sendContent("\r\n");                   // this separates header from content that follows
  server.sendContent("<!DOCTYPE HTML>");
  server.sendContent("<html>");
  server.sendContent("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><title>MQTT Setup</title></head>");
  server.sendContent("<body style=\"font-family:Verdana;text-align:center;min-width:340px;\"><h1>Water Meter MQTT Setup</h1>");
  server.sendContent("by Craig Hoffmann<p>");

  server.sendContent("<p><div>");
  server.sendContent("<p><form action=\"/mqttConfirmSave\" method=\"post\">");

  server.sendContent(String("<p>Server: <br><input type=\"text\" name=\"Server\" value=\"") + SetupData.MQTTHost + "\" size=\"" + String(MAX_SERVER_STR_LEN) + "\">");
  server.sendContent(String("<p>User: <br><input type=\"text\" name=\"User\" value=\"") + SetupData.MQTTUser + "\" size=\"" + String(MAX_USER_STR_LEN) + "\">");
  server.sendContent(String("<p>Password: <br><input type=\"password\" name=\"Password\" value=\"XXXXXX\" size=\"") + String(MAX_PASSWORD_STR_LEN) + "\">");
  //server.sendContent(String("<p>") + SetupData.MQTTPassword + "<p>");   // ** test code only **
  dtostrf(SetupData.MQTTPort, -4, 0, TempStr);  // negative width means left justify
  server.sendContent(String("<p>Port: <br><input type=\"text\" name=\"Port\" value=\"") + TempStr + "\" min=\"0\" min=\"9999\">");

  server.sendContent("<p><input type=\"submit\" value=\"Update\">");
  server.sendContent("</form>");
  server.sendContent("</div>");
 
  server.sendContent("</body>");
  server.sendContent("</html>");
  server.sendContent("\r\n");
  server.client().stop(); // Stop is needed because we sent no content length
}


// ****************************************************************
// Handle MQTT Save Confirmation POST Request
// ****************************************************************

void HandleMQTTSaveConfirmation()
{
  int i;
  String ArgsString = "";
  
  if (server.hasArg("Server"))
  {
    ArgsString = server.arg("Server");     //Gets the value of the query parameter
    i = ArgsString.length()+1;
    if (i < MAX_SERVER_STR_LEN)
    {
      ArgsString.toCharArray(SetupData.MQTTHost,i);
    }
  }

  if (server.hasArg("User"))
  {
    ArgsString = server.arg("User");     //Gets the value of the query parameter
    i = ArgsString.length()+1;
    if (i < MAX_USER_STR_LEN)
    {
      ArgsString.toCharArray(SetupData.MQTTUser,i);
    }
  }

  if (server.hasArg("Password"))
  {
    ArgsString = server.arg("Password");     //Gets the value of the query parameter
    i = ArgsString.length()+1;
    if ((i < MAX_PASSWORD_STR_LEN) && (ArgsString != "XXXXXX")) 
    {
      ArgsString.toCharArray(SetupData.MQTTPassword,i);
    }
  }


  if (server.hasArg("Port"))
  {
    if (server.arg("Port") != "")
    {
      ArgsString = server.arg("Port");     //Gets the value of the query parameter
      i = ArgsString.toInt();
      if ((i>=1) && (i<=9999))
      {
        SetupData.MQTTPort = i;
      }
    }
  }
  
  EEPROM.put(SETUP_DATA_ADDR,SetupData);

  noInterrupts();
  EEPROM.commit();
  interrupts();
  
  server.sendContent("HTTP/1.1 200 OK\r\n");
  server.sendContent("Content-Type: text/html\r\n");
  server.sendContent("Connection: close\r\n");  // the connection will be closed after completion of the response
  server.sendContent("\r\n");                   // this separates header from content that follows
  server.sendContent("<!DOCTYPE HTML>");
  server.sendContent("<html>");
  server.sendContent("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><title>MQTT Setup Saved</title></head>");
  server.sendContent("<body style=\"font-family:Verdana;\"><center><h1>Save Complete</h1>");
  server.sendContent("<p>");

  server.sendContent(String("<p>Server: <br><input type=\"text\" name=\"Server\" value=\"") + SetupData.MQTTHost + "\" size=\"" + String(MAX_SERVER_STR_LEN) + "\" readonly>");
  server.sendContent(String("<p>User: <br><input type=\"text\" name=\"User\" value=\"") + SetupData.MQTTUser + "\" size=\"" + String(MAX_USER_STR_LEN) + "\" readonly>");
  server.sendContent(String("<p>Password: <br><input type=\"password\" name=\"Password\" value=\"XXXXXX\" size=\"") + String(MAX_PASSWORD_STR_LEN) + "\" readonly>");
  //server.sendContent(String("<p>") + SetupData.MQTTPassword + "<p>");   // ** test code only **
  dtostrf(SetupData.MQTTPort, -4, 0, TempStr);  // negative width means left justify
  server.sendContent(String("<p>Port: <br><input type=\"text\" name=\"Port\" value=\"") + TempStr + "\" min=\"0\" min=\"9999\" readonly>");

  server.sendContent("<p><form><input type=\"button\" value=\"Return\" onclick=\"window.location.href='/'\"/></form>");
  server.sendContent("</center></body>");
  server.sendContent("</html>");
  server.sendContent("\r\n");
  server.client().stop(); // Stop is needed because we sent no content length
}
