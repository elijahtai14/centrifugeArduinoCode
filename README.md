# centrifugeArduinoCode
This project is the basic code to build and control a centrifuge with an Arduino.
It requires an LCD + potentiometer, three relays, four buttons, and a temperature sensor.

# Functionality
- The code supports a 9-state menu, displayed via an LCD, to navigate, control, and view the centrifuge inputs and running status
- Timer to support a timed running operation  
- An automatic PID and fan system that tries to keep the temperature sensor within a set range and target temperature
- An on/off button which keeps the Arduino running but disables the fan, heating pad, motor, and LCD.
- Settings are stored in EEPROM rather than SRAM so that when the system loses power, important values are stored. My code only updates the      EEPROM when the machine is turned off, otherwise the values are manipulated in SRAM, intended to minimize breakdown.
- Only uses a timer interrupt via timer1, which is on the Micro, Uno and Mega, so it may be compatible. The design uses 15 digital pins so it might work on the Micro.

Have fun, and Good Luck!
