 /*
    LiquidCrystal
    works with all LCD displays that are compatible with the
    Hitachi HD44780 driver.
    https://www.arduino.cc/en/Reference/LiquidCrystal
*/
    
/*
    EEPROM
    1024 bytes on the ATmega328P, 
    512 bytes on the ATmega168 and ATmega8,
    4 KB (4096 bytes) on the ATmega1280 and ATmega2560. 
    The Arduino and Genuino 101 boards have an emulated EEPROM space of 1024 bytes.
    More than enough, we're just storing three values: 
      1) The target temperature (tempcutoff) 
      2) The rotations per minute (rpm)
      3) The running time after hitting start (runtime)
    https://www.arduino.cc/en/Reference/EEPROM
 */
// include the library code:
#include <LiquidCrystal.h>
#include <EEPROM.h>

// What screen is displaying, see FSM.jpg to see what state correstponds to what functionalities.
int state = 0;

// Countdown timer
int countdown;

// ===CONSTANTS=== (for customization)
// Default variables (sets temperature to 38, rpm to 15, runtime to 5 minutes)
const int settemp = 38, rotpm = 15, runt = 300;

// Variable limits
// 26-50 degrees celcius : 1,000-3,000 rpm : 0 - 10 mins (600 secs)
const int tempmin = 26, tempmax = 50, rpmmin = 1, rpmmax = 30, runmin = 0, runmax = 600;

// If the temp is below this range, heat. If above, fan.
const int temprange = 1;

// ===PINS===
// These pins control the LCD
const int rspin = 34, enpin = 32, bklpin = 30, d4pin = 37, d5pin = 35, d6pin = 33, d7pin = 31;

// ---RELAY PINS---
// This pin is the relay pin which connects to a fan or a motor
const int fanpin = 38;
// I assume there is some motor driver -- would need to look at the specs for that to really implement it.
// This pin controls the centrifuge motor
const int motorpin = 39;
// This pin controls the heating pad
const int heatpin = 41;

// These are all the input buttons
const int onpin = 42, backpin = 46, uppin = 50, downpin = 48;

// A light to signal the device is on
const int onledpin = 44;

// EEPROM Storage address
const int eeAddress = 0;

// Temperature sensor analog pin 
#define temppin A0
#define tempbeta 4090 // Beta of the thermistor
#define tempres 10 // Pulldown resistor value, 10k

// Initialize LCD
LiquidCrystal lcd(rspin, enpin, d4pin, d5pin, d6pin, d7pin);

// Stores whether the fan should be on
bool fanon = true;
// Stores whether the motor should be on
bool motoron = false;
// Stores whether the heat should be on
bool heaton = false;

// Stores button values to look for a change (a positive-edge detector).
int onstate = 0;
int lastonstate = 0;
bool on = 0;
int backstate = 0;
int lastbackstate = 0;
int upstate = 0;
int lastupstate = 0;
int downstate = 0;
int lastdownstate = 0;

// Initialize variables to default value
int tempcutoff = settemp;
int rpm = rotpm;
int runtime = runt;


// ===STORED & RETRIEVE VALUES===:
// Obviously, we want to put values in EEPROM and not SRAM

// Put stuff in memory, in the order: tempcutoff, rpm, runtime
void putmem(int t=settemp, int r=rotpm, int rt=runt);
void getmem();
// Another helper function to convert integers to a time format
String converttime(int sec);

void setup()
{
  // Just for debugging - Logs behavior.
  Serial.begin(9600);
  while (!Serial)
  {
   ; // Wait for serial connection to finish
  }

  // Initialize Buttons
  pinMode(onpin, INPUT);
  pinMode(backpin, INPUT);
  pinMode(uppin, INPUT);
  pinMode(downpin, INPUT);
  pinMode(onledpin, OUTPUT);
  
  // We control the LCD backlight digitally. <-Should I swap this to analog for brightness control?
  pinMode(bklpin, OUTPUT);
  
  // Set up the LCD's columns and rows:
  lcd.begin(16, 2);
  lcd.clear();
  
  // Relay initialization.
  pinMode(fanpin, OUTPUT);
  pinMode(motorpin, OUTPUT);
  pinMode(heatpin, OUTPUT);
  // (Set the fan to high to begin with, and the motor and heat to low)
  digitalWrite(fanpin, HIGH);
  digitalWrite(motorpin, LOW);
  digitalWrite(heatpin, LOW);
  fanon = true;
  motoron = false;
  heaton = false;
  
  // "Loading" screen that prints "LW Scientific"
  digitalWrite(bklpin, HIGH);
  lcd.setCursor(0,0); 
  lcd.print("LW ");
  lcd.setCursor(0,1); 
  lcd.print("Scientific"); 
  delay(2000); 
  lcd.clear();

  // We are at the main screen now
  state = 1;

  // 1Hz clock initialization via Arduino's built in timer1
  // Includes calculations to get exactly 1Hz frequency
  cli();//stop interrupts
  // Set timer1 interrupt at 1Hz. timer1 is also in the ArduinoMicro, so this will work.
  TCCR1A = 0;// set entire TCCR1A register to 0
  TCCR1B = 0;// same for TCCR1B
  TCNT1  = 0;//initialize counter value to 0
  // Set compare match register for 1hz increments
  OCR1A = 15624;// = (16*10^6) / (1*1024) - 1 (must be <65536)
  // Turn on CTC mode
  TCCR1B |= (1 << WGM12);
  // Set CS12 and CS10 bits for 1024 prescaler
  TCCR1B |= (1 << CS12) | (1 << CS10);  
  // Enable timer compare interrupt
  TIMSK1 |= (1 << OCIE1A);
  // Allow interrupts
  sei();
}

// ===THIS FUNCTION RUNS ONCE PER SECOND===
// Timer1 interrupt 1Hz function
ISR(TIMER1_COMPA_vect){
  Serial.println("tick");
  // If the centrifuge is "RUN" state then count down
  if (state == 7 || state == 8)
  {
    countdown = countdown - 1;
  }
}

void loop() 
{
  // Read all the buttons values
  onstate = digitalRead(onpin);
  backstate = digitalRead(backpin);
  upstate = digitalRead(uppin);
  downstate = digitalRead(downpin);

  // ===If DEVICE IS ON===:  
  if (on){
    // Turn the green LED on
    digitalWrite(onledpin, HIGH);
    // Set the backlight on
    digitalWrite(bklpin, HIGH);

    // Calculate temperature
    long val = 1023 - analogRead(temppin);
    float temp = tempbeta /(log((1025.0 * 10 / val - 10) / 10) + tempbeta / 298.0) - 273.0;
     
    /*
    Run fan if it exceeds the cuttoff irregardless of state. 
    Hopefully it will only kick in if the heating pad is on. 
    If the fan should be on and it is not, then turn it on.
    If the fan should not be on and it is, then turn it off.
    This way we only need to write to the fan pin when necessary and not every iteration.
    */
    if (temp > tempcutoff + temprange)
    {
      if (!fanon)
      {
        digitalWrite(fanpin, HIGH);
        fanon = 1;
      }
    } 
    else 
    {
      if (fanon)
      {
        digitalWrite(fanpin, LOW);
        fanon = 0;
      }
    }

    // Same idea for the motor, except only run when in a "RUN" state
    if (state == 7 ||  state == 8)
    {
      if (!motoron)
      {
        digitalWrite(motorpin, HIGH);
        motoron = 1;
      }
    } 
    else 
    {
      if (motoron)
      {
        digitalWrite(motorpin, LOW);
        motoron = 0;
      }
    }

    // Same idea for the heating pad, except only run when below the desired temperature and in "RUN" state
    if ((temp < tempcutoff - temprange) && (state == 7 || state == 8))
    {
      if (!heaton)
      {
        digitalWrite(heatpin, HIGH);
        heaton = 1;
      }
    } 
    else 
    {
      if (heaton)
      {
        digitalWrite(heatpin, LOW);
        heaton = 0;
      }
    }

    

    // ---BUTTON CHANGES---(state logic, see FSM.jpg to see how the transitions work):
    // If it was the back button
    if (backstate && !lastbackstate)
    {
      lcd.clear();
      Serial.println("Back: from ");
      Serial.print(state);
      if (state == 2) {state = 1;}
      else if (state == 3 || state == 6) {state = 2;}
      else if (state == 4 || state == 5) {state = 3;}  
      else if (state == 7 || state == 8) {state = 1;}  
      Serial.print(" to ");
      Serial.print(state); 
    }

    // If it was the up button, clear the screen, 
    if (upstate && !lastupstate)
    {
      lcd.clear();
      Serial.println("Up: from ");
      Serial.print(state);
      if (state == 1) {state = 2;}
      else if (state == 2) {state = 6;}
      else if (state == 3) {state = 4;}  
      // Note that states 4, 5, and 6 allows the user to alter the runtime, rpm, and tempterature values
      //SET TIME UP (or at maximum value)
      else if (state == 4) 
      {
        if (runtime <= runmax - 30) 
        {
          runtime = runtime + 30;
        }
        else
        {
          runtime = runmax;
        }
      } 
      else if (state == 5) 
      //SET RPM UP (or at maximum value)
      {
        if (rpm <= rpmmax - 1) 
        {
          rpm = rpm + 1;
        }
        else
        {
          rpm = rpmmax;
        }
      }
      else if (state == 6) 
      //SET TEMP UP (or at maximum value)
      {
        if (tempcutoff <= tempmax - 1) 
        {
          tempcutoff = tempcutoff + 1;
        }
        else
        {
          tempcutoff = tempmax;
        }
      }     
      else if (state == 7) {state = 8;}    
      else if (state == 8) {state = 7;} 
      Serial.print(" to ");
      Serial.print(state); 
    }

    // If it was the down button
    if (downstate && !lastdownstate)
    {
      lcd.clear();
      Serial.println("Down: from ");
      Serial.print(state); 
      if (state == 1) {
        //Start the countdown at the set value
        countdown = runtime;
        state = 8;
      }
      else if (state == 2) {state = 3;}
      else if (state == 3) {state = 5;}  
      //SET TIME DOWN (or at minimum value)
      else if (state == 4) 
      {
        if (runtime >= runmin + 30) 
        {
          runtime = runtime - 30;
        }
        else
        {
          runtime = runmin;
        }
      } 
      else if (state == 5) 
      //SET RPM DOWN (or at minimum value)
      {
        if (rpm >= rpmmin + 1) 
        {
          rpm = rpm - 1;
        }
        else
        {
          rpm = rpmmin;
        }
      }
      else if (state == 6) 
      //SET TEMP DOWN (or at minimum value)
      {
        if (tempcutoff >= tempmin + 1) 
        {
          tempcutoff = tempcutoff - 1;
        }
        else
        {
          tempcutoff = tempmin;
        }
      }   
      else if (state == 7) {state = 8;}    
      else if (state == 8) {state = 7;} 
      Serial.print(" to "); 
      Serial.print(state); 
    }


    // ===THIS IS ALL THE DISPLAY STUFF BASED ON THE CURRENT STATE===
    switch (state) {
      case 1:
        lcd.setCursor(0,0);
        lcd.print("Program");
        lcd.setCursor(0,1);
        lcd.print("Run"); 
      break;
      
      case 2:
        lcd.setCursor(0,0);
        lcd.print("Set Temp");
        lcd.setCursor(0,1);
        lcd.print("Set Time/RPM"); 
      break;

      case 3:
        lcd.setCursor(0,0);
        lcd.print("Set Time");
        lcd.setCursor(0,1);
        lcd.print("Set RPM"); 
      break;

      case 4:
        lcd.setCursor(0,0);
        lcd.print("Time");
        lcd.setCursor(0,1);
        lcd.print(converttime(runtime)); 
      break;

      case 5:
        lcd.setCursor(0,0);
        lcd.print("RPM");
        lcd.setCursor(0,1);
        lcd.print(rpm); 
        lcd.print(",000");
      break;

      case 6:
        lcd.setCursor(0,0);
        lcd.print("Temp C.");
        lcd.setCursor(0,1);
        lcd.print(tempcutoff); 
      break;

      case 7:
        lcd.setCursor(0,0);
        lcd.print("Cur Temp: ");
        lcd.print(temp);
        lcd.print("C");
        
        lcd.setCursor(0,1);
        lcd.print("Set Temp: ");
        lcd.print(tempcutoff);
        lcd.print("C");

        // Check the timer here
        if (countdown < 0)
        {
          countdown = runtime;
          state = 1;
          lcd.clear();
        }
      break;

      case 8:
        lcd.setCursor(0,0);
        lcd.print("RPM: ");
        lcd.print(rpm); 
        lcd.print(",000");

        lcd.setCursor(0,1);
        lcd.print("Time Left: ");
        // It is runtime for now, will implement a clock later.
        lcd.print(converttime(countdown)); 

        // Check the timer here
        if (countdown < 0)
        {
          countdown = runtime;
          state = 1;
          lcd.clear();
        }
      break;
      
      
      default:
        // If we ever reach an invalid state, go back home just in case.
        state = 1;
    }
  }

  // ===OFF STATE===
  else 
  {
  // Do nothing
  }

  // ---ON/OFF--- detector:
  if (onstate && !lastonstate)
  {
    // Turning from on to off:
    if (on)
    {
      // Clear the screen, turn off the light, turn off the backlight, turn off the relay, turn off the screen.
      lcd.clear();
      digitalWrite(onledpin, LOW);
      digitalWrite(bklpin, LOW);
      digitalWrite(fanpin, LOW);
      digitalWrite(motorpin, LOW);
      digitalWrite(heatpin, LOW);
      lcd.noDisplay();
      Serial.println("Powering Off");
      Serial.println(tempcutoff);
      // Put the info set so far in memory
      putmem(tempcutoff, rpm, runtime);
    }
    // When turning from off to on
    if (!on)
    {
      Serial.println("Powering On");
      // Reinitialize everything in setup()!
      setup();
      // Retrieve the info set so far from memory
      getmem();
    }
    // Then negate the on variable (from on to off or from off to on)
    on = !on;
    Serial.println("On/Off");
  }

  // Update all the states of the buttons for the next frame. past values <- current values for next cycle edge-detection.
  lastonstate = onstate;
  lastbackstate = backstate;
  lastupstate = upstate;
  lastdownstate = downstate;
}

// ===HELPER FUNCTIONS ===
// Stores temperature, rotations, and runtime in memory
void putmem(int t, int r, int rt)
{
  Serial.println("Putting stuff in Memory...");
  // Put them in memory
  int v[] = {t, r, rt};
  EEPROM.put(eeAddress, v);
}

// Gets stuff from memory
void getmem()
{
  Serial.println("Getting stuff from Memory...");
  
  int vars[] = {settemp, rotpm, runt};
  EEPROM.get(eeAddress, vars);
  
  // If the values are bad, initialize to default values
  if (vars[0] < 0 || vars[1] < 0 || vars[2] < 0)
  {
    tempcutoff = settemp;
    rpm = rotpm;
    runtime = runt;
  }
  else
  {
    // Otherwise just read in the values
    tempcutoff = vars[0];
    rpm = vars[1];
    runtime = vars[2];
  }

  //Checking if retrieval worked.
  Serial.print("Retrieved Temp ");
  Serial.println(vars[0]);
  Serial.print("Retrieved RPM ");
  Serial.println(vars[1]);
  Serial.print("Retrieved Runtime ");
  Serial.println(vars[2]);

  // Update the countdown
  countdown = runtime % 30;
}

// Helper function that converts a number of seconds to a minute-time format (m)m:ss
String converttime(int sec)
{
  int mins = sec/60;
  int secs = sec%60;
  // Add a zero so the seconds look good if secs is one digit
  if (secs < 10)
  {
    return String(String(mins) + ":0" + String(secs));
  }
  else
  {
    return String(String(mins) + ":" + String(secs));
  }
}
