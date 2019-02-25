# ArduinoChickenDoor
This is the code for an arduino to control the opening and closing of a chicken door at sunrise and sunset, respectively. Hint: we don't need no stinking photosensor.

Hardware Configuration:
Arduino Uno
RTC3231 Real Time Clock with Battery Backup from DIYMORE 
HiLetgo DC 12V 2 Channel Relay Module pn 3-01-0342
Add-A-Motor - Chicken Coop Automatic Door Motor - Model D20

After uploading the code to your arduino you must send an 'r' over the terminal to initialize the RTC to the _DATE_ _TIME_ of your compile. So send the 'r' right away or your RTC will be off. The code awkwardly converts the time to UTC and uses that time to compute the time of sunrise and sunset. Different locals use DST and some don't. Knowing if you are in a DST observent local is not my thing so you'll need to adjust the code accordingly. It's easy. If you're in a local that does not observe DST then make checkIfDST() always return false and you're done.

Terminal commands:
't' reports the current time, what time the door is scheduled to close, and what time the door is scheduled to open.

'b' sumulates a button press to manually open/close the door.

'r' initialize the RTC to the _DATE_ _TIME_ of your compile.

's' report the current state of the door: "closed", "open", "closing", "opening".

Fault Tolerance
There is some basic fault tolerance built in. When the door changes state we write that state to the available memory on the RTC chip. This only works as long as the battery backup is working on the RTC.
