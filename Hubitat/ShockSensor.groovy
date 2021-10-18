metadata {
    definition (name: "ShockSensor", namespace: "iharyadi", author: "iharyadi") {
        capability "ShockSensor"
        capability "Refresh"
        capability "Configuration"
        capability "Sensor"
    }
    
    preferences {
    
        section("Setup")
        {
            input name:"reversePin", type: "bool", title: "Reverse Pin", description: "Reverse pin High/Low translation to Active/Inactive",
                defaultValue: "false", displayDuringSetup: false 
            
            input name:"delay", type: "number", title: "Delay", description: "Delay event in seconds after inactivity",
                defaultValue: 0, displayDuringSetup: false 
        }
    }
}

#include iharyadi.gpiolib

def delayedInactiveEvent()
{
    sendEvent([name:"shock", value:"clear"])
}

def parse(def data) { 
        
    if(data[0].toInteger() == REQUEST_CONFIGURATION() )
    {
        initialize()
        return null
    }
    
    if(data[0].toInteger() != REPORT_PIN_CURRENT_VALUE())
    {
        return null   
    }
    
    Integer page = zigbee.convertHexToInt(data[1])
    
    if(getDevicePinNumber() != page)
    {
       return null   
    }
       
    short pinValue = (short) Long.parseLong(data[2], 16);
    
    def event = null
    
    if(pinValue == HIGH() ^ (boolean) reversePin)
    {
        unschedule(delayedInactiveEvent)
        event = createEvent(name:"shock", value:"detected")
    }
    else
    {
        if(delay && delay > 0)
        {
            runIn(delay, delayedInactiveEvent)
        }
        else
        {
            event = createEvent(name:"shock", value:"clear")
        }
    }
    
    return event;
}

def configure_child() {
}

def installed() {
    initialize()
}

def initialize()
{
    byte[] setPinMode  = [SET_PIN_MODE(),getDevicePinNumber(),INPUT()];
    byte[] getpinvalue = [GET_PIN_VALUE(),getDevicePinNumber(),0]
    byte[] setpindebounce = [SET_INPUT_PIN_DEBOUNCE(), getDevicePinNumber(),250]
    byte[] setpindebouncemode = [SET_INPUT_PIN_DEBOUNCE_MODE(), getDevicePinNumber(),DEBOUNCE_IGNORE_LEVEL()]
    def cmd = []
    cmd += parent.sendToSerialdevice(setpindebouncemode)    
    cmd += "delay 100"
    cmd += parent.sendToSerialdevice(setpindebounce)    
    cmd += "delay 100"
    cmd += parent.sendToSerialdevice(setPinMode)
    cmd += "delay 100"
    cmd += parent.sendToSerialdevice(getpinvalue)  
    cmd += "delay 100"
    parent.sendCommandP(cmd) 
}

def configure()
{
    initialize()
}

def uninstalled() {
    unschedule(delayedInactiveEvent)
    unconfiguredImp()
}

def refresh()
{
    refreshImp()
}

