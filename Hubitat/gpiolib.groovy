library (
  author: "iharyadi",
  category: "gpio",
  description: "gpio related library",
  name: "gpiolib",
  namespace: "iharyadi",
  documentationLink: "http://www.google.com/"
)

private short REPORT_PIN_CURRENT_VALUE()
{
    return 0x00
}

private short SET_OUTPUT_PIN_VALUE()
{
    return 0x01    
}

private short GET_PIN_VALUE()
{
    return 0x02    
}

private short SET_PIN_MODE()
{
    return 0x03   
}

private short SET_INPUT_PIN_DEBOUNCE()
{
    return 0x04   
}

private short REQUEST_CONFIGURATION()
{
    return 0x06   
}

private short PULSE_OUTPUT_PIN()
{
    return 0x07   
}

private short SET_INPUT_PIN_DEBOUNCE_MODE()
{
    return 0x08
}

private short DEBOUNCE_NORMAL_MODE()
{
    return 0x00
}

private short DEBOUNCE_IGNORE_LEVEL_MODE()
{
    return 0x01
}

private short LOW()
{
    return 0;   
}

private short HIGH()
{
    return 1;   
}

private short INPUT()
{
   return 0x00;   
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

def refreshImp()
{
    byte[] getpinvalue = [GET_PIN_VALUE(),getDevicePinNumber(),0];
    def cmd = []
    cmd += parent.sendToSerialdevice(getpinvalue)  
    cmd += "delay 100"
    parent.sendCommandP(cmd) 
}

def unconfiguredImp()
{
    byte[] setPinMode = [SET_PIN_MODE(),getDevicePinNumber(),UNCONFIGURED()];
    def cmd = []
    cmd += parent.sendToSerialdevice(setPinMode) 
    cmd += "delay 100"
    parent.sendCommandP(cmd) 
}
