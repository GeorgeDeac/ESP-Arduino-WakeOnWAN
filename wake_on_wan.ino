#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Arduino.h>
#include <SPI.h>
#include <WiFiUDP.h>
#include <ESP8266Ping.h>
#include "settings.h"

bool staticip = true; //set it to false for dhcp
IPAddress ip();
IPAddress gateway(192,168,100,1);
IPAddress subnet(255,255,255,0);
MDNSResponder mdns;
WiFiUDP udp;
ESP8266WebServer server(1337);

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
String usn = SERVICE_USERNAME;
String pwd = SERVICE_PASSWORD;
int wait_ping = PING_POLL_WAIT;

void sendWOL(const IPAddress ip, const byte mac[]);
void beginWifi();
void macStringToBytes(const String mac, byte *bytes);
bool pingHost(const IPAddress ip);

//Check cookies
bool is_authentified(){
  Serial.println("Enter is_authentified");
  if (server.hasHeader("Cookie")){   
    Serial.print("Found cookie: ");
    String cookie = server.header("Cookie");
    Serial.println(cookie);
    if (cookie.indexOf("ESPSESSIONID=1") != -1) {
      Serial.println("Authentification Successful");
      return true;
    }
  }
  Serial.println("Authentification Failed");
  return false;  
}

//Login page, also called for disconnect
void handleLogin(){
  String msg;
  if (server.hasHeader("Cookie")){   
    Serial.print("Found cookie: ");
    String cookie = server.header("Cookie");
    Serial.println(cookie);
  }
  if (server.hasArg("DISCONNECT")){
    Serial.println("Disconnection");
    String header = "HTTP/1.1 301 OK\r\nSet-Cookie: ESPSESSIONID=0\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
    server.sendContent(header);
    return;
  }
  
  //Return to root if user is already logged in by cookie
  if (is_authentified()){
    String header = "HTTP/1.1 301 OK\r\nLocation: /\r\nCache-Control: no-cache\r\n\r\n";
    server.sendContent(header);
    return;
    }
  
  if (server.hasArg("USERNAME") && server.hasArg("PASSWORD")){
    if (server.arg("USERNAME") == usn &&  server.arg("PASSWORD") == pwd ){
      String header = "HTTP/1.1 301 OK\r\nSet-Cookie: ESPSESSIONID=1\r\nLocation: /\r\nCache-Control: no-cache\r\n\r\n";
      server.sendContent(header);
      Serial.println("Log in Successful");
      return;
    }
  msg = "Wrong username/password! try again.";
  Serial.println("Log in Failed");
  }
  String content = "<html><body><form action='/login' method='POST'>Log in<br>";
  content += "User:<input type='text' name='USERNAME' placeholder='user name'><br>";
  content += "Password:<input type='password' name='PASSWORD' placeholder='password'><br>";
  content += "<input type='submit' name='SUBMIT' value='Submit'></form>" + msg + "<br>";
  server.send(200, "text/html", content);
}

//Root
void handleRoot(){
  Serial.println("Enter handleRoot");
  String header;
  if (!is_authentified()){
    String header = "HTTP/1.1 301 OK\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
    server.sendContent(header);
    return;
  }
  //Will display a form that, once submitted, sends a GET to /wol
  digitalWrite(LED_BUILTIN, HIGH);
  IPAddress target_ip;
  target_ip = WiFi.localIP();
  String html_home_page = HOME_PAGE;
  html_home_page.replace("{favicon}", FAVICON);
  html_home_page.replace("{ip1}", String(target_ip[0]));
  html_home_page.replace("{ip2}", String(target_ip[1]));
  html_home_page.replace("{ip3}", String(target_ip[2]));
  server.send(200, "text/html", html_home_page);
  delay(500); 
  digitalWrite(LED_BUILTIN, LOW);
}

//wol
void wol(){
  Serial.println("Enter handleWol");
  String header;
  if (!is_authentified()){
    String header = "HTTP/1.1 301 OK\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
    server.sendContent(header);
    return;
  }
 digitalWrite(LED_BUILTIN, HIGH);
    // GET requests to /wol send Wake-On-LAN frames (users should go to / and use the form)
    // the mac parameter is mandatory and should be a non-delimited MAC address
    // password is also mandatory
    // example: GET /wol?mac=112233aabbcc&bcast=255&pwd=xxx
    
    if(server.arg("mac").length() <= 12 && server.arg("bcast").length() <= 3) {
      String mac = server.arg("mac");
      int bcast = server.arg("bcast").toInt();

        IPAddress target_ip;
        target_ip = WiFi.localIP();
        target_ip[3] = bcast;
        byte target_mac[6];
        macStringToBytes(mac, target_mac);
        Serial.println("Sending WOL");
        Serial.println(target_ip);
        sendWOL(target_ip, target_mac);
        Serial.println("Pinging...");
        server.send(200, "text/plain", "WOL sent to " + target_ip.toString() + " " + mac + " " + ((pingHost(target_ip) == true) ? "online" : "no response"));
    }
    else {
      server.send(403, "text/plain", "Invalid data");
    }
    delay(1000); 
    digitalWrite(LED_BUILTIN, LOW);
}

//Custom 404
void handleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  digitalWrite(LED_BUILTIN, HIGH); //Blink to spot brute-force attempts
  delay(250); 
  digitalWrite(LED_BUILTIN, LOW);
}

//Setup
void setup(void){
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW); //Turn on led
  beginWifi();
  while (!mdns.begin("esp8266", WiFi.localIP())) {}
  udp.begin(9);

  //The list of handlers
  server.on("/", handleRoot);
  server.on("/login", handleLogin);
  server.on("/wol", wol);
  server.onNotFound(handleNotFound);

 //The list of headers to be recorded
  const char * headerkeys[] = {"User-Agent","Cookie"} ;
  size_t headerkeyssize = sizeof(headerkeys)/sizeof(char*);
  //Ask server to track these headers
  server.collectHeaders(headerkeys, headerkeyssize );
  
  server.begin();
  Serial.println("HTTP server started");
}

//Mainloop
void loop(void){
  if (WiFi.status() != WL_CONNECTED){
    delay(500); //Unecessary delay xD
     ESP.reset(); 
  }
  server.handleClient();
}

//beginWifi connection
void beginWifi() {
  //Check if either to use a static ip or a dynamic one
  if(staticip == true){
   WiFi.config(ip, gateway, subnet);
   Serial.println("");
   Serial.println("Acquiring static ip");
  }
  else {
   Serial.println("");
   Serial.println("Acquiring dynamic ip");
  }
  WiFi.begin(ssid, password);
  Serial.println("");
  
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    Serial.print(".");
    digitalWrite(LED_BUILTIN, LOW);
  }
  
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

//Utils functions down below

/*
* Send a Wake-On-LAN packet for the given MAC address, to the given IP
* address. Often the IP address will be the local broadcast.
*/
void sendWOL(const IPAddress ip, const byte mac[]) {
  digitalWrite(LED_BUILTIN, HIGH);
  byte preamble[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  udp.beginPacket(ip, 9);
  udp.write(preamble, 6);
  for (uint8 i = 0; i < 16; i++) {
    udp.write(mac, 6);
  }
  udp.endPacket();
  delay(100); //Min delay between packets
  digitalWrite(LED_BUILTIN, LOW);
}

//Ping function with an initial ping poll and a secondary delayed one
bool pingHost(const IPAddress ip) {
  if(Ping.ping(ip, 1) == true){
    Serial.println("Already online");
    return true;
  }
  digitalWrite(LED_BUILTIN, HIGH);
  delay(wait_ping);
  digitalWrite(LED_BUILTIN, LOW);
  if(Ping.ping(ip, 1) == true){
    Serial.println("Online, woken up");
    return true;  
  } 
  else {
    Serial.println("Offline");
    return false;
  }
}

//Ascii to hex base
byte valFromChar(char c) {
  if(c >= 'a' && c <= 'f') return ((byte) (c - 'a') + 10) & 0x0F;
  if(c >= 'A' && c <= 'F') return ((byte) (c - 'A') + 10) & 0x0F;
  if(c >= '0' && c <= '9') return ((byte) (c - '0')) & 0x0F;
  return 0;
}

/*
* Very simple converter from a String representation of a MAC address to
* 6 bytes. Does not handle errors or delimiters, but requires very little
* code space and no libraries.
*/
void macStringToBytes(const String mac, byte *bytes) {
  if(mac.length() >= 12) {
    for(int i = 0; i < 6; i++) {
      bytes[i] = (valFromChar(mac.charAt(i*2)) << 4) | valFromChar(mac.charAt(i*2 + 1));
    }
  } else {
    Serial.println("Incorrect MAC format.");
  }
}
