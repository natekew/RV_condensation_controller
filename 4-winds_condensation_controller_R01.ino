/*
	DHTServer - ESP8266 Webserver with a DHT sensor as an input
   	Based on ESP8266Webserver, and DHTexample (thank you).
	For calculating the dewpoint above %50, the following
	formula was proposed in a 2005 article by
	Mark G. Lawrence in the Bulletin of the American Meteorological
	Society: Td = T - ((100 - RH)/5.)
*/

//tell the compiler what header files to include

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>				// Webserver
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <DHT.h>							// Sensor
#include <FS.h>								// SPIFFS File System

#ifndef STASSID
	#define STASSID "mySSID"
	#define STAPSK  "myPassword"
#endif

#define DHTTYPE DHT22						// the DHT22 as the sensor type referenced in the DHT.h file
#define DHTPIN  2							// the MCU pin-2 as the input from the sensor
#define D6 12 								// use if the MCU pin-12 does not map correctly
#define AcPin 12							// the MCU pin-12 as the control signal for the heater's power tail

//Initializations

ESP8266WebServer server(80);                // Initialize web server to listen on port 80
DHT dht(DHTPIN, DHTTYPE, 11);               // Initialize sensor, 11 works fine for ESP8266
											// (but should not be necessary on recent sensors that will adjust themselves)

/*--------Initialize some global variables----------- */

const char* ssid = STASSID;
const char* password = STAPSK;
const int varPinOn = 0;								// 3.3 volts pulls relays in the AC power tail on
const int varPinOff = 1;                            // 1.8-volts is insufficient to hold the relays on, so the power tail turns off
int varReadYet = 0;									// used to determine if the sensor was read for the firt time
int varMaxTemp = 18;								// arbitrary maximum temperature limit
int varMinTemp = 10;								// arbitrary minimum temperature limit
int varDewPointBuffer = 2;                          // the buffer determines how close to the dew point the temperature is allowed to fall
float varFltHumid, varFltTemp, varFltDewPoint;      // Numeric values read from local sensor and calculated reletive humidity
unsigned long varTimeRunning = millis();            // Time now in milliseconds
unsigned long varLastReadTime = 0;                  // will store time in milliseconds that last sensor reading occured
const long varReadInterval = 180000;                // interval in milliseconds to read sensor (3-minutes)
String varHeaterState = "off";                      // used to communicate the heater status on the web page
String varReason = "";								// used by the heaterlogic to provide an explanation for the heater state

/*--------Start setup----------- */

void setup() {
	//Serial.begin(115200);
	/* https://arduino-esp8266.readthedocs.io/en/latest/ota_updates/readme.html*/

	WiFi.mode(WIFI_STA);									// copied from readthedocs
	WiFi.begin(ssid, password);								// copied from readthedocs
	while (WiFi.waitForConnectResult() != WL_CONNECTED) {	// copied from readthedocs
		Serial.println("Connection Failed! Rebooting...");	// copied from readthedocs
    	delay(5000);										// copied from readthedocs
    	ESP.restart();										// copied from readthedocs
	}
  	// Port defaults to 8266
	// ArduinoOTA.setPort(8266);

  	// Hostname defaults to esp8266-[ChipID]
  	//ArduinoOTA.setHostname("testing123");

  	// No authentication by default
  	//ArduinoOTA.setPassword("myNextPassword"); //*************
  	/* Note: SPIFFS data upload fails when authentication is enabled
	upload attempt returns error "Authentication Failed" */

	ArduinoOTA.onStart([]() {								// copied from readthedocs
		String type;										// copied from readthedocs
    	if (ArduinoOTA.getCommand() == U_FLASH) {			// copied from readthedocs
      		type = "sketch";								// copied from readthedocs
    	}
		else { // U_FS										// copied from readthedocs
      		type = "filesystem";							// copied from readthedocs
    	}

    	// NOTE: if updating FS this is the place to unmount FS using FS.end();
		SPIFFS.end();

	});

	ArduinoOTA.onEnd([]() {													// copied from readthedocs
    	Serial.println("\nEnd");											// copied from readthedocs
  	});

	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {	// copied from readthedocs
    	Serial.printf("Progress: %u%%\r", (progress / (total / 100)));		// copied from readthedocs
  	});

	ArduinoOTA.onError([](ota_error_t error) {
    	Serial.printf("Error[%u]: ", error);

		if (error == OTA_AUTH_ERROR) {
    		Serial.println("Auth Failed");
    	}
		else if (error == OTA_BEGIN_ERROR) {
    		Serial.println("Begin Failed");
    	}
		else if (error == OTA_CONNECT_ERROR) {
    		Serial.println("Connect Failed");
    	}
		else if (error == OTA_RECEIVE_ERROR) {
    		Serial.println("Receive Failed");
    	}
		else if (error == OTA_END_ERROR) {
    		Serial.println("End Failed");
    	}
  	});							// copied from readthedocs

	ArduinoOTA.begin();

	SPIFFS.begin();									// start the Serial Peripheral Interface File Folder System
	read_Variable("/maxtemp.txt");					// read the maximum temperature threshhold from SPIFFS
	read_Variable("/mintemp.txt");					// read the minimum temperature threshhold from SPIFFS
	server.on("/", send_index);                     // handles client request for index page - goto send_index
	server.on("/2fe6a97dcf5264180a167e80fc8a45eb", send_variable_form);		// handles client request for setting variables
	server.on("/setmintemp", set_Min_Temp);			// handles button action from set variables form
	server.on("/setmaxtemp", set_Max_Temp);			// handles button action from set variables form
	server.onNotFound(handleOther);					// handles client requests not defined above
    dht.begin();                                    // start the temperature/humidity sensor
    pinMode(AcPin, OUTPUT);							// set the AcPin to output mode
    WiFi.begin(ssid, password);                     // Connect to WiFi network
    server.begin();                                 // start the web server

}

/*--------Run the program----------- */

void loop() {
	ArduinoOTA.handle();												// start the OTA handler
	varTimeRunning = millis();                                       	// update how long the loop has run in milliseconds

	if (varReadYet < 1 ){                                               // if not read yet, this is the first time in the loop so:
		delay(1000);                                                   	// pause for a second before 1st read of the sensor or you might get garbage
		readsensors();                                              	// read temperature for 1st time.
		varReadYet = 1;                                      			// change boolean from false to true
	}

	if ((varTimeRunning - varLastReadTime) > varReadInterval){       	// if the read interval is reached:
		readsensors();                                              	// read sensor again
	}

	server.handleClient();                                           	// create a web page if web client makes a request

}

/*--------read the temperature and humidity sensor ----------- */

void readsensors(){

   varFltHumid = dht.readHumidity();                           		// Read varHumidity in percent
   varFltTemp = dht.readTemperature(false);                    		// Read the temperature, true is Fahrenheit, false is Centigrade

   if (isnan(varFltHumid) || isnan(varFltTemp)){					// Check if any reads failed and
	   return;                                                    	//exit early if so.
   }

   varLastReadTime = millis();                                   	// mark the time of this sensor reading
   varFltDewPoint = varFltTemp - ((100 - varFltHumid) / 5);      	// equation from Mark G. Lawrence mentioned above
   heaterlogic();													// decide if the heater should be on or off
}

/*--------Decide if the heater needs to be on/off----------- */

void heaterlogic(){

   float varDewSafety = (varFltDewPoint + varDewPointBuffer);      	// define a local variable

   if (varFltTemp <= varMinTemp){									// if the temperature is less than the minimum allowed
	   varHeaterState = "On";										// set the HTML text variable to On
	   varReason = "Minimum temperature reached";					// with a note explaining why
	   digitalWrite(AcPin, varPinOn);								// turn the heater on
	   return;														// exit heaterlogic
   }

   if (varFltTemp >= varMaxTemp){									// if the temperature is above the maximum allowed
	   varHeaterState = "Off";										// set the HTML text variable to Off
	   varReason = "Maximum temperature reached";					// with a note explaining why
	   digitalWrite(AcPin, varPinOff);								// turn the heater off
	   return;														// exit heaterlogic
   }

   if (varFltTemp < varDewSafety){									// if the temperature is close to the dewpoint
	   varHeaterState = "On";										// set the HTML text variable to On
	   varReason = "Temperature below buffered dewpoint";					// with a note explaining why
	   digitalWrite(AcPin, varPinOn);								// turn the heater on
	   return;														// exit heaterlogic
   }

   if (varFltTemp > varDewSafety){	                				// if the temperature is above the dewpoint
	   varHeaterState = "Off";									    // set the HTML text variable to Off
	   varReason = "Temperature above buffered dewpoint";					// with a note explaining why
	   digitalWrite(AcPin, varPinOff);								// turn the heater off
	   return;														// exit heaterlogic
   }
}

/*--------Set the maximum temperature----------- */

void set_Max_Temp() {
   String maxTemp = server.arg("maxTemp");							// receive setting from the set variables page
   update_Variable("/maxtemp.txt", maxTemp);						// update the global variable
   send_variable_form();											// resend the updte the form with the new setting
}

/*--------Set the minimum temperature----------- */

void set_Min_Temp() {
   String minTemp = server.arg("minTemp");							// receive setting from the set variables page
   update_Variable("/mintemp.txt", minTemp);						// update the global variable
   send_variable_form();											// resend the updte the form with the new setting
}

/*--------Call functions to update variables----------- */

void update_Variable(String myFile, String myString) {
   write_Variable(myFile, myString);								// write the new value to file
   read_Variable(myFile);											// read the file into the correct variable
}

/*--------Write variables to SPIFFS----------- */

void write_Variable(String myFile, String myString) {
   File f = SPIFFS.open(myFile, "w");								// open the file for writing
   f.println(myString);												// print the value into the file
   f.close();														// close the file
}

/*--------Read varibles from SPIFFS and update variables----------- */

void read_Variable(String myFile) {
   File f = SPIFFS.open(myFile, "r");								// open the file for reading
   String line = f.readStringUntil('\n');							// read the value into the string 'line'
   f.close();														// close the file

   if (myFile=="/maxtemp.txt"){										// if the file name is maxtemp.txt
		varMaxTemp = line.toInt();									// update the variable
	}
	if (myFile=="/mintemp.txt"){									// if the file name is maxtemp.txt
		varMinTemp = line.toInt();									// update the variable
	}
}

/* 	Thanks to Stensat Org for the following lesson Authentication
	handles requests from the served webpage for supporting files;
	http://www.stensat.org/docs/sys395/16_simple_webserver.pdf
*/
void handleOther() {
   String path = server.uri();										// copy the Uniform Resource Identifier into a variable
   String dataType = "text/plain";									// initialize the dataType variable
   //if(path.endsWith("/")) path = "/index.html";					// not used here
   //if(path.endsWith(".jpg")) dataType = "image/jpeg";				// not used here
   /*else*/ if(path.endsWith(".png")) dataType = "image/png";
   //else if(path.endsWith(".html")) dataType = "text/html";		// not used here
   //Serial.println(path.c_str());									// not used here
   File datafile = SPIFFS.open(path.c_str(),"r");					// open the requested file
   //Serial.println(datafile);										// not used here
   server.streamFile(datafile,dataType);							// stream the requested file
   datafile.close();												// close the requested file
}


/*--------Stream a file to the client----------- */

void server_stream(String myFile) {
File f = SPIFFS.open(myFile, "r");									// open the file for reading
server.streamFile(f, "text/html");									// stream the file
f.close();															// close the file
}

/*--------Update and send the root web page -----------*/

void send_index() {

   String holder[]={"%TEMPERATURE%",								// make a String array of the place holders in the webpage
				   "%HUMIDITY%",
				   "%DEWPOINT%",
				   "%HEATERSTATE%",
				   "%REASON%",
				   "%MAXTEMP%",
				   "%MINTEMP%",
				   "%DPBUFFER%",
				   "%SENSINTERVAL%"
   };

   String value[]={String((int)varFltTemp),							// make a String array of the values to place in the webpage
				   String((int)varFltHumid),
				   String((int)varFltDewPoint),
				   varHeaterState,
				   varReason,
				   String(varMaxTemp),
				   String(varMinTemp),
				   String(varDewPointBuffer),
				   String(varReadInterval / 60000)
   };

   File f1 = SPIFFS.open("/in.html", "r");							// open the web page file for reading
   String webP = f1.readString();									// read the file into a String variable
   f1.close();														// close the file

   int i = 0;														// set a counter to zreo
   while (i < 9){													// while the counter value is less than
	   webP.replace(holder[i], value[i]);							// replace each place-holder with a value
	   i++;															// increase the counter value by one
   }

   write_Variable("/out.html", webP);								// write the String to a file called out.html
   server_stream("/out.html");										// stream the new file to the client

}

/*--------Update and send the set variables page----------- */

void send_variable_form() {

   String holder[]={"%MAXTEMP%",									// make a String array of the place holders in the webpage
				   "%MINTEMP%"
   };

   String value[]={String(varMaxTemp),								// make a String array of the values to place in the webpage
				   String(varMinTemp),
   };

   File f2 = SPIFFS.open("/var.html", "r");							// open the web page file for reading
   String webP = f2.readString();									// read the file into a String variable
   f2.close();														// close the file
Serial.println(webP);
   int i = 0;														// set a counter to zreo
   while (i < 2){													// while the counter value is less than
	   webP.replace(holder[i], value[i]);							// replace each place-holder with a value
	   i++;															// increase the counter value by one
   }
   write_Variable("/vout.html", webP);								// write the String to a file called out.html
   server_stream("/vout.html");										// stream the new file to the client

}
