#include "SPI.h"
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"
#include "ESP8266WiFi.h"
#include "user_interface.h"

/*
    GPIO    NodeMCU   Name  |   Uno
  ===================================
     15       D8      CS    |   D10
     13       D7      MOSI  |   D11
     12       D6      MISO  |   D12
     14       D5      SCK   |   D13

     2        D4      DC    |   D9
     0        D3      RST   |   D8

*/

#define UPDATE_INTERVAL 5000
#define ESP8266
//#define DEBUG

#ifdef ESP8266
	#define TFT_MISO 12
	#define TFT_CLK  14
	#define TFT_MOSI 13
	#define TFT_DC    2
	#define TFT_RST   0
	#define TFT_CS   15
#endif

#ifdef UNO
	#define TFT_MISO 12
	#define TFT_CLK  13
	#define TFT_MOSI 11
	#define TFT_DC    9
	#define TFT_RST   8
	#define TFT_CS   10
#endif

#define SCR_WIDTH  320
#define SCR_HEIGHT 240

// ch_coord[] stores the pixel coord of the center of the 13 channels
const int ch_coord[15] = {23, 43, 64, 85, 106, 127, 148, 169, 190, 211, 232, 253, 274, 295, 314};

// ch_color[] stores the different colors of the 13 channels
const int ch_color[13] =		
				{0xF800, //red
				0x07E0, //green
				0xF81F, //magenta
				0x07FF, //cyan
				0xF810, //pink
				0xFFE0, //yellow
				0x001F, //blue
				0xF800, //red
				0x07E0, //green
				0xF81F, //magenta
				0x07FF, //cyan
				0xFFE0, //yellow
				0x001F  //blue
			};

// nr_of_netw_per_ch[] stores the amount of networks that use each channel
int nr_of_netw_per_ch[13] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

int nr_of_netw = 0;			// stores total nr of netw discoverd in current scan
int wait_state = 0;			// used for 'running' animation
char wait[5] = "/-\\|";		// used for 'running' animation
char nr_of_netw_buff[5];	// used to pad the nr of networks
bool refresh_flag = false;	// used as flag when networks are refreshed

// wrapper for controlling the tft screen
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_CLK, TFT_RST, TFT_MISO);;

// wrapper for timer used to refresh networks
os_timer_t refresh_timer;


// callback for refresh timer
void timer_callback(void *pArg) {
	refresh_flag = true;
}

void setup(){
	// start the serial communictation (used for debugging => #define DEBUG)
	Serial.begin(115200);

	// set WiFi to station mode and disconnect from an AP (if it was previously connected)
	WiFi.mode(WIFI_STA);
	WiFi.disconnect();
	delay(100);

	#ifdef DEBUG
		Serial.printf("wifi setup done\n");
	#endif
  

	// start the screen and set the correct rotation
	// pinheaders are on the left side in this case
	tft.begin();
	tft.setRotation(3);

	#ifdef DEBUG
		Serial.printf("tft setup done\n");
	#endif


	// setup the cursor for text
	tft.setCursor(0, 0);
	tft.setTextColor(ILI9341_WHITE);
	tft.setTextSize(1);
	tft.setTextWrap(false);

  // reset the screen => make is completely black
  tft.fillScreen(ILI9341_BLACK);

	// create the red top header bar
	tft.fillRect(0, 0, 320, 19, ILI9341_RED);
	tft.setCursor(34, 2);
	tft.setTextSize(2);
	tft.print("ESP8266 WiFi Analyzer");

	// show general info about the networks, show 0 netw found at startup
	update_general_netw_info(0);

	// draw the box that contains the signal triangles
	tft.drawRect(22, 30, 294, 190, ILI9341_WHITE);

	// create the vertical axis
	//	 => draw tickmarks on the vertical axis to indicate sign strength
	//  => also draw vertical lines to extend these tickmarks (in a subtle color)
	for(int i = 38; i < 218; i+= 10){
		tft.drawPixel(23, i, ILI9341_WHITE);
		tft.drawPixel(24, i, ILI9341_WHITE);

		tft.drawFastHLine(25, i, 290, 0x2104);
	}

	//  => show the signal strength on the vertical axis (y-axis values)
	//  => also, print the unit first (dBm)
	tft.setCursor(2, 22);
	tft.print("dBm");

	for(int i = 8; i <= 90; i += 10){
		tft.setCursor(2, i * 2 + 20);
		tft.print("-");
		tft.print(i + 2, DEC);
	}

	// draw tick marks on horizontal axis to idicate the channels 1-13
	for(int i = 1; i < 14; i++){
		tft.drawPixel(ch_coord[i], 218, ILI9341_WHITE);
	}
  
	// show channel numbers on horizontal axis (x-axis values)
	tft.setTextColor(ILI9341_WHITE);
	tft.setCursor(0, 223);
	tft.print("   ch");
    
	int ch_nr = 0;
	for(int i = 41; i <= 290; i += 21){
		tft.setTextColor(ch_color[ch_nr]);
		tft.setCursor(i, 223);
		tft.print(ch_nr + 1, DEC);
		ch_nr++;

		// numbers 10-13 need a slightly different alignment, which is fixed here
		if(i == 209){
			i = 206;
		}
	}

	// show nr of networks found on each channel below the channel nr
	tft.setTextColor(ILI9341_WHITE);
	tft.setCursor(0, 232);
	tft.print("#netw");

	// reset the text to it's default
	tft.setCursor(0, 0);
	tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
	tft.setTextSize(1);

	// set up the refresh timer
	os_timer_setfn(&refresh_timer, timer_callback, NULL);
	#ifdef DEBUG
		Serial.printf("timer setup done\n");
	#endif

	// start the timer
	os_timer_arm(&refresh_timer, UPDATE_INTERVAL, true);
}




void loop(void) {
//	update_wait_state();
	tft.setCursor(306, 21);
	tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
	tft.print(wait[wait_state]);

	if(wait_state < 3){
		wait_state++;
	}
	else{
		wait_state = 0;

		// draw extra pixel in the center of |, because there is no pixel there for some reason
		tft.drawPixel(308, 24, ILI9341_WHITE);
	}

	delay(100);

	if(refresh_flag){
		refresh_flag = false;

		#ifdef DEBUG
			Serial.printf("scan started\n");
		#endif

		// scan for networks
		nr_of_netw = WiFi.scanNetworks(false, true);

		#ifdef DEBUG
			Serial.printf("scan done\n");
		#endif
	  
		// update info about networks
		update_general_netw_info(nr_of_netw);

		// clear the screen
		clear_netw_screen();

		// run through all discovered networks, update the amount of networks per channel
		// and draw each network on the screen
		for(int i = 0; i < nr_of_netw; ++i){
			nr_of_netw_per_ch[WiFi.channel(i) - 1]++;
			draw_netw_str(WiFi.channel(i), WiFi.RSSI(i), WiFi.SSID(i).c_str(), WiFi.encryptionType(i) != ENC_TYPE_NONE);
		}

		// update the networks per channel counters
		update_nr_of_netw_per_ch();

		// debugging info for terminal of each discovered network
		#ifdef DEBUG
			if(nr_of_netw == 0){
				Serial.printf("no networks found\n");
			}
			else{
				Serial.printf("%d network(s) found\n", nr_of_netw);
				Serial.printf("==================\n");
				Serial.printf("nr   SSID           ch | strength | security\n");
				Serial.printf("------------------\n");

				for(int i = 0; i < nr_of_netw; ++i){
					Serial.printf("#%d: %s @ ch:%d (%ddBm) %s\n", i + 1, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i) == ENC_TYPE_NONE ? "open" : "");
				}
			}
			Serial.printf("\n");
		#endif
	 } 
}


void update_general_netw_info(int nr_of_netw){
  // setup cursor and font
  tft.setCursor(20, 20);
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);

  // pad the nr of netw with spaces on the right (always 3 char)
  sprintf(nr_of_netw_buff, "%3d", nr_of_netw);

  // print the info on the screen
  tft.print(nr_of_netw_buff);
  tft.print(" network(s) found -- suggested ch: 1, 6, 11");
}

void update_nr_of_netw_per_ch(){
  int ch_nr = 0;

  // iterate over all 13 channels
  for(int i = 35; i <= 290; i += 21){
    // update the cursor position and color, to match channel color
    tft.setCursor(i, 232);
    tft.setTextColor(ch_color[ch_nr], ILI9341_BLACK);

    // print the amount of networks in the channel, exceptions are:
    //  - no netw  => don't show a number (print spaces to overwrite prev data)
    //  - >9 netw  => print (x), since two digits are too big to display
    //  - >0 & <10 => print nr of netw
    if(nr_of_netw_per_ch[ch_nr] == 0){
      tft.print("   ");
    }
    else if(nr_of_netw_per_ch[ch_nr] > 9){
      tft.print("(x)");
    }
    else{
      tft.print("(");
      tft.print(nr_of_netw_per_ch[ch_nr], DEC);
      tft.print(")");
    }

    ch_nr++;
  }

  // reset the number of networks for all channels for the next scan
  for(int i = 0; i < 13; i++){
    nr_of_netw_per_ch[i] = 0;
  }
}

void clear_netw_screen(){
  // clear the screen
  tft.fillRect( 23, 31, 292, 188, ILI9341_BLACK);
  tft.fillRect(316, 30,   3, 189, ILI9341_BLACK); // part right of the box, where names sometimes appear
  tft.drawFastVLine(315, 30, 190, ILI9341_WHITE);

  // redraw the vertical and horizontal tickmarks
  for(int i = 38; i < 218; i+= 10){
    tft.drawPixel(23, i, ILI9341_WHITE);
    tft.drawPixel(24, i, ILI9341_WHITE);

    tft.drawFastHLine(25, i, 290, 0x2104);
  }
  for(int i = 1; i < 14; i++){
    tft.drawPixel(ch_coord[i], 218, ILI9341_WHITE);
  }
  
}

void draw_netw_str(int ch, int sig_str, const char * ssid, bool protc){
  // convert sign strength (dBm) to coordinates
  int sig_str_coord = (-sig_str * 2) + 18;

  if(sig_str_coord < 220 && sig_str_coord > 20){
    // draw the signal and redraw the bottom white line
    tft.drawTriangle(ch_coord[ch - 1], 219, ch_coord[ch], sig_str_coord, ch_coord[ch + 1], 219, ch_color[ch - 1]);
    tft.drawFastHLine(ch_coord[ch - 1], 219, 45, ILI9341_WHITE);
    
    // redraw tick marks on horizontal axis to idicate the channels 1-13
    for(int i = 1; i < 14; i++){
      tft.drawPixel(ch_coord[i], 218, ILI9341_WHITE);
    }
  
    // show SSID, strength and if the netw is protected or not (*)
    tft.setCursor(ch_coord[ch] - 5, sig_str_coord - 9);
    tft.setTextColor(ch_color[ch - 1]);
    tft.printf("%s (%ddB)", ssid, sig_str);
    if(!protc) tft.print("(*)");
  }
}
