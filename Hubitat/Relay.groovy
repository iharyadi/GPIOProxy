metadata {
    definition (name: "Relay", namespace: "iharyadi", author: "iharyadi") {
        capability "RelaySwitch"
        capability "Sensor"
        capability "Refresh"
        capability "Configuration"
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

private short OUTPUT()
{
   return 0x01;   
}

private short INPUT_PULLUP()
{
   return 0x02;   
}

private short getDevicePinNumber()
{
    String devicePinNumber = device.getDataValue("pageNumber")
    return (short) devicePinNumber.toInteger();
}

def parse(def data) { 
    
    log.info "data $data"
     
    Integer page = zigbee.convertHexToInt(data[1])
    
    if(getDevicePinNumber() != page)
    {
       return null   
    }
    
    if(data[0].toInteger() == REQUEST_CONFIGURATION() )
    {
        initialize(state.last_request == "OFF" ? HIGH() : LOW())
        return null
    }
    
    if(data[0].toInteger() != REPORT_PIN_CURRENT_VALUE())
    {
        return null   
    }
    
    short pinValue = (short) Long.parseLong(data[2], 16);
    
    if(state.last_request)
    {
        if(state.last_request == "OFF" && pinValue == LOW())
        {
            off()
            return null
       }
       else if (state.last_request == "ON" && pinValue == HIGH())
       {
           on()
           return null
       }   
    }
    
    return createEvent(name:"switch", value:(pinValue == LOW())?"on":"off")
}

def configure_child() {
}

private def setPin(short value)
{
    byte[] setPinValue = [SET_OUTPUT_PIN_VALUE(),getDevicePinNumber(),value];
    def cmd = [] 
    cmd += parent.sendToSerialdevice(setPinValue)
    cmd += "delay 50"
    parent.sendCommandP(cmd) 
}

def off() {
    state.last_request = "OFF"
    setPin(HIGH())
    
}

def on() {
    state.last_request = "ON"
    setPin(LOW())
}

def initialize(short val)
{
    byte[] setPinMode = [SET_PIN_MODE(),getDevicePinNumber(),OUTPUT()];
    byte[] setPinValue = [SET_OUTPUT_PIN_VALUE(),getDevicePinNumber(),val];
    byte[] getPinValue = [GET_PIN_VALUE(),getDevicePinNumber(),0];
    def cmd = []
    cmd += parent.sendToSerialdevice(setPinMode)    
    cmd += "delay 50"
    cmd += parent.sendToSerialdevice(setPinValue) 
    cmd += "delay 50"
    cmd += parent.sendToSerialdevice(getPinValue) 
    cmd += "delay 2000"
    parent.sendCommandP(cmd)  
}
       
def installed() {
    state.last_request = "OFF"
    initialize(HIGH())
}

def configure()
{
    state.last_request = "OFF"
    initialize(HIGH())
}

def uninstalled() {
    byte[] setPinMode = [SET_PIN_MODE(),getDevicePinNumber(),INPUT_PULLUP()];
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