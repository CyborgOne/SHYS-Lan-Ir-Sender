/* 
 homecontrol incl. Intertechno BT-Switch Support - v1.2
 
 Arduino Sketch für homecontrol 
 Copyright (c) 2016 Daniel Scheidler All right reserved.
 
 homecontrol ist Freie Software: Sie können es unter den Bedingungen
 der GNU General Public License, wie von der Free Software Foundation,
 Version 3 der Lizenz oder (nach Ihrer Option) jeder späteren
 veröffentlichten Version, weiterverbreiten und/oder modifizieren.
 
 homecontrol wird in der Hoffnung, dass es nÃ¼tzlich sein wird, aber
 OHNE JEDE GEWÄHRLEISTUNG, bereitgestellt; sogar ohne die implizite
 GewÃ¤hrleistung der MARKTFÄHIGKEIT oder EIGNUNG FÜR EINEN BESTIMMTEN ZWECK.
 Siehe die GNU General Public License für weitere Details.
 
 Sie sollten eine Kopie der GNU General Public License zusammen mit diesem
 Programm erhalten haben. Wenn nicht, siehe <http://www.gnu.org/licenses/>.
 */


#include <SPI.h>
#include <Ethernet.h>
#include <IRremote.h>
#include <SoftwareSerial.h>
#include <SSD1306Ascii.h>
#include <SSD1306AsciiAvrI2c.h>

// ---------------------------------------------------------------
// --                      START CONFIG                         --
// ---------------------------------------------------------------
boolean serialOut = true;

// Display
boolean useDisplay = false;
#define OLED_I2C_ADDRESS 0x3C

// Display-Timeout in ms
long displayTimeout = 60000; 

// ---------------------------------------------------------------
// --                       END CONFIG                          --
// ---------------------------------------------------------------

IRsend irsend;                 // Pin #D3 Sendediode ohne Widerstand


// Display 
SSD1306AsciiAvrI2c oled;


// Netzwerkdienste
EthernetServer HttpServer(80); 
EthernetClient interfaceClient;


// Webseiten/Parameter
char*      rawCmdAnschluss          = (char*)malloc(sizeof(char)*10);
char*      rawCmdDimmLevel          = (char*)malloc(sizeof(char)*10);
const int  MAX_BUFFER_LEN           = 80; // max characters in page name/parameter 
char       buffer[MAX_BUFFER_LEN+1]; // additional character for terminating null
 
#if defined(__SAM3X8E__)
    #undef __FlashStringHelper::F(string_literal)
    #define F(string_literal) string_literal
#endif

const __FlashStringHelper * htmlHeader;
const __FlashStringHelper * htmlHead;
const __FlashStringHelper * htmlFooter;

// ------------------ Reset stuff --------------------------
void(* resetFunc) (void) = 0;
unsigned long resetMillis;
boolean resetSytem = false;
// --------------- END - Reset stuff -----------------------



/**
 * SETUP
 *
 * Grundeinrichtung der HomeControl
 * - Serielle Ausgabe aktivieren
 * - TFT initialisieren
 * - Netzwerk initialisieren
 * - Webserver starten
 * - IN-/OUT- Pins definieren
 * - Waage initialisieren (Tara)
 */
void setup() {
  unsigned char mac[]  = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED  };
  unsigned char ip[]   = { 192, 168, 1, 13 };
  unsigned char dns[]  = { 192, 168, 1, 1  };
  unsigned char gate[] = { 192, 168, 1, 1  };
  unsigned char mask[] = { 255, 255, 255, 0  };

  // Serial initialisieren
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for Leonardo only
  }
  Serial.println(F("SmartHome yourself - IR"));
  Serial.println();

  // Display
  if(useDisplay){
    oled.begin(&Adafruit128x64, OLED_I2C_ADDRESS);
    oled.setFont(System5x7);
    oled.clear();
    oled.home();
    clearDisplay(true);
  }


  
  // Netzwerk initialisieren
  Ethernet.begin(mac, ip, dns, gate, mask);
  HttpServer.begin();

  Serial.print( F("IP: ") );
  Serial.println(Ethernet.localIP());

  initStrings();
}


/**
 * Standard Loop Methode
 */
void loop() {
   EthernetClient client = HttpServer.available();
   
  if (client) {
    while (client.connected()) {
      if(client.available()){        
        if(serialOut){
          Serial.println(F("Website anzeigen"));
        }
        showWebsite(client);
         
        delay(100);
        client.stop();
      }
    }
  }
  
  delay(100);
  
  // Gecachte URL-Parameter leeren
  memset(rawCmdAnschluss,0, sizeof(rawCmdAnschluss)); 
  memset(rawCmdDimmLevel,0, sizeof(rawCmdDimmLevel)); 
}




void switchIrOutlet(char* irCode){
    Serial.print("Sende IR-Code: ");
    Serial.println(irCode);

    unsigned long irSignal = strtoul(irCode, NULL, 16);
    
    for (int i = 0; i < 3; i++) {
        irsend.sendNEC(irSignal, 32);
        delay(40);
    }

}



// ---------------------------------------
//     Webserver Hilfsmethoden
// ---------------------------------------

/**
 *  URL auswerten und entsprechende Seite aufrufen
 */
void showWebsite(EthernetClient client){
  char * HttpFrame =  readFromClient(client);
  
 // delay(200);
  boolean pageFound = false;
  
  char *ptr = strstr(HttpFrame, "favicon.ico");
  if(ptr){
    pageFound = true;
  }
  ptr = strstr(HttpFrame, "index.html");
  if (!pageFound && ptr) {
    runIndexWebpage(client);
    pageFound = true;
  } 
  ptr = strstr(HttpFrame, "rawCmd");
  if(!pageFound && ptr){
    runRawCmdWebpage(client, HttpFrame);
    pageFound = true;
  } 

  delay(200);

  ptr=NULL;
  HttpFrame=NULL;

 if(!pageFound){
    runIndexWebpage(client);
  }
  delay(20);
}



// ---------------------------------------
//     Webseiten
// ---------------------------------------

/**
 * Startseite anzeigen
 */
void  runIndexWebpage(EthernetClient client){
  showHead(client);

  client.print(F("<h4>Navigation</h4><br/>"
    "<a href='/rawCmd'>Manuelle Schaltung</a><br>"));

  showFooter(client);
}


/**
 * rawCmd anzeigen
 */
void  runRawCmdWebpage(EthernetClient client, char* HttpFrame){
  if (strlen(rawCmdAnschluss)!=0 ) {
    postRawCmd(client, rawCmdAnschluss);
    return;
  }

  showHead(client);
  
  client.println(F(  "<h4>Manuelle Schaltung</h4><br/>"
                     "<form action='/rawCmd'>"));

  client.println(F( "<b>Anschluss: </b>" 
                    "<input type='text' name='schalte' size='10'>"));
  client.println(F( "<input type='submit' value='Abschicken'/>"
                    "</form>"));

  showFooter(client);
}


void postRawCmd(EthernetClient client, char* irCode){
  showHead(client);
    
  client.println(F( "<h4> IR-Signal senden</h4><br/>" ));
  client.print(F( "Signal: " ));
  client.println(irCode);
  switchIrOutlet(irCode);
  
  showFooter(client);
}





// ---------------------------------------
//     HTML-Hilfsmethoden
// ---------------------------------------

void showHead(EthernetClient client){
  client.println(htmlHeader);
  client.print("IP: ");
  client.println(Ethernet.localIP());
  client.println(htmlHead);
}


void showFooter(EthernetClient client){
  client.println(F("<div  style=\"position: absolute;left: 30px; bottom: 40px; text-align:left;horizontal-align:left;\" width=200>"));
 
  client.println(F("</div>"));
  client.print(htmlFooter);
}


void initStrings(){
  htmlHeader = F("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n");
  
  htmlHead = F("<html><head>"
    "<title>HomeControl</title>"
    "<style type=\"text/css\">"
    "body{font-family:sans-serif}"
    "*{font-size:14pt}"
    "a{color:#abfb9c;}"
    "</style>"
    "</head><body text=\"white\" bgcolor=\"#494949\">"
    "<center>"
    "<hr><h2>SmartHome yourself - IR</h2><hr>") ;
    
    htmlFooter = F( "</center>"
    "<a  style=\"position: absolute;left: 30px; bottom: 20px; \"  href=\"/\">Zurueck zum Hauptmenue;</a>"
    "</body></html>");
    
}


// ---------------------------------------
//     Ethernet - Hilfsmethoden
// ---------------------------------------
/**
 * Zum auswerten der URL des Ã¼bergebenen Clients
 * (implementiert um angeforderte URL am lokalen Webserver zu parsen)
 */
char* readFromClient(EthernetClient client){
  char paramName[20];
  char paramValue[20];
  char pageName[20];
  
  if (client) {
  
    while (client.connected()) {
  
      if (client.available()) {
        memset(buffer,0, sizeof(buffer)); // clear the buffer

        client.find("/");
        
        if(byte bytesReceived = client.readBytesUntil(' ', buffer, sizeof(buffer))){ 
          buffer[bytesReceived] = '\0';

          if(serialOut){
            Serial.print(F("URL: "));
            Serial.println(buffer);
          }
          
          if(strcmp(buffer, "favicon.ico\0")){
            char* paramsTmp = strtok(buffer, " ?=&/\r\n");
            int cnt = 0;
            
            while (paramsTmp) {
            
              switch (cnt) {
                case 0:
                  strcpy(pageName, paramsTmp);
                  if(serialOut){
                    Serial.print(F("Domain: "));
                    Serial.println(buffer);
                  }
                  break;
                case 1:
                  strcpy(paramName, paramsTmp);
                
                  if(serialOut){
                    Serial.print(F("Parameter: "));
                    Serial.print(paramName);
                  }
                  break;
                case 2:
                  strcpy(paramValue, paramsTmp);
                  if(serialOut){
                    Serial.print(F(" = "));
                    Serial.println(paramValue);
                  }
                  pruefeURLParameter(paramName, paramValue);
                  break;
              }
              
              paramsTmp = strtok(NULL, " ?=&/\r\n");
              cnt=cnt==0?1:cnt==1?2:1;
            }
          }
        }
      }// end if Client available
      break;
    }// end while Client connected
  } 

  return buffer;
}


void pruefeURLParameter(char* tmpName, char* value){
  if(strcmp(tmpName, "schalte")==0 && strcmp(value, "")!=0){
    strcpy(rawCmdAnschluss, value);
    
    if(serialOut){
      Serial.print(F("Check IR-Code: "));
      Serial.println(rawCmdAnschluss);    
    }
  }  
  if(strcmp(tmpName, "dimm")==0 && strcmp(value, "")!=0){
    strcpy(rawCmdDimmLevel, value);
    
    if(serialOut){
      Serial.print(F("Dimm-Level: "));
      Serial.println(rawCmdAnschluss);    
    }
  }  
}



// ---------------------------------------
//     Display - Hilfsmethoden
// ---------------------------------------

void clearDisplay(boolean fullClear){
  if(useDisplay){
    oled.clear();
    
    if (!fullClear){
      oled.setFont(Verdana12_bold);
      oled.setCursor(0, 1);
      oled.print(F("SmartHome yourself"));
      
      oled.setFont(System5x7);
      oled.setCursor(0, 2);
      oled.print(F("IR Sender"));
  
      oled.setCursor(0, 7);
      oled.print(F("IP: "));
      oled.print(Ethernet.localIP());
    }
  }
}



// ---------------------------------------
//     Allgemeine Hilfsmethoden
// ---------------------------------------
char* int2bin(unsigned int x){
  static char buffer[6];
  for (int i=0; i<5; i++) buffer[4-i] = '0' + ((x & (1 << i)) > 0);
  buffer[68] ='\0';
  return buffer;
}
