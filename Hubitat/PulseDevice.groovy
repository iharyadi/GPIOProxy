metadata {
    definition (name: "PulseDevice", namespace: "iharyadi", author: "iharyadi") {
        capability "RelaySwitch"
        capability "Switch"
        capability "Sensor"
        capability "Refresh"
        capability "Configuration"
    }
    
    preferences {
        section("Setup")
        {
            input name:"pulseLevel", type: "bool", title: "Pulse Level High?", description: "Is it low to high pulse?",
                defaultValue: "true", displayDuringSetup: false 
            
            input name:"pulseWidth", type: "number", title: "Pulse Width", description: "Length of the pulse in milliseconds",
                defaultValue: 0, displayDuringSetup: false 
        }
    }
}

#include iharyadi.gpiolib

def parse(def data) { 
     
    Integer page = zigbee.convertHexToInt(data[1])
    
    if(getDevicePinNumber() != page)
    {
       return null   
    }
    
    if(data[0].toInteger() == REQUEST_CONFIGURATION() )
    {
        initialize(LOW())
        return null
    }
    
    if(data[0].toInteger() != REPORT_PIN_CURRENT_VALUE())
    {
        return null   
    }
    
    short pinValue = (short) Long.parseLong(data[2], 16);
     
    return createEvent(name:"switch", value:(pinValue == LOW())?"off":"on")
}

def configure_child() {
}

private def setPin(short value)
{
    byte[] setPinValue = [SET_OUTPUT_PIN_VALUE(),getDevicePinNumber(),value];
    def cmd = [] 
    cmd += parent.sendToSerialdevice(setPinValue)
    cmd += "delay 2000"
    parent.sendCommandP(cmd) 
}

def off() {
    setPin(LOW())
}

def on() {
    if(pulseWidth == null || pulseWidth == 0)
    {
        return    
    }
    
    sendPulse((pulseLevel == null || pulseLevel) ? HIGH(): LOW(),(short) pulseWidth)
}

def initialize(short val)
{
    byte[] setPinMode = [SET_PIN_MODE(),getDevicePinNumber(),OUTPUT()];
    byte[] setPinValue = [SET_OUTPUT_PIN_VALUE(),getDevicePinNumber(),val];
    byte[] getPinValue = [GET_PIN_VALUE(),getDevicePinNumber(),0];
    def cmd = []
    cmd += parent.sendToSerialdevice(setPinMode)    
    cmd += "delay 100"
    cmd += parent.sendToSerialdevice(setPinValue) 
    cmd += "delay 100"
    cmd += parent.sendToSerialdevice(getPinValue) 
    cmd += "delay 100"
    parent.sendCommandP(cmd)  
}
       
def installed() {
    initialize(LOW())
}

def configure()
{
    initialize(LOW())
}

def uninstalled() {
    unconfiguredImp()
}

def refresh()
{
    refreshImp()
}