metadata {
    definition (name: "PulseDevice", namespace: "iharyadi", author: "iharyadi") {
        capability "RelaySwitch"
        capability "Switch"
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

private short PULSE_OUTPUT_PIN()
{
    return 0x07
}

private short INPUT()
{
   return 0x00;   
}

private short UNCONFIGURED()
{
   return 0xFF;   
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

private byte[]  toBytes(int i)
{
  byte[] result = new byte[4];

  result[0] = (byte) (i);
  result[1] = (byte) (i >> 8);
  result[2] = (byte) (i >> 16);
  result[3] = (byte) (i >> 24);
  return result;
}

private sendPulse(short level, int delay)
{
    ByteArrayOutputStream outputStream = new ByteArrayOutputStream( )
    byte [] tmp = [PULSE_OUTPUT_PIN(),getDevicePinNumber(),level]
    outputStream.write(tmp)
    outputStream.write(toBytes(delay))
    
    byte [] setDelay = outputStream.toByteArray( )
    
    def cmd = []
    cmd += parent.sendToSerialdevice(setDelay)  
    cmd += "delay 2000"
    parent.sendCommandP(cmd) 
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
    sendPulse(HIGH(),5)
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
    initialize(LOW())
}

def configure()
{
    initialize(LOW())
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