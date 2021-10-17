metadata {
    definition (name: "Contact", namespace: "iharyadi", author: "iharyadi") {
        
        singleThreaded: true
        
        capability "ContactSensor"
        capability "Refresh"
        capability "Configuration"
        capability "Sensor"
    }
    
    section("Setup")
    {
        input name:"reversePin", type: "bool", title: "Reverse Pin?", description: "Reverse pin High/Low translation to Active/Inactive",
            defaultValue: "false", displayDuringSetup: false 
            
        input name:"useInternalPullup", type: "bool", title: "Internal Pullup?", description: "Do you want to use internal pull up?",
            defaultValue: "false", displayDuringSetup: false 
        
        input name:"debouncePeriod", type: "number", title: "Debounce Period", description: "Set Debounce Period",
            defaultValue: "250", displayDuringSetup: false 
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

private short INPUT_PULLUP()
{
   return 0x02;   
}

private short UNCONFIGURED()
{
   return 0xFF;   
}

private short SET_PIN_MODE()
{
    return 0x03   
}

private short SET_INPUT_PIN_DEBOUNCE()
{
    return 0x04   
}

private short GET_PIN_VALUE()
{
    return 0x02    
}

private short REQUEST_CONFIGURATION()
{
    return 0x06;
}

private short getDevicePinNumber()
{
    String devicePinNumber = device.getDataValue("pageNumber")
    return (short) devicePinNumber.toInteger();
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
    
    return createEvent(name:"contact", value:(pinValue != LOW() ^ (boolean) reversePin )?"open":"closed")
}

def configure_child() {
}

def initialize() {
    byte[] setPinMode  = [SET_PIN_MODE(),getDevicePinNumber(), (boolean) useInternalPullup ? INPUT_PULLUP() : INPUT()];
    byte[] getpinvalue = [GET_PIN_VALUE(),getDevicePinNumber(),0];
    byte[] setpindebounce = [SET_INPUT_PIN_DEBOUNCE(), getDevicePinNumber(), debouncePeriod ? (short)debouncePeriod.toInteger():250]
    def cmd = []
    cmd += parent.sendToSerialdevice(setPinMode)  
    cmd += "delay 50"
    cmd += parent.sendToSerialdevice(setpindebounce)  
    cmd += "delay 50"
    cmd += parent.sendToSerialdevice(getpinvalue)    
    cmd += "delay 2000"
    parent.sendCommandP(cmd) 
}

void installed() {
    initialize() 
}

def configure()
{
    initialize();   
}

void updated()
{
    initialize();   
}

def uninstalled() {
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