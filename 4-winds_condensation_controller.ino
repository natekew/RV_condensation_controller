/* DHTServer - ESP8266 Webserver with a DHT sensor as an input
   Based on ESP8266Webserver, and DHTexample (thank you)
   --
   Static IP Address must be set by the router and is assigned to
   the ESP8266 by it's mac address - nk
   
   	If you are interested in a simpler calculation that gives an approximation of dew point temperature
	if you know the observed temperature and relative humidity, the following formula was proposed in a 
	2005 article by Mark G. Lawrence in the Bulletin of the American Meteorological Society:  
	Td = T - ((100 - RH)/5.) 
	where Td is dew point temperature (in degrees Celsius), T is observed temperature (in degrees Celsius)
	and RH is relative humidity (in percent). Apparently this relationship is fairly accurate for relative 
	humidity values above 50%.
	Lawrence, Mark G., 2005: The relationship between relative humidity and the dewpoint temperature in 
	moist air: A simple conversion and applications. Bull. Amer. Meteor. Soc., 86, 225-233. 
	doi: http;//dx.doi.org/10.1175/BAMS-86-2-225 
	-- Michael Bell
*/

//tell the compiler what header files to include

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DHT.h>

// tell the compiler to define:

#define DHTTYPE DHT22													// the DHT22 as the sensor type referenced in the DHT.h file
#define DHTPIN  2													    // the MCU pin-2 as the input from the sensor
#define D6 12 															  // use if the pin-12 does not map correctly
#define AcPin 12															// the MCU pin-12 as the control signal for the heater power tail

//Initializations

ESP8266WebServer server(80);                  // Initialize web server to listen on port 80
DHT dht(DHTPIN, DHTTYPE, 11);                 // Initialize sensor, 11 works fine for ESP8266 
																	            // (but should not be necessary on recent sensors that will adjust themselves)
/*--------Initialize some variables----------- */

const char* ssid     = "your ssid";									// ssid of the local router
const char* password = "your password";							// password for the router
const int varPinOn = 0;												      // 3.3 volts pulls relays in the AC power tail on
const int varPinOff = 1;                            // 1.8-volts is insufficient to hold the relays on, so the power tail turns off                                                                    
bool varReadYet = false;											      // used to determine if the sensor was read for the firt time
int varMaxTemp = 18;												        // arbitrary maximum temperature limit
int varMinTemp = 10;												        // arbitrary minimum temperature limit
int varDewPointBuffer = 2;                          // the buffer determines how close to the dew point the temperature is allowed to fall
float varFltHumid, varFltTemp, varFltDewPoint;      // Numeric values read from local sensor and calculated reletive humidity
unsigned long varTimeRunning = millis();            // Time now in milliseconds
unsigned long varLastReadTime = 0;                  // will store time in milliseconds that last sensor reading occured
const long varReadInterval = 180000;                // interval in milliseconds to read sensor (3-minutes)
String varHeaterState = "off";                      // used to communicate the heater status on the web page

/*--------Start setup----------- */

void setup() {
	server.on("/", send_index);                       // on client request for index page goto send_index
    dht.begin();                                    // start the temperature/humidity sensor
    pinMode(AcPin, OUTPUT);											    // set the AcPin to output mode
	varReadYet = false;                               // make sure the boolean is false as the sensor is not yet read
    WiFi.begin(ssid, password);                     // Connect to WiFi network 
    server.begin();                                 // start the web server
 }  																                //End setup

 /*--------Run the program----------- */
 
void loop(){                                                       	// start the main program loop
  varTimeRunning = millis();                                       	// how long the loop has run in milliseconds
  
  if (!varReadYet){                                                	// if the boolean is false this is the first time in the loop so:
    delay(1000);                                                   	// pause for a second before 1st read of the sensor or you might get garbage
    readsensors();                                              	  // read temperature for 1st time.
	varReadYet = !varReadYet;                                      	  // change boolean from false to true
  }                                                                	// end if
  
  if ((varTimeRunning - varLastReadTime) > varReadInterval){       	// if the read interval is reached:
    readsensors();                                              	  // read sensor again
  }                                                                	// end if

  server.handleClient();                                           	// create a web page if web client makes a request
}                                                                  	// end the main program loop
 
 /*--------read the sensor ----------- */

void readsensors(){                                           		  // start reading temperature into variables
 
	varFltHumid = dht.readHumidity();                           	    // Read varHumidity in percent
    varFltTemp = dht.readTemperature(false);                    	  // Read the temperature, true is Fahrenheit, false is Centigrade
                             
    if (isnan(varFltHumid) || isnan(varFltTemp)){
     return;                                                    	  // Check if any reads failed and exit early if so.
    }                                                             	// end if
    
  varLastReadTime = millis();                                   	  // mark the time of this reading
  varFltDewPoint = varFltTemp - ((100 - varFltHumid) / 5);        	// equation from Mark G. Lawrence mentioned above
  heaterlogic();													                          // should the heater be on or off
}                                                               	  // end readsensors

/*--------Decide heater on/off----------- */

void heaterlogic(){													                      // start heater on or off decision

	float varDewSafety = (varFltDewPoint + varDewPointBuffer);      // define some local variables
	
	if (varFltTemp <= varMinTemp){									                // if the temperature is less than the minimum allowed
		varHeaterState = "On - Min-T reached";						            // set the HTML text variable to On
		digitalWrite(AcPin, varPinOn);								                // turn the heater on
		return;														                            // exit heaterlogic
	}																                                // end if
																	
	if (varFltTemp >= varMaxTemp){									                // if the temperature is above the maximum allowed
		varHeaterState = "Off - Max-T reached";						            // set the HTML text variable to Off
		digitalWrite(AcPin, varPinOff);								                // turn the heater off
		return;														                            // exit heaterlogic
	}																                                // end if
																	
	if (varFltTemp < varDewSafety){									                // if the temperature is close to the dewpoint
		varHeaterState = "On BDP reached";							              // set the HTML text variable to On
		digitalWrite(AcPin, varPinOn);								                // turn the heater on
		return;														                            // exit heaterlogic
	}																                                // end if
																	
	if (varFltTemp > varDewSafety){	                								// if the temperature is above the dewpoint
		varHeaterState = "Off - BDP reached";					              	// set the HTML text variable to Off
		digitalWrite(AcPin, varPinOff);								                // turn the heater off
		return;														                            // exit heaterlogic
	}																                                // end if	
	
}																	                                // end heater logic 

/*--------Write the root web page ----------- 
Notes: If you use quotes enclosed in quotes, you must use the escape "\" charecter
for example \"
The HTML string and variable concatenation works fine as is. Inserting concatenation
instructions such as + or =+ at the beging or end of each line may cause errors in
the delivery of the web page.
*/


void send_index() {                                                	
  
  String buffy = "<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\">"
"<html>"
  "<head>"
    "<meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\">"
    "<title>rvcontroller</title>"
  "</head>"
  "<body link=\"#0000EE\" vlink=\"#551A8B\" bgcolor=\"#66cccc\" alink=\"#66ffff\""
    "text=\"#000000\">"
    "<table cellspacing=\"2\" cellpadding=\"2\" align=\"center\" width=\"80%\""
      "border=\"2\">"
      "<tbody>"
        "<tr align=\"center\">"
          "<td valign=\"top\" bgcolor=\"#6600cc\"><font size=\"+4\""
              "color=\"#ccffff\">RV Condensation Control</font><br>"
          "</td>"
        "</tr>"
        "<tr>"
		  //"<td valign=\"top\"><font color=\"#ccffff\"><font size=\"+4\""
          "<td valign=\"top\"><font size=\"+4\""
                ">Temperature: " + String((int)varFltTemp) + " C</font><br>"
            "</font></td>"
        "</tr>"
        "<tr>"
          "<td valign=\"top\"><font size=\"+4\">Humidity: " + String((int)varFltHumid) + " percent</font><br>"
          "</td>"
        "</tr>"
        "<tr>"
          "<td valign=\"top\"><font size=\"+4\">Dew Point: " + String((int)varFltDewPoint) + "C</font><br>"
          "</td>"
        "</tr>"
        "<tr>"
          "<td valign=\"top\"><font size=\"+4\">Heater: " + varHeaterState + "</font><br>"
          "</td>"
        "</tr>"
        "<tr>"
          "<td valign=\"top\"><br>"
          "</td>"
        "</tr>"
        "<tr>"
          "<td valign=\"top\">"
            "<p><font size=\"+4\">Variables:<br>"
            "</p>"
            "<ul>"
              "<li>Max Temp: " + varMaxTemp + " C</li>"
              "<li>Min Temp: " + varMinTemp + "C</li>"
              "<li>Dew Point Buffer: " + varDewPointBuffer + "C</li>"
              "<li>Sensing Interval: " + String(varReadInterval/60000) + " Minutes</li>"
            "</ul>"
			"<p></p></font>"
			"<p></p><font size=\"+2\">"
			"<p></p>NK-rev-10"
          "</td>"
        "</tr>"
      "</tbody>"
    "</table>"
    "<br>"
  "</body>"
"</html>"
; 
				 
  server.send(200, "text/html", buffy);														// send the string to the server

}  																			
