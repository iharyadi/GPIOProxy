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

private short PULSE_OUTPUT_PIN()
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
    cmd += "delay 500"
    parent.sendCommandP(cmd) 
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
    byte[] setPinMode = [SET_PIN_MODE(),getDevicePinNumber(),UNCONFIGURED()];
    def cmd = []
    cmd += parent.sendToSerialdevice(setPinMode)    
    parent.sendCommandP(cmd) 
}