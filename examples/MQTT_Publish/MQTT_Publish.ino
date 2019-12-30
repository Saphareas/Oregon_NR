/************************* Libraries *********************************/
#include <ESP8266WiFi.h>
#include <Oregon_NR.h>
#include <PubSubClient.h>


/************************* WiFi *********************************/
const char* ssid     = "<your wifi name>";
const char* password = "<your wifi password>";
WiFiClient wifi_client;

void setup_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("WiFi connected. ");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}


/************************* MQTT Broker *********************************/
const char* broker = "<your wifi broker>";
const int   port   = 1883; // use 8883 for SSL
const char* client_id = "ESP8266Client";
const char* super_topic = "oregon"; //MQTT topic will be super_topic/[wind, rain, ...]/<sensor data>
PubSubClient client(wifi_client);

void connect_mqtt() {
  while (!client.connected()) {
    Serial.print("Connecting to broker...");
    if (!client.connect(client_id)) {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 5 seconds");
      delay(5000);
    } else {
      Serial.println("    Connected!");
    }
  }
}


/************************* RF Receiver *********************************/
Oregon_NR oregon(0, 0, 2, true); //TODO: Change Pins
const char windDir[16][4] = {
  "N  ", "NNE", "NE ", "ENE",  "E  ", "ESE", "SE ", "SSE",  "S  ", "SSW", "SW ", "WSW",  "W  ", "WNW", "NW ", "NNW"
};


/************************* Routines *********************************/
void setup() {
  Serial.begin(115200);  
  setup_wifi();

  client.setServer(broker, 1883);

  oregon.start();
  oregon.decode_method = 3;
}



void loop() {
  //MQTT setup
  if (!client.connected()) {
    connect_mqtt();
  }
  client.loop();
  
  //Start receiving
  oregon.capture(0);

  if (oregon.captured) {
    // Output information to Serial
    Serial.print ((float) millis () / 1000, 1); //Time
    Serial.print ("s \t \t");
    // protocol version
    if (oregon.ver == 2) 
      Serial.print ("");
    if (oregon.ver == 3) 
      Serial.print ("3");
    
    // Package recovery information
    if (oregon.restore_sign & 0x01) 
      Serial.print ("s"); // restored single measures    
    if (oregon.restore_sign & 0x02) 
      Serial.print ("d"); // restored double measures    
    if (oregon.restore_sign & 0x04) 
      Serial.print ("p"); // fixed error when recognizing package version    

    // Output the received packet. Dots are nibbles containing dubious bits.
    for (int q = 0; q <PACKET_LENGTH - 1; q ++)
      if (oregon.valid_p [q] == 0x0F) 
        Serial.print (oregon.packet [q], HEX);
      else 
        Serial.print (".");
        
    // Package processing time
    Serial.print (" ");
    Serial.print (oregon.work_time);
    Serial.print ("ms");


    //If Temperature/Humidity sensor
    if ((oregon.sens_type == THGN132 || (oregon.sens_type & 0x0FFF) == RTGN318 || oregon.sens_type == THGR810 || oregon.sens_type == THN132) && oregon.crc_c) {
      log_temp();
      char topic[50];
      sprintf(topic, "%s/ch%d", super_topic, oregon.sens_chnl); // e.g. for channel 1 => oregon/ch1/...
      publish_temp(topic);
    }

    //If wind sensor
    if (oregon.sens_type == WGR800 && oregon.crc_c) {
      log_wind();
      char topic[50];
      sprintf(topic, "%s/%s", super_topic, "wind");
      publish_wind(topic);
    }

    //If rain sensor
    if (oregon.sens_type == PCR800 && oregon.crc_c) {
      log_rain();
      char topic[50];
      sprintf(topic, "%s/%s", super_topic, "rain");
      publish_rain(topic);
    }

    Serial.println();
  }
}

bool publish_all(char topic[]) {
  char actual_topic[50];
  char payload[50];
  bool publish_successfull = false; 
  
  sprintf(actual_topic, "%s%s", topic, "/type"); // e.g. oregon/ch1/type
  sprintf(payload, "%04X", oregon.sens_type);
  if (client.publish(actual_topic, payload, true)) 
    publish_successfull = true;   

  sprintf(actual_topic, "%s%s", topic, "/id");
  sprintf(payload, "%02X", oregon.sens_id);
  if (client.publish(actual_topic, payload, true)) 
    publish_successfull = true;   
    
  sprintf(actual_topic, "%s%s", topic, "/battery");
  sprintf(payload, "%d", oregon.sens_battery);
  if (client.publish(actual_topic, payload, true)) 
    publish_successfull = true;

  return publish_successfull;  
}

void log_temp() {
  Serial.print("\t");
  
  Serial.print(" TYPE: ");
  if (oregon.sens_type == THGN132) 
    Serial.print("THGN132N");
  if (oregon.sens_type == THGR810) 
    Serial.print("THGR810 ");
  if ((oregon.sens_type & 0x0FFF) == RTGN318) 
    Serial.print("RTGN318");
  if (oregon.sens_type == THN132) 
    Serial.print("THN132N ");
    
  Serial.print(" CHNL: ");
  Serial.print(oregon.sens_chnl);
  
  if (oregon.sens_tmp >= 0 && oregon.sens_tmp < 10) 
    Serial.print(" TMP:  ");
  if (oregon.sens_tmp < 0 && oregon.sens_tmp >-10 || oregon.sens_tmp >= 10) 
    Serial.print(" TMP: ");
  if (oregon.sens_tmp <= -10) 
    Serial.print(" TMP:");  
  Serial.print(oregon.sens_tmp, 1);
  Serial.print("C ");
  
  if (oregon.sens_type == THGN132 || oregon.sens_type == THGR810 || (oregon.sens_type & 0x0FFF) == RTGN318) {
    Serial.print("HUM: ");
    Serial.print(oregon.sens_hmdty, 0);
    Serial.print("%");
  }
  else 
    Serial.print("        ");
  
  Serial.print(" BAT: ");
  if (oregon.sens_battery) 
    Serial.print("ok "); 
  else 
    Serial.print("!! ");
    
  Serial.print("ID: ");
  Serial.print(oregon.sens_id, HEX);
}



void publish_temp(char topic[]) {
  char actual_topic[50];
  char payload[50];
  bool publish_successfull = false; 
  
  sprintf(actual_topic, "%s%s", topic, "/temperature");
  sprintf(payload, "%.02f", oregon.sens_tmp);
  if (client.publish(actual_topic, payload, true)) 
    publish_successfull = true;   

  sprintf(actual_topic, "%s%s", topic, "/humidity");
  sprintf(payload, "%.02f", oregon.sens_hmdty);
  if (client.publish(actual_topic, payload, true)) 
    publish_successfull = true;  
    
  if (publish_all(topic) && publish_successfull)
    Serial.println("Published successfully!"); 
  else 
    Serial.println("Publishing failed!");  
}

void log_wind() {
  Serial.print("\t");
  
  Serial.print(" TYPE: WGR800");
  
  Serial.print(" AVG WS: ");
  Serial.print(oregon.sens_avg_ws, 1);
  Serial.print("m/s ");

  Serial.print("MAX WS: ");
  Serial.print(oregon.sens_max_ws, 1);
  Serial.print("m/s "); 
  Serial.print("WDIR: "); //N = 0, E = 4, S = 8, W = 12
  Serial.print(oregon.sens_wdir);
  
  Serial.print(" BAT: ");
  if (oregon.sens_battery) 
    Serial.print("ok "); 
  else 
    Serial.print("!! ");
    
  Serial.print("ID: ");
  Serial.print(oregon.sens_id, HEX);
}

void publish_wind(char topic[]) {
  char actual_topic[50];
  char payload[50];
  bool publish_successfull; 
  
  sprintf(actual_topic, "%s%s", topic, "/avg_speed"); // oregon/wind/avg_speed
  sprintf(payload, "%.02f", oregon.sens_avg_ws);
  if (client.publish(actual_topic, payload, true)) 
    publish_successfull = true;
    
  sprintf(actual_topic, "%s%s", topic, "/max_speed");
  sprintf(payload, "%.02f", oregon.sens_max_ws);
  if (client.publish(actual_topic, payload, true)) 
    publish_successfull = true;

  sprintf(actual_topic, "%s%s", topic, "/quadrant"); // Wind direction as quadrant (0-15)
  sprintf(payload, "%d", oregon.sens_wdir);
  if (client.publish(actual_topic, payload, true)) 
    publish_successfull = true;

//  sprintf(actual_topic, "%s%s", topic, "/direction"); // Wind direction as compass (N = North, WSW = West South West)
//  sprintf(payload, "%s", windDir[oregon.sens_wdir]);
//  if (client.publish(actual_topic, payload, true)) 
//    publish_successfull = true;
//
//  sprintf(actual_topic, "%s%s", topic, "/degrees"); // Wind direction in degrees (quadrant * 22.5)
//  sprintf(payload, "%f", oregon.sens_wdir * 22.5);
//  if (client.publish(actual_topic, payload, true)) 
//    publish_successfull = true;

  if (publish_all(topic) && publish_successfull)
    Serial.println("Published successfully!"); 
  else 
    Serial.println("Publishing failed!");
}

void log_rain() {
  Serial.print("\t");

  Serial.print(" TYPE: PCR800");

  Serial.print(" Total rain: ");
  Serial.print(oregon.sens_total_rain);
  
  Serial.print(" Rate: ");
  Serial.print(oregon.sens_rain_rate);
  
  Serial.print(" BAT: ");
  if (oregon.sens_battery) 
    Serial.print("ok "); 
  else 
    Serial.print("!! ");
    
  Serial.print("ID: ");
  Serial.print(oregon.sens_id, HEX);
}

void publish_rain(char topic[]) {
  char actual_topic[50];
  char payload[50];
  bool publish_successfull;   
  
  sprintf(actual_topic, "%s%s", topic, "/total_rain"); // oregon/rain/total_rain
  sprintf(payload, "%.02f", oregon.sens_total_rain);
  if (client.publish(actual_topic, payload, true)) 
    publish_successfull = true;

  sprintf(actual_topic, "%s%s", topic, "/rain_rate");
  sprintf(payload, "%.02f", oregon.sens_rain_rate);
  if (client.publish(actual_topic, payload, true)) 
    publish_successfull = true;

  if (publish_all(topic) && publish_successfull)
    Serial.println("Published successfully!"); 
  else 
    Serial.println("Publishing failed!");
}
