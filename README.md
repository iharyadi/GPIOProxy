# mega
 It is an example sketch used with Arduino Mega 2560.  This skecth takes command to configure, read and write Arduino GPIO pin. 
 
 The idea is to make GPIO proxy.   The following description is in the context of its use in Hubitat/Smartthings ecosystem.
 
 The Arduino sketch take commands from serial port.  As example of a command is to set pin mode on the Arduino.  Another one is to set output level.  This is basically  of serialize and deserialize arduino GPIO api that deal specifically with input an output.
 
 On the serialized commands is send back and fort through arduino to a Hubitat/ Smartthings hub.
 
 Arduino <======> Environment Arduino Shield <==Zigbee==> Hub (Hubitat/Smartthings) <=========> DTH
                                                                                          ||==> DTH
                                                                                          
 The sketch does not persist the pin configuration.  I try to offload the configuration of a pin to the DTH side.  In the DTH code,  you should see codes that set the GPIO mode and etc during installation.
 
 One may asked,  what happend if the arduino loose power.   In this case,  when the arduino started,  It will send a command to the hub requesting its configuration.  The hub will response by refreshing the configuration.
 
 
