metadata {
    definition (name: "Tone", namespace: "iharyadi", author: "iharyadi") {
        capability "Tone"
        capability "Sensor"
        capability "Configuration"
    }
}

#include iharyadi.gpiolib

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
    
    return null
}

def configure_child() {
}

def beep() {
    sendPulse(HIGH(),200)    
}

def installed() {
    initialize()
}

def configure(){
    initialize()
}

def initialize() {
    byte[] setPinMode = [SET_PIN_MODE(),getDevicePinNumber(),OUTPUT()];
    byte[] setPinValue = [SET_OUTPUT_PIN_VALUE(),getDevicePinNumber(),LOW()];
    def cmd = []
    cmd += parent.sendToSerialdevice(setPinMode)    
    cmd += "delay 100"
    cmd += parent.sendToSerialdevice(setPinValue) 
    cmd += "delay 100"
    parent.sendCommandP(cmd) 
}

def uninstalled() {
    unconfiguredImp()
}