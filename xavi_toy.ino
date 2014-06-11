#include <FatReader.h>
#include <SdReader.h>
#include <avr/pgmspace.h>
#include "WaveUtil.h"
#include "WaveHC.h"

SdReader card;    // This object holds the information for the card
FatVolume vol;    // This holds the information for the partition on the card
FatReader root;   // This holds the information for the filesystem on the card
FatReader f;      // This holds the information for the file we're play

WaveHC wave;      // This is the only wave (audio) object, since we will only play one at a time

#define DEBOUNCE 5  // button debouncer

// here is where we define the buttons that we'll use. button "1" is the first, button "6" is the 6th, etc
byte buttons[] = {A0, A1, A2};
// This handy macro lets us determine how big the array up above is, by checking the size
#define NUMBUTTONS sizeof(buttons)
// we will track if a button is just pressed, just released, or 'pressed' (the current state
volatile byte pressed[NUMBUTTONS], justpressed[NUMBUTTONS], justreleased[NUMBUTTONS];


// ethan variables
int led1 = 7;
int led2 = 6;
int led3 = 8;
String currWav = "";

int inPin = A3;         // the number of the input pin (toggle switch)

int toggleState = HIGH;      // the current state of the output pin
int reading;           // the current reading from the input pin
int previous = LOW;    // the previous reading from the input pin

// the follow variables are long's because the time, measured in miliseconds,
// will quickly become a bigger number than can be stored in an int.
long time = 0;         // the last time the output pin was toggled
long debounce = 200;   // the debounce time, increase if the output flickers




// this handy function will return the number of bytes currently free in RAM, great for debugging!   
int freeRam(void)
{
  extern int  __bss_end; 
  extern int  *__brkval; 
  int free_memory; 
  if((int)__brkval == 0) {
    free_memory = ((int)&free_memory) - ((int)&__bss_end); 
  }
  else {
    free_memory = ((int)&free_memory) - ((int)__brkval); 
  }
  return free_memory; 
} 

void sdErrorCheck(void)
{
  if (!card.errorCode()) return;
  putstring("\n\rSD I/O error: ");
  Serial.print(card.errorCode(), HEX);
  putstring(", ");
  Serial.println(card.errorData(), HEX);
  while(1);
}

void setup() {
  Serial.begin(9600);
  Serial.print("print me out");
  byte i;
  
  // set up serial port
  Serial.begin(9600);
  putstring_nl("WaveHC with ");
  Serial.print(NUMBUTTONS, DEC);
  putstring_nl("buttons");
  
  putstring("Free RAM: ");       // This can help with debugging, running out of RAM is bad
  Serial.println(freeRam());      // if this is under 150 bytes it may spell trouble!
  
  // Set the output pins for the DAC control. This pins are defined in the library
  pinMode(2, OUTPUT);
  pinMode(3, OUTPUT);
  pinMode(4, OUTPUT);
  pinMode(5, OUTPUT);
 
  // LEDs
  pinMode(led1, OUTPUT);
  pinMode(led2, OUTPUT);
  pinMode(led3, OUTPUT);

  // Toggle switch
  // pinMode(inPin, INPUT);
 
  // Make input & enable pull-up resistors on switch pins
  for (i=0; i< NUMBUTTONS; i++) {
    pinMode(buttons[i], INPUT);
    digitalWrite(buttons[i], HIGH);
  }
  
  //  if (!card.init(true)) { //play with 4 MHz spi if 8MHz isn't working for you
  if (!card.init()) {         //play with 8 MHz spi (default faster!)  
    putstring_nl("Card init. failed!");  // Something went wrong, lets print out why
    sdErrorCheck();
    while(1);                            // then 'halt' - do nothing!
  }
  
  // enable optimize read - some cards may timeout. Disable if you're having problems
  card.partialBlockRead(true);
 
// Now we will look for a FAT partition!
  uint8_t part;
  for (part = 0; part < 5; part++) {     // we have up to 5 slots to look in
    if (vol.init(card, part)) 
      break;                             // we found one, lets bail
  }
  if (part == 5) {                       // if we ended up not finding one  :(
    putstring_nl("No valid FAT partition!");
    sdErrorCheck();      // Something went wrong, lets print out why
    while(1);                            // then 'halt' - do nothing!
  }
  
  // Lets tell the user about what we found
  putstring("Using partition ");
  Serial.print(part, DEC);
  putstring(", type is FAT");
  Serial.println(vol.fatType(),DEC);     // FAT16 or FAT32?
  
  // Try to open the root directory
  if (!root.openRoot(vol)) {
    putstring_nl("Can't open root dir!"); // Something went wrong,
    while(1);                             // then 'halt' - do nothing!
  }
  
  // Whew! We got past the tough parts.
  putstring_nl("Ready!");
  
  TCCR2A = 0;
  TCCR2B = 1<<CS22 | 1<<CS21 | 1<<CS20;

  //Timer2 Overflow Interrupt Enable
  TIMSK2 |= 1<<TOIE2;


}

SIGNAL(TIMER2_OVF_vect) {
  check_switches();
}

void check_switches() {
  static byte previousstate[NUMBUTTONS];
  static byte currentstate[NUMBUTTONS];
  byte index;

  for (index = 0; index < NUMBUTTONS; index++) {
    currentstate[index] = digitalRead(buttons[index]);   // read the button
    

    // Serial.print(index, DEC);
    // Serial.print(": cstate=");
    // Serial.print(currentstate[index], DEC);
    // Serial.print(", pstate=");
    // Serial.print(previousstate[index], DEC);
    // Serial.print(", press=");

    
    if (currentstate[index] == previousstate[index]) {
      if ((pressed[index] == LOW) && (currentstate[index] == LOW)) {
          // just pressed
          justpressed[index] = 1;
      }
      else if ((pressed[index] == HIGH) && (currentstate[index] == HIGH)) {
          // just released
          justreleased[index] = 1;
      }
      pressed[index] = !currentstate[index];  // remember, digital HIGH means NOT pressed
    }
    //Serial.println(pressed[index], DEC);
    previousstate[index] = currentstate[index];   // keep a running tally of the buttons
  }
}


void ledOn(int led) {
  digitalWrite(led, HIGH);   // turn the LED on (HIGH is the voltage level)
  // delay(100);               // wait for a second
  // delay(1000);  
}

void ledOff(int led) {
  digitalWrite(led, LOW);    // turn the LED off by making the voltage LOW
}

// if no wav file playing, change currWave to empty string. 
void checkIfPlaying() {
  if (!wave.isplaying) {
    currWav = "";
  }
}


void checkToggle() {
  reading = digitalRead(inPin);

  // Serial.println(reading);
  // if (reading == HIGH) {
  //   Serial.println("HIGH");
  // } else {
  //   Serial.println("LOW");
  // }

  // if the input just went from LOW and HIGH and we've waited long enough
  // to ignore any noise on the circuit, toggle the output pin and remember
  // the time
  if (reading == HIGH && previous == LOW && millis() - time > debounce) {
    Serial.println("ick");
    if (toggleState == HIGH)
      toggleState = LOW;
    else
      toggleState = HIGH;
    time = millis();    
  }

 if (reading == LOW && previous == HIGH && millis() - time > debounce) {
   Serial.println("blah");
   if (toggleState == LOW)
     toggleState = HIGH;
   else
     toggleState = LOW;
   time = millis();    
 } 

  previous = reading;  
}


void loop() {
  byte i;

  checkIfPlaying();
  checkToggle();

  if (pressed[0] == 0) {
    Serial.println("button 0");
    ledOn(led1);

    if (toggleState == HIGH)
      playfile("XAVI.WAV");
    else {
      playfile("ISLAND.WAV");
    }


    // justpressed[0] = 0;
    // while (wave.isplaying && pressed[0]) {
    //   // Serial.print("1");
    // }
    ledOff(led1);
  }

  if (pressed[1] == 0) {
   Serial.println("button 1");
   ledOn(led2);
   // wave.stop();
   // justpressed[1] = 0;
   

   if (toggleState == HIGH)
     playfile("LASER.WAV");
   else {
     playfile("BLUES.WAV");
   }
   // while (wave.isplaying && pressed[1]) {
   //   // Serial.print("2");
   // }
   ledOff(led2);
  }

  if (pressed[2] == 0) {
    Serial.println("button 2");
    ledOn(led3);
    // wave.stop();
    // justpressed[1] = 0;
    if (toggleState == HIGH)
      playfile("SUSP.WAV");
    else {
      playfile("PARTY.WAV");
    }

    // while (wave.isplaying && pressed[2]) {
    //   // Serial.print("2");
    // }
    ledOff(led3);
  }




}



// Plays a full file from beginning to end with no pause.
void playcomplete(char *name) {
  // call our helper to find and play this name
  playfile(name);
  while (wave.isplaying) {
  // do nothing while its playing
  }
  // now its done playing
}


void playfile(char *name) {
  String testWav = name;
  if (testWav == currWav) {
    // Serial.println("same");
  } else {
    // Serial.println("not same");

    currWav = name;

    // see if the wave object is currently doing something
    if (wave.isplaying) { // already playing something, so stop it!
      wave.stop(); // stop it
    }
    // look in the root directory and open the file
    if (!f.open(root, name)) {
      putstring("Couldn't open file "); Serial.print(name); return;
    }
    // OK read the file and turn it into a wave object
    if (!wave.create(f)) {
      putstring_nl("Not a valid WAV"); return;
    }
    
    // ok time to play! start playback
    wave.play(); 


  }

}










