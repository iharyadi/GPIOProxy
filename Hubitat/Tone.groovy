metadata {
    definition (name: "Tone", namespace: "iharyadi", author: "iharyadi") {
        capability "Tone"
        capability "Sensor"
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

private short REQUEST_CONFIGURATION()
{
    return 0x06   
}

private short REPORT_PIN_CURRENT_VALUE()
{
    return 0x00
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
    
    byte[] setPinValueHigh = [SET_OUTPUT_PIN_VALUE(),getDevicePinNumber(),HIGH()];
    byte[] setPinValueLow = [SET_OUTPUT_PIN_VALUE(),getDevicePinNumber(),LOW()];
    def cmd = [] 
    cmd += parent.sendToSerialdevice(setPinValueHigh)
    cmd += "delay 500"
    cmd += parent.sendToSerialdevice(setPinValueLow)
    parent.sendCommandP(cmd) 
    
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
    cmd += "delay 500"
    cmd += parent.sendToSerialdevice(setPinValue) 
    cmd += "delay 2000"
    parent.sendCommandP(cmd) 
}

def uninstalled() {
    byte[] setPinMode = [SET_PIN_MODE(),getDevicePinNumber(),INPUT_PULLUP()];
    def cmd = []
    cmd += parent.sendToSerialdevice(setPinMode)    
    parent.sendCommandP(cmd) 
}