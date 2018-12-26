#include <SPI.h>
#include "LedMatrix.h"
#include <ESP8266WiFi.h>

#define NUMBER_OF_DEVICES 4
#define CS_PIN 15

#define SW1_PIN D1
LedMatrix ledMatrix = LedMatrix(NUMBER_OF_DEVICES, CS_PIN);


// WiFi login parameters - network name and password
const char* ssid = "bv61";                   // edit your wifi SSID here
const char* password = "22051975";            // edit your wifi password here

// WiFi Server object and parameters
WiFiServer server(80);

const uint8_t MESG_SIZE = 255;
const uint8_t CHAR_SPACING = 1;
const uint8_t SCROLL_DELAY = 75;


#if DEBUG
#define  PRINT(s, v) { Serial.print(F(s)); Serial.print(v); }
#define PRINTS(s)   { Serial.print(F(s)); }
#else
#define PRINT(s, v)
#define PRINTS(s)
#endif

char curMessage[MESG_SIZE];
char newMessage[MESG_SIZE];
bool newMessageAvailable = false;

char WebResponse[] = "HTTP/1.1 200 OK\nContent-Type: text/html\n\n";

char WebPage[] =
"<!DOCTYPE html>" \
"<html>" \
"<head>" \
"<title>eTechPath MAX7219 ESP8266</title>" \
"<style>" \
"html, body" \ 
"{" \
"width: 600px;" \
"height: 400px;" \
"margin: 0px;" \
"border: 0px;" \
"padding: 10px;" \
"background-color: white;" \
"}" \
"#container " \
"{" \
"width: 100%;" \
"height: 100%;" \
"margin-left: 200px;" \
"border: solid 2px;" \
"padding: 10px;" \
"background-color: #b3cbf2;" \
"}" \          
"</style>"\
"<script>" \
"strLine = \"\";" \
"function SendText()" \
"{" \
"  nocache = \"/&nocache=\" + Math.random() * 1000000;" \
"  var request = new XMLHttpRequest();" \
"  strLine = \"&MSG=\" + document.getElementById(\"txt_form\").Message.value;" \
"  request.open(\"GET\", strLine + nocache, false);" \
"  request.send(null);" \
"}" \
"</script>" \
"</head>" \
"<body>" \
"<div id=\"container\">"\
"<H1><b>WiFi MAX7219 LED Matrix Display</b></H1>" \ 
"<form id=\"txt_form\" name=\"frmText\">" \
"<label>Msg:<input type=\"text\" name=\"Message\" maxlength=\"255\"></label><br><br>" \
"</form>" \
"<br>" \
"<input type=\"submit\" value=\"Send Text\" onclick=\"SendText()\">" \
"<p><b>Visit Us at</b></p>" \ 
"<a href=\"http://www.eTechPath.com\">www.eTechPath.com</a>" \
"</div>" \
"</body>" \
"</html>";

char *err2Str(wl_status_t code)
{
  switch (code)
  {
  case WL_IDLE_STATUS:    return("IDLE");           break; // WiFi is in process of changing between statuses
  case WL_NO_SSID_AVAIL:  return("NO_SSID_AVAIL");  break; // case configured SSID cannot be reached
  case WL_CONNECTED:      return("CONNECTED");      break; // successful connection is established
  case WL_CONNECT_FAILED: return("CONNECT_FAILED"); break; // password is incorrect
  case WL_DISCONNECTED:   return("CONNECT_FAILED"); break; // module is not configured in station mode
  default: return("??");
  }
}

uint8_t htoi(char c)
{
  c = toupper(c);
  if ((c >= '0') && (c <= '9')) return(c - '0');
  if ((c >= 'A') && (c <= 'F')) return(c - 'A' + 0xa);
  return(0);
}

boolean getText(char *szMesg, char *psz, uint8_t len)
{
  boolean isValid = false;  // text received flag
  char *pStart, *pEnd;      // pointer to start and end of text

  // get pointer to the beginning of the text
  pStart = strstr(szMesg, "/&MSG=");

  if (pStart != NULL)
  {
    pStart += 6;  // skip to start of data
    pEnd = strstr(pStart, "/&");

    if (pEnd != NULL)
    {
      while (pStart != pEnd)
      {
        if ((*pStart == '%') && isdigit(*(pStart+1)))
        {
          // replace %xx hex code with the ASCII character
          char c = 0;
          pStart++;
          c += (htoi(*pStart++) << 4);
          c += htoi(*pStart++);
          *psz++ = c;
        }
        else
          *psz++ = *pStart++;
      }

      *psz = '\0'; // terminate the string
      isValid = true;
    }
  }

  return(isValid);
}

void handleWiFi(void)
{
  static enum { S_IDLE, S_WAIT_CONN, S_READ, S_EXTRACT, S_RESPONSE, S_DISCONN } state = S_IDLE;
  static char szBuf[1024];
  static uint16_t idxBuf = 0;
  static WiFiClient client;
  static uint32_t timeStart;

  switch (state)
  {
  case S_IDLE:   // initialise
    PRINTS("\nS_IDLE");
    idxBuf = 0;
    state = S_WAIT_CONN;
    break;

  case S_WAIT_CONN:   // waiting for connection
    {
      client = server.available();
      if (!client) break;
      if (!client.connected()) break;

#if DEBUG
      char szTxt[20];
      sprintf(szTxt, "%03d:%03d:%03d:%03d", client.remoteIP()[0], client.remoteIP()[1], client.remoteIP()[2], client.remoteIP()[3]);
      PRINT("\nNew client @ ", szTxt);
#endif

      timeStart = millis();
      state = S_READ;
    }
    break;

  case S_READ: // get the first line of data
    PRINTS("\nS_READ");
    while (client.available())
    {
      char c = client.read();
      if ((c == '\r') || (c == '\n'))
      {
        szBuf[idxBuf] = '\0';
        client.flush();
        PRINT("\nRecv: ", szBuf);
        state = S_EXTRACT;
      }
      else
        szBuf[idxBuf++] = (char)c;
    }
    if (millis() - timeStart > 1000)
    {
      PRINTS("\nWait timeout");
      state = S_DISCONN;
    }
    break;


  case S_EXTRACT: // extract data
    PRINTS("\nS_EXTRACT");
    // Extract the string from the message if there is one
    newMessageAvailable = getText(szBuf, newMessage, MESG_SIZE);
    PRINT("\nNew Msg: ", newMessage);
    state = S_RESPONSE;
    break;

  case S_RESPONSE: // send the response to the client
    PRINTS("\nS_RESPONSE");
    // Return the response to the client (web page)
    client.print(WebResponse);
    client.print(WebPage);
    state = S_DISCONN;
    break;

  case S_DISCONN: // disconnect client
    PRINTS("\nS_DISCONN");
    client.flush();
    client.stop();
    state = S_IDLE;
    break;

  default:  state = S_IDLE;
  }
}


int highscore=0;
int sw1val = 0;

void setup() {
  Serial.begin(115200); // For debugging output
  ledMatrix.init();
  ledMatrix.setIntensity(1); // range is 0-15
  ledMatrix.clear();
  ledMatrix.setPixel(0,0);
  ledMatrix.setRotation(true);
  
  // initial state of game
  toGAME_IDLE();
  
attachInterrupt(SW1_PIN, SW1H, CHANGE);
//   
//  WiFi.begin(ssid, password);
//
//  while (WiFi.status() != WL_CONNECTED)
//  {
//    PRINT("\n", err2Str(WiFi.status()));
//    delay(500);
//  }
//  PRINTS("\nWiFi connected");
//
//  // Start the server
//  server.begin();
//  PRINTS("\nServer started");
//
//  // Set up first message as the IP address
//  sprintf(curMessage, "%03d:%03d:%03d:%03d", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);
//  
}



void SW1H(){


static unsigned long last_interrupt_time = 0;
 unsigned long interrupt_time = millis();
 // If interrupts come faster than 200ms, assume it's a bounce and ignore
 if (interrupt_time - last_interrupt_time > 30) 
 {  
  sw1val++;
 }
 last_interrupt_time = interrupt_time;

 
  
}

#define GAME_IDLE 0
#define GAME_COUNTDOWN 1
#define GAME_PLAYING 2
#define GAME_OVER 3

// duration of one game...
#define GAME_DURATION 32000

int gameStatus=GAME_IDLE;

void toGAME_IDLE(){
  ledMatrix.setTextAlignment(TEXT_ALIGN_LEFT_END);
  sprintf(curMessage,"Flip! Highscore=%d",highscore);
  ledMatrix.setText(curMessage);
  gameStatus=GAME_IDLE;
  sw1val=0;
}

int count;
void toGAME_COUNTDOWN(){
   ledMatrix.setTextAlignment(TEXT_ALIGN_LEFT);
   gameStatus=GAME_COUNTDOWN;
   count=3;
}

unsigned long gameStartedTime;
void toGAME_PLAYING(){
   ledMatrix.setTextAlignment(TEXT_ALIGN_LEFT);
   gameStatus=GAME_PLAYING;
   gameStartedTime = millis();
   sw1val=0;
}

unsigned long gameOverTime;
void toGAME_OVER(){
   ledMatrix.setTextAlignment(TEXT_ALIGN_LEFT_END);
   gameStatus=GAME_OVER;
   gameOverTime = millis();
   if (sw1val > highscore){
    highscore = sw1val;
    sprintf(curMessage,"NEW HIGH SCORE!!! %d",sw1val);
   }else{
    sprintf(curMessage,"Game Over %d",sw1val);
   }
  ledMatrix.setText(curMessage);
}

bool redrawRequested=false;
unsigned long duration,duration2,d;

void loop() {


  switch(gameStatus){
    case GAME_IDLE:
      ledMatrix.scrollTextLeft();
      redraw();
      if (sw1val>0){
        toGAME_COUNTDOWN();
      }
      break;
    case GAME_COUNTDOWN:
      sprintf(curMessage,"%d",count);
      ledMatrix.setText(curMessage);
      redraw();
      delay(1000);
      count--;
      if (count==0){
        toGAME_PLAYING();
      }
      break;   
    case GAME_PLAYING:
      duration = millis() - gameStartedTime;
      sprintf(curMessage,"%d",sw1val);        
      d = duration * 32 / GAME_DURATION;

      ledMatrix.setText(curMessage);
      ledMatrix.drawText();
      ledMatrix.setPixel(d,7);
      ledMatrix.commit();
      
      redraw();
      if (duration > GAME_DURATION){
         toGAME_OVER();
      }
      break;
    case GAME_OVER:
      duration2 = millis() - gameOverTime;
      ledMatrix.scrollTextLeft();
      redraw();
      if (duration2 > 1000*5){
         toGAME_IDLE();
      }
      break;
    }

    if(redrawRequested){
        redraw();
    }
}


void requestRedraw(){
      redrawRequested=true;
}
void redraw(){
      ledMatrix.clear();
      ledMatrix.drawText();
      ledMatrix.commit();
      redrawRequested=false;
}
