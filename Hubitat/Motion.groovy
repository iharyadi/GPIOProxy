metadata {
    definition (name: "Motion", namespace: "iharyadi", author: "iharyadi") {
        capability "MotionSensor"
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

private short REPORT_PIN_CURRENT_VALUE()
{
    return 0x00
}

private short LOW()
{
    return 0;   
}

private short INPUT()
{
   return 0x00;   
}

private short UNCONFIGURED()
{
   return 0xFF;   
}

private short INPUT_PULLUP()
{
   return 0x02;   
}

private short GET_PIN_VALUE()
{
    return 0x02    
}

private short SET_PIN_MODE()
{
    return 0x03   
}

private short REQUEST_CONFIGURATION()
{
    return 0x06   
}

private short SET_INPUT_PIN_DEBOUNCE()
{
    return 0x04   
}

private short getDevicePinNumber()
{
    String devicePinNumber = device.getDataValue("pageNumber")
    return (short) devicePinNumber.toInteger();
}

def delayedInactiveEvent()
{
    sendEvent([name:"motion", value:"inactive"])
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
    
    if((pinValue != LOW()) ^ (boolean) reversePin)
    {
        unschedule(delayedInactiveEvent)
        event = createEvent(name:"motion", value:"active")
    }
    else
    {
        if(delay && delay > 0)
        {
            runIn(delay, delayedInactiveEvent)
        }
        else
        {
            event = createEvent(name:"motion", value:"inactive")
        }
    }
    
    return event;
}

def configure_child() {
}

def initialize() {
    byte[] setPinMode  = [SET_PIN_MODE(),getDevicePinNumber(),INPUT()];
    byte[] getpinvalue = [GET_PIN_VALUE(),getDevicePinNumber(),0];
    byte[] setpindebounce = [SET_INPUT_PIN_DEBOUNCE(), getDevicePinNumber(),250]
    def cmd = []
    cmd += parent.sendToSerialdevice(setPinMode)  
    cmd += "delay 50"
    cmd += parent.sendToSerialdevice(setpindebounce)  
    cmd += "delay 50"
    cmd += parent.sendToSerialdevice(getpinvalue)    
    cmd += "delay 2000"
    parent.sendCommandP(cmd) 
}

def installed() {
    initialize() 
}

def configure()
{
    initialize()   
}

def uninstalled() {
    unschedule(delayedInactiveEvent)
    byte[] setPinMode = [SET_PIN_MODE(),getDevicePinNumber(),UNCONFIGURED()];
    def cmd = []
    cmd += parent.sendToSerialdevice(setPinMode)    
    parent.sendCommandP(cmd) 
}

def refresh()
{
    byte[] getpinvalue = [GET_PIN_VALUE(),getDevicePinNumber(),0];
    def cmd = []
    cmd += parent.sendToSerialdevice(getpinvalue)  
    parent.sendCommandP(cmd) 
}
