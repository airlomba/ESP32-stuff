#include <M5Atom.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <ESP_Mail_Client.h>

#define PIN       27
#define NUMPIXELS 1

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

#define WIFI_SSID_1         "SSID-1"                // edit this
#define WIFI_PASSWORD_1     "password-for-SSID-1"   // edit this
#define WIFI_SSID_2         "SSID-2"                // edit this
#define WIFI_PASSWORD_2     "password-for-SSID-2"   // edit this

#define SMTP_HOST           "smtp-server.domain"    // edit this
#define SMTP_PORT           465
#define AUTHOR_EMAIL        "source-email-adress"   // edit this
#define AUTHOR_PASSWORD     "source-password"       // edit this

#define RECIPIENT_EMAIL     "someone@somewhere.net"   // edit this Recipient's email

#define NO_TOUCH            0xFE
#define THRESHOLD           200
#define ATTINY1_HIGH_ADDR   0x78
#define ATTINY2_LOW_ADDR    0x77

#define sense_interval      60000   // 60000 = 1 minute

uint32_t chipId = 0;
int network_option = 0;

unsigned char level_data[20] = {0};
unsigned int ticks = 0;
unsigned int alert_already_sent = 0;
unsigned int last_alert_elasped_time = 0;

SMTPSession smtp;

int setupWifi();
void smtpCallback(SMTP_Status status);  //Callback function to get the Email sending status
void getSectionValues();
int send_alert();

void setup(){
  pixels.begin();
  pixels.setPixelColor(0, pixels.Color(100, 0, 0));
  pixels.show();
  Wire.begin();
  Serial.begin(115200);
  Serial.println();
  // Just to know which program is running on my Arduino
  Serial.println(F("START " __FILE__ " from " __DATE__));
  for(int i=0; i<17; i=i+8) {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  Serial.println(" ");
  Serial.printf("ESP32 Chip model = %s Rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("This chip has %d cores\n", ESP.getChipCores());
  Serial.print("Chip ID: "); Serial.println(chipId);
  Serial.println(" ");

  network_option = setupWifi();
  
  pixels.setPixelColor(0, pixels.Color(0, 100, 0));  // Bright green
  pixels.show();  // sends the updated color to the hardware.
  Serial.println();
}

void loop(){
  uint32_t level = 0;

  // Check if connected to WiFi
  if (WiFi.status() != WL_CONNECTED){
    Serial.println("Lost WiFi connection! Trying to reconnect now...");
    pixels.setPixelColor(0, pixels.Color(100, 0, 0));  // Bright green
    pixels.show();  // sends the updated color to the hardware.
    network_option = 0;
    network_option = setupWifi();
    if (network_option != 0){
      pixels.setPixelColor(0, pixels.Color(0, 100, 0));  // Bright green
      pixels.show();  // sends the updated color to the hardware.
    }
  }

  pixels.setPixelColor(0, pixels.Color(100, 100, 100));  // Bright white
  pixels.show();  // sends the updated color to the hardware.
  getSectionValues();
  delay(500);
  pixels.setPixelColor(0, pixels.Color(0, 100, 0));  // Bright green
  pixels.show();  // sends the updated color to the hardware.

  /* for debug only...
  Serial.print("Section values: ");
  for (int i=0; i<20; i++){
    Serial.print(level_data[i]);
    Serial.print(".");
  }
  Serial.println();
  */
  
  for (int i = 0 ; i < 20; i++) {
    if (level_data[i] > THRESHOLD) { level++; }
  }

  Serial.print("water level = ");
  Serial.print(level * 5);
  Serial.println("%");

  if ((level * 5) > 10){    // if 10% mark, send alert via email!
    pixels.setPixelColor(0, pixels.Color(0, 0, 100));  // Bright blue
    pixels.show();  // sends the updated color to the hardware.

    if (alert_already_sent == 0){
      if (send_alert() != 0){ alert_already_sent = 1; }      
    }
    else {
      last_alert_elasped_time++;
      if (last_alert_elasped_time > 60){
        if (send_alert() != 0){ last_alert_elasped_time = 0; }
      }      
    }
  }  
  delay(sense_interval); // Wait a minute
  
}




int setupWifi() {
    delay(10);
    // try server 1:
    Serial.printf("Connecting to %s", WIFI_SSID_1);
    WiFi.mode(WIFI_STA);  // Set the mode to WiFi station mode.
    WiFi.begin(WIFI_SSID_1, WIFI_PASSWORD_1);  // Start Wifi connection.
    int max_retries = 10;
    while (max_retries >0){
        if (WiFi.status() != WL_CONNECTED){
          delay(500);
          Serial.print(".");
          max_retries--;
        }
        else {
          max_retries = -1;
          Serial.printf(" Success!\n");
          Serial.print("IP address: ");
          Serial.println(WiFi.localIP());
          return 1;
        }
    }
    Serial.println("FAIL!");
    // try server 2:
    Serial.printf("Connecting to %s", WIFI_SSID_2);
    WiFi.mode(WIFI_STA);  // Set the mode to WiFi station mode.
    WiFi.begin(WIFI_SSID_2, WIFI_PASSWORD_2);  // Start Wifi connection.
    max_retries = 10;
    while (max_retries >0){
        if (WiFi.status() != WL_CONNECTED){
          delay(500);
          Serial.print(".");
          max_retries--;
        }
        else {
          max_retries = -1;
          Serial.printf(" Success!\n");
          Serial.print("IP address: ");
          Serial.println(WiFi.localIP());
          return 2;
        }
    }
    Serial.println("FAIL!");
    return 0;
}




void getSectionValues(){
  memset(level_data, 0, sizeof(level_data));
  Wire.requestFrom(ATTINY2_LOW_ADDR, 8);
  while (8 != Wire.available());
  for (int i = 0; i < 8 ; i++) {
    level_data[i] = Wire.read(); // receive a byte as character
  }
  delay(10);
  Wire.requestFrom(ATTINY1_HIGH_ADDR, 12);
  while (12 != Wire.available());
  for (int i = 8; i < 20; i++) {
    level_data[i] = Wire.read();
  }
}


int send_alert(){
  /*  Set the network reconnection option */
  MailClient.networkReconnect(true);

  /** Enable the debug via Serial port
   * 0 for no debugging
   * 1 for basic level debugging
   *
   * Debug port can be changed via ESP_MAIL_DEFAULT_DEBUG_PORT in ESP_Mail_FS.h
   */
  smtp.debug(1);

  /* Set the callback function to get the sending results */
  smtp.callback(smtpCallback);

  /* Declare the Session_Config for user defined session credentials */
  Session_Config config;

  /* Set the session config */
  config.server.host_name = SMTP_HOST;
  config.server.port = SMTP_PORT;
  config.login.email = AUTHOR_EMAIL;
  config.login.password = AUTHOR_PASSWORD;
  config.login.user_domain = "";

  /*
  Set the NTP config time
  For times east of the Prime Meridian use 0-12
  For times west of the Prime Meridian add 12 to the offset.
  Ex. American/Denver GMT would be -6. 6 + 12 = 18
  See https://en.wikipedia.org/wiki/Time_zone for a list of the GMT/UTC timezone offsets
  */
  config.time.ntp_server = F("pool.ntp.org");
  config.time.gmt_offset = 0;
  config.time.day_light_offset = 1;

  /* Declare the message class */
  SMTP_Message message;

  /* Set the message headers */
  message.sender.name = F("Deposito do cilindro");
  message.sender.email = AUTHOR_EMAIL;
  message.subject = F("Alerta de garrafao cheio (cilindro)");
  message.addRecipient(F("AIRLOMBA-IoT"), RECIPIENT_EMAIL);
    
  /*Send HTML message*/
  /*String htmlMsg = "<div style=\"color:#2f4468;\"><h1>Hello World!</h1><p>- Sent from ESP board</p></div>";
  message.html.content = htmlMsg.c_str();
  message.html.content = htmlMsg.c_str();
  message.text.charSet = "us-ascii";
  message.html.transfer_encoding = Content_Transfer_Encoding::enc_7bit;*/
   
  //Send raw text message
  String textMsg = "Olá,\nO depósito de água de escape do cilindro esta quase cheio!\nFavor despejar o garrafão.\nObrigado.\n\nMensagem enviada por ADB-IoT-01.\n\n\nPor favor, não responda a esta mensagem.\nSe tiver algo a dizer, contacte o Emmanuel. :)";
  message.text.content = textMsg.c_str();
  message.text.charSet = "pt-ascii";
  message.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  
  message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_low;
  message.response.notify = esp_mail_smtp_notify_success | esp_mail_smtp_notify_failure | esp_mail_smtp_notify_delay;


  // Connect to the server 
  if (!smtp.connect(&config)){
    ESP_MAIL_PRINTF("Connection error, Status Code: %d, Error Code: %d, Reason: %s", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
    return 0;
  }

  if (!smtp.isLoggedIn()){
    Serial.println("\nNot yet logged in.");
  }
  else{
    if (smtp.isAuthenticated())
      Serial.println("\nSuccessfully logged in.");
    else
      Serial.println("\nConnected with no Auth.");
  }

  // Start sending Email and close the session
  if (!MailClient.sendMail(&smtp, &message)){
    ESP_MAIL_PRINTF("Error, Status Code: %d, Error Code: %d, Reason: %s", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
    return 0;
  }
  return 1;
}


/* Callback function to get the Email sending status */
void smtpCallback(SMTP_Status status){
  /* Print the current status */
  Serial.println(status.info());

  /* Print the sending result */
  if (status.success()){
    Serial.println("----------------");
    ESP_MAIL_PRINTF("Message sent success: %d\n", status.completedCount());
    ESP_MAIL_PRINTF("Message sent failed: %d\n", status.failedCount());
    Serial.println("----------------\n");

    for (size_t i = 0; i < smtp.sendingResult.size(); i++)
    {
      /* Get the result item */
      SMTP_Result result = smtp.sendingResult.getItem(i);
      ESP_MAIL_PRINTF("Message No: %d\n", i + 1);
      ESP_MAIL_PRINTF("Status: %s\n", result.completed ? "success" : "failed");
      ESP_MAIL_PRINTF("Date/Time: %s\n", MailClient.Time.getDateTimeString(result.timestamp, "%B %d, %Y %H:%M:%S").c_str());
      ESP_MAIL_PRINTF("Recipient: %s\n", result.recipients.c_str());
      ESP_MAIL_PRINTF("Subject: %s\n", result.subject.c_str());
    }
    Serial.println("----------------\n");

    // You need to clear sending result as the memory usage will grow up.
    smtp.sendingResult.clear();
  }
}
