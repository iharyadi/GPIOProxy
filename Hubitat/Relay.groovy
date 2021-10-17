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

private short LOW()
{
    return 0x00;   
}

private short HIGH()
{
    return 0x01;   
}

private short SET_OUTPUT_PIN_VALUE()
{
    return 0x01    
}

private short SET_PIN_MODE()
{
    return 0x03   
}

private short GET_PIN_VALUE()
{
    return 0x02    
}

private short REPORT_PIN_CURRENT_VALUE()
{
    return 0x00
}

private short REQUEST_CONFIGURATION()
{
    return 0x06
}

private short SET_DELAY_OUTPUT_COMMAND()
{
    return 0x07
}

private short OUTPUT()
{
   return 0x01;   
}

private short INPUT_PULLUP()
{
   return 0x02;   
}

private short UNCONFIGURED()
{
   return 0xFF;   
}

private short getDevicePinNumber()
{
    String devicePinNumber = device.getDataValue("pageNumber")
    return (short) devicePinNumber.toInteger();
}

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
    cmd += "delay 2000"
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