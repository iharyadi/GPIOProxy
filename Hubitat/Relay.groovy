metadata {
    definition (name: "Relay", namespace: "iharyadi", author: "iharyadi") {
        capability "RelaySwitch"
        capability "Sensor"
        capability "Refresh"
        capability "Configuration"
    }  
    
    section("Setup")
    {
        input name:"reversePin", type: "bool", title: "Reverse Pin?", description: "Reverse pin High/Low translation to On/Off",
            defaultValue: "false", displayDuringSetup: false 
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
        initialize(state.last_request!= null?(short)state.last_request:((boolean) reversePin ? HIGH() : LOW()))
        return null
    }
    
    if(data[0].toInteger() != REPORT_PIN_CURRENT_VALUE())
    {
        return null   
    }
    
    short pinValue = (short) Long.parseLong(data[2], 16);
    
    state.last_request = pinValue
     
    return createEvent(name:"switch", value:(pinValue == HIGH() ^ (boolean) reversePin)?"on":"off")
}

def configure_child() {
}

private def setPin(short value)
{
    byte[] setPinValue = [SET_OUTPUT_PIN_VALUE(),getDevicePinNumber(),value];
    def cmd = [] 
    cmd += parent.sendToSerialdevice(setPinValue)
    cmd += "delay 100"
    parent.sendCommandP(cmd) 
}

def off() {
    setPin((boolean) reversePin ? HIGH() : LOW())
}

def on() {
     setPin((boolean) reversePin ? LOW() : HIGH())
}

def updated()
{
    runIn(2,refresh);
}

def initialize(short val)
{
    state.last_request = val
    
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
    initialize((boolean) reversePin ? HIGH() : LOW())
}

def configure()
{
    initialize((boolean) reversePin ? HIGH() : LOW())
}

def uninstalled() {
    unschedule()
    unconfiguredImp()
}

def refresh()
{
    refreshImp()
}