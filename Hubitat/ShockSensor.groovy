metadata {
    definition (name: "ShockSensor", namespace: "iharyadi", author: "iharyadi") {
        capability "ShockSensor"
        capability "Refresh"
        capability "Configuration"
        capability "Sensor"
    }
}

private short REPORT_PIN_CURRENT_VALUE()
{
    return 0x00
}

private short SET_INPUT_PIN_DEBOUNCE()
{
     return 0x04   
}

private short LOW()
{
    return 0;   
}

private short INPUT_PULLUP()
{
   return 0x02;   
}

private short SET_PIN_MODE()
{
    return 0x03   
}

private short GET_PIN_VALUE()
{
    return 0x02    
}

private short REQUEST_CONFIGURATION()
{
    return 0x06   
}

private short getDevicePinNumber()
{
    String devicePinNumber = device.getDataValue("pageNumber")
    return (short) devicePinNumber.toInteger();
}

def parse(def data) { 
    
    log.info "data $data"
    
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
    
    return createEvent(name:"shock", value:(pinValue == LOW())?"clear":"detected", isStateChange:true)
}

def configure_child() {
}

def installed() {
    initialize()
}

def initialize()
{
    byte[] setPinMode  = [SET_PIN_MODE(),getDevicePinNumber(),INPUT_PULLUP()];
    byte[] getpinvalue = [GET_PIN_VALUE(),getDevicePinNumber(),0]
    byte[] setpindebounce = [SET_INPUT_PIN_DEBOUNCE(), getDevicePinNumber(),1]
    def cmd = []
    cmd += parent.sendToSerialdevice(setPinMode)
    cmd += "delay 50"
    cmd += parent.sendToSerialdevice(getpinvalue)  
    cmd += "delay 50"
    cmd += parent.sendToSerialdevice(setpindebounce)    
    cmd += "delay 2000"
    parent.sendCommandP(cmd) 
}

def configure()
{
    initialize()
}

def refresh()
{
    byte[] getpinvalue = [GET_PIN_VALUE(),getDevicePinNumber(),0];
    def cmd = []
    cmd += parent.sendToSerialdevice(getpinvalue)  
    parent.sendCommandP(cmd) 
}
