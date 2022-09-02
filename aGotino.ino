
const long SERIAL_SPEED = 9600;          // serial interface baud. Make sure your computer/phone matches this
//long MAX_RANGE = 1800;                   // default max slew range in deg minutes (1800'=30°). See +range command


/*
 * It is safe to keep the below untouched
 * 
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
bool DEBUG = false;
#include "catalogs.h" // load objects for aGoto protocol (Star List, Messier, NGC)

const long DAY_SECONDS =  86400; // secs in a day
const long NORTH_DEC   = 324000; // 90°

// Current coords in Secs (default to true north)
long currRA  = 0;     
long currDEC = NORTH_DEC;


// Serial Input
char input[20];     // stores serial input
int  in = 0;        // current char in serial input
// Serial Input (New) coords
long inRA    = 0;
long inDEC   = 0;

// Current position in Meade lx200 format, see updateLx200Coords()
String lx200RA  = "00:00:00#";
String lx200DEC = "+90*00:00#";

String _aGotino = "aGotino";
const unsigned long _ver = 210708;

//Define time setup constant
#define TIME_MSG_LEN 11 // time sync to PC is HEADER followed by Unix time_t as ten ASCII digits
#define TIME_HEADER 'T' // Header tag for serial time sync message
#define TIME_REQUEST 7 // ASCII bell character requests a time sync message

//Define some necessary variables to calculate alt and az
#define longi 106.660172
#define lat 10.762622
#define j2kyear 8035.99999107639

#include <Servo.h>

#include <TimeLib.h>
Servo myservoAz;
Servo myservoAlt;
void setup() {
  Serial.begin(SERIAL_SPEED);
  Serial.print(_aGotino);
  lx200DEC[3] = char(223); // set correct char in string as per earlier specs - FIXME: this should not be needed anymore
  Serial.println(" ready.");
  myservoAlt.attach(8);
  myservoAz.attach(9);  
//  myservoAz.write(0);
//  delay(500);
//  myservoAlt.write(0);
  delay(500);
}

int slewRaDecBySecs(float alt, float az) {
//  myservoAlt.write((long)alt);
//  delay(500);
  long azLong = (long) az;
  myservoAz.write(azLong);
  delay(500);
  // Success
  return 1;
}



/* 
 *  Basic Meade LX200 protocol
 */
void lx200(String s) { // all :.*# commands are passed here 
  if (s.substring(1,3).equals("GR")) { // :GR# 
    printLog("GR");
    // send current RA to computer
    Serial.print(lx200RA);
  } else if (s.substring(1,3).equals("GD")) { // :GD# 
    printLog("GD");
    // send current DEC to computer
    Serial.print(lx200DEC);
  } else if (s.substring(1,3).equals("GV")) { // :GV*# Get Version *
    char c = s.charAt(3); 
    if ( c == 'P') {// GVP - Product name
       Serial.print(_aGotino);  
    } else if (c == 'N') { // GVN - firmware version
       Serial.print(_ver);  
    }
    Serial.print('#');
  } else if (s.substring(1,3).equals("Sr")) { // :SrHH:MM:SS# or :SrHH:MM.T# // no blanks after :Sr as per Meade specs
    printLog("Sr");
    // this is INITAL step for setting position (RA)
    long hh = s.substring(3,5).toInt();
    long mi = s.substring(6,8).toInt();
    long ss = 0;
    if (s.charAt(8) == '.') { // :SrHH:MM.T#
      ss = (s.substring(9,10).toInt())*60/10;
    } else {
      ss = s.substring(9,11).toInt();
    }
    inRA = hh*3600+mi*60+ss;
    Serial.print(1); // FIXME: input is not validated
  } else if (s.substring(1,3).equals("Sd")) { // :SdsDD*MM:SS# or :SdsDD*MM#
    printLog("Sd");
    // this is the FINAL step of setting a pos (DEC) 
    long dd = s.substring(4,6).toInt();
    long mi = s.substring(7,9).toInt();
    long ss = 0;
    if (s.charAt(9) == ':') { ss = s.substring(10,12).toInt(); }
    inDEC = (dd*3600+mi*60+ss)*(s.charAt(3)=='-'?-1:1);
    // FIXME: the below should not be needed anymore since :CM# command is honored
    if (currDEC == NORTH_DEC) { // if currDEC is still the initial default position (North)
      // assume this is to sync current position to new input
      currRA  = inRA;
      currDEC = inDEC;
      updateLx200Coords(currRA, currDEC);
      // recompute strings
    }
    Serial.print(1); // FIXME: input is not validated
  } else if (s.charAt(1) == 'M') { // MOVE:  :MS# (slew), :Mx# (slow move)
    if (s.charAt(2) == 'S' ) { // SLEW
      printLog("MS");
      // assumes Sr and Sd have been processed hence
      // inRA and inDEC have been set, now it's time to move
      long deltaRaSecs  = currRA-inRA;
      long deltaDecSecs = currDEC-inDEC;
      // FIXME: need to implement checks, but can't wait for slewRaDecBySecs
      //        reply since it may takes several seconds:
      Serial.print(0); // slew is possible 
      //Convert deltaRA and deltaDEC to deltaALT and deltaAZ
      float currAlt = convertALT(inRA, inDEC);
      float currAZ = convertAZ(inRA, inDEC);
      // slewRaDecBySecs replies to lx200 polling with current position until slew ends:
      if (slewRaDecBySecs(currAlt, currAZ) == 1) { // success         
        currRA  = inRA;
        currDEC = inDEC;
        updateLx200Coords(currRA, currDEC); // recompute strings
      } else { // failure
        Serial.print("1Range_too_big#");
      }
    } 
  } else if (s.charAt(1) == 'Q') { // :Q# or :Qx# stop Dec Motor and set RA to Tracking
    
  } else if (s.substring(1,3).equals("CM")) { // :CM# sync
    // assumes Sr and Sd have been processed
    // sync current position with input
    printLog("CM");
    currRA  = inRA;
    currDEC = inDEC;
    Serial.print("Synced#");
    updateLx200Coords(currRA, currDEC); // recompute strings
  }
}

/*Function to calculate ALT*/
float convertALT(long RA, long dec){
  float decindeg = convertDegindeg(dec);
  float ha = convertHa(RA);
  float sinALT = sin(rad(decindeg))*sin(rad(lat)) + cos(rad(decindeg))*cos(rad(lat))*cos(rad(ha));
  float alt = asin(sinALT)* 180 /  3.14159265358979323846;
  return alt;
}
float convertAZ(long RA, long dec){
  float alt = rad(convertALT(RA, dec));
  float decindeg = convertDegindeg(dec);
  float cosAZ = (sin(rad(decindeg)) - sin(alt)*sin(rad(lat))) / (cos(alt)*cos(rad(lat)));
  float az = acos(cosAZ)* 180 /  3.14159265358979323846;
  float ha = convertHa(RA);
  if(sin(rad(ha)) >= 0) {
    az = 360 - az;
  }
  return az;
}

// Convert decindeg
float convertDegindeg(long dec){
  long pp = abs(dec)/3600;
  if (dec<0) pp = - pp;
  long mi = (abs(dec)-pp*3600)/60;
  long ss = (abs(dec)-mi*60-pp*3600);
  return (pp + (float) mi/60.00 + (float) ss/3600.00);
}

// Convert ha
float convertHa(long RA) {
    long pp = RA/3600;
  long mi = (RA-pp*3600)/60;
  long ss = (RA-mi*60-pp*3600);
  float RAindeg = 15*(pp + (float) mi/60.00 + (float) ss/3600.00);
    long hours = (long) hour();
  long mins = (long) minute();
  long secs = (long) second();
  float timehr = ((float)hours + (float) mins/60.00 + (float) secs/3600.00) / 24.00;
  long days = (long) month();
  switch(days){
    case 1:
      days= 0;
      break;
    case 2:
      days = 31;
      break;
    case 3:
      days = 59;
      break;
    case 4:
      days = 90;
      break;
    case 5:
      days = 120;
      break;
    case 6:
      days = 151;
      break;
    case 7:
      days = 181;
      break;
    case 8:
      days = 212;
      break;
    case 9:
      days = 243;
      break;
    case 10:
      days = 273;
      break;
    case 11:
      days = 304;
      break;
    case 12:
      days = 334;
      break;
  }
  days += (long) day();
  float J2k = (float) days + j2kyear + timehr;
  float LST = 100.46 + 0.985647 * J2k + longi + 15*((float)hours + (float) mins/60.00 + (float) secs/3600.00);
  if(LST<0) LST= LST*(-1);
  while(LST>=360){
      LST-=360;
    }
  float ha = LST - RAindeg;
  if (ha<0) ha+=360;
  return ha;
}

//Convert degreez to radians
float rad(double a){
    return a* 3.14159265358979323846 /180;
}

/* Update lx200 RA&DEC string coords so polling 
 * (:GR# and :GD#) gets processed faster
 */
void updateLx200Coords(long raSecs, long decSecs) {
  unsigned long pp = raSecs/3600;
  unsigned long mi = (raSecs-pp*3600)/60;
  unsigned long ss = (raSecs-mi*60-pp*3600);
  lx200RA = "";
  if (pp<10) lx200RA.concat('0');
  lx200RA.concat(pp);lx200RA.concat(':');
  if (mi<10) lx200RA.concat('0');
  lx200RA.concat(mi);lx200RA.concat(':');
  if (ss<10) lx200RA.concat('0');
  lx200RA.concat(ss);lx200RA.concat('#');

  pp = abs(decSecs)/3600;
  mi = (abs(decSecs)-pp*3600)/60;
  ss = (abs(decSecs)-mi*60-pp*3600);
  lx200DEC = "";
  lx200DEC.concat(decSecs>0?'+':'-');
  if (pp<10) lx200DEC.concat('0');
  lx200DEC.concat(pp);lx200DEC.concat(char(223)); // FIXME: may be just * nowadays
  if (mi<10) lx200DEC.concat('0'); 
  lx200DEC.concat(mi);lx200DEC.concat(':');
  if (ss<10) lx200DEC.concat('0');
  lx200DEC.concat(ss);lx200DEC.concat('#');
 } 

/* 
 * Print current state in serial  
 */
void printInfo() {
  Serial.print("Current Position: ");
  printCoord(currRA, currDEC);
  Serial.print("Version: ");
  Serial.println(_ver);
  #ifdef ST4
  Serial.print("ST4");
  #endif
}


// Print nicely formatted coords
void printCoord(long raSecs, long decSecs) {
  long pp = raSecs/3600;
  Serial.print(pp);
  Serial.print("h");
  long mi = (raSecs-pp*3600)/60;
  if (mi<10) Serial.print('0');
  Serial.print(mi);
  Serial.print("'");
  long ss = (raSecs-mi*60-pp*3600);
  if (ss<10) Serial.print('0');
  Serial.print(ss);
  Serial.print("\" ");
  pp = abs(decSecs)/3600;
  Serial.print((decSecs>0?pp:-pp));
  Serial.print("°");
  mi = (abs(decSecs)-pp*3600)/60;
  if (mi<10) Serial.print('0');
  Serial.print(mi);
  Serial.print("'");
  ss = (abs(decSecs)-mi*60-pp*3600);
  if (ss<10) Serial.print('0');
  Serial.print(ss);
  Serial.println("\"");
}

/*
 * main loop
 */
 
void loop() {
  // Check if message on serial input
  // Please find the time in seconds here: https://www.utctime.net/utc-timestamp
  setTime(1662118596);
  if (Serial.available() > 0) {
    {
      input[in] = Serial.read(); 
  
      // discard blanks. Meade LX200 specs states :Sd and :Sr are
      // not followed by a blank but some implementation does include it.
      // also this allows aGoto commands to be typed with blanks
      if (input[in] == ' ') return; 
      
      // acknowledge LX200 ACK signal (char(6)) for software that tries to autodetect protocol (i.e. Stellarium Plus)
      if (input[in] == char(6)) { Serial.print("P"); return; } // P = Polar
  
      if (input[in] == '#' || input[in] == '\n') { // after a # or a \n it is time to check what is in the buffer
        if (input[0] == ':') { // it's lx200 protocol
          printLog(input);
          lx200(input);
        } else {
          // unknown command, print message only
          // if buffer contains more than one char
          // since stellarium seems to send extra #'s
          if (in > 0) {
            String s = input;
            Serial.print(s.substring(0,in));
            Serial.println(" unknown. Expected lx200 or aGotino commands");
          }
        }
        in = 0; // reset buffer // WARNING: the whole input buffer is passed anyway
      } else {
        if (in++>20) in = 0; // prepare for next char or reset buffer if max lenght reached
      } 
    }
  }
}
//Setup UTC time, considering it as UT time for alt-az calculation
void processSyncMessage() {
 // if time sync available from serial port, update time and return true
 while (Serial.available() >= TIME_MSG_LEN ) { // time message consists of header & 10 ASCII digits
 char c = Serial.read() ;
 Serial.print(c);
 if ( c == TIME_HEADER ) {
 time_t pctime = 0;
 for (int i = 0; i < TIME_MSG_LEN - 1; i++) {
 c = Serial.read();
 if ( c >= '0' && c <= '9') {
 pctime = (10 * pctime) + (c - '0') ; // convert digits to a number
 }
 }
 setTime(pctime); // Sync Arduino clock to the time received on the serial port
 }
 }
}
// Helpers to write on serial when DEBUG is active
void printLog(  String s)         { if (DEBUG) { Serial.print(":");Serial.println(s); } }
void printLogL( long l)           { if (DEBUG) { Serial.print(":");Serial.println(l); } }
void printLogUL(unsigned long ul) { if (DEBUG) { Serial.print(":");Serial.println(ul);} }
