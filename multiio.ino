#include <RingBuf.h>

#include <TaskScheduler.h>
#include <TaskSchedulerDeclarations.h>
#include <TaskSchedulerSleepMethods.h>

#include <slip.h>

#define UNCONFIGURED 0xFF

#define INPUT_POLL_INTERVAL 20
#define DEFAULT_INPUT_DEBOUNCE 3 

#define MAX_QUEUE_SIZE  5

struct  __attribute__((packed))  InputValue
{
  InputValue(uint8_t pin_,uint8_t value_):pin(pin_),value(value_)
  {
  }

  InputValue(){};

  uint8_t pin = 0xFF;
  uint8_t value = LOW;
};

struct __attribute__((packed)) IoDataFrame
{
  typedef enum CommandType
  {
    REPORT_PIN_CURRENT_VALUE = 0,
    SET_OUTPUT_PIN_VALUE,
    GET_PIN_VALUE,
    SET_PIN_MODE,
    SET_INPUT_PIN_DEBOUNCE,
    ERASE_CONFIG,
    REQUEST_CONFIGURATION
  };

  IoDataFrame(uint8_t command_, const InputValue& data):
  command(command_), pin(data.pin), value(data.value){};
  IoDataFrame(uint8_t command_, uint8_t pin_, uint8_t value_):
  command(command_), pin(pin_), value(value_){};
  IoDataFrame(){};
  uint8_t command = 0xFF;
  uint8_t pin = 0;      
  uint8_t value = LOW;  
};

RingBuf<InputValue, MAX_QUEUE_SIZE> notifyBuffer;

static uint8_t pinCfg[NUM_DIGITAL_PINS];
static uint8_t pinDebounceCfg[NUM_DIGITAL_PINS];
static uint8_t inputLastChange[NUM_DIGITAL_PINS];
static uint8_t inputLastValue[NUM_DIGITAL_PINS];
  
bool inline isInputPin(uint8_t pin)
{
  return pinCfg[pin] == INPUT_PULLUP || pinCfg[pin] == INPUT;
};

bool inline isConfigured(uint8_t pin)
{
  return pinCfg[pin] != UNCONFIGURED;
};

bool inline isReservedPin(uint8_t pin)
{
  return pin == 0 || pin == 1 || pin == 18 || pin == 19 || pin >= NUM_DIGITAL_PINS ;
}

void inline setPinConfig(uint8_t pin, uint8_t mode)
{
  pinCfg[pin] = mode;
}

uint8_t inline getPinConfig(uint8_t pin)
{
  return pinCfg[pin];
}

bool isValidGPIOLevel(uint8_t level)
{
  return level == HIGH || level == LOW;
}

bool isValidGPIOMode(uint8_t mode)
{
  return mode == INPUT || mode == INPUT_PULLUP || mode == OUTPUT || mode == UNCONFIGURED;
}

HardwareSlip slip(Serial1);

Scheduler runner;

void initializeIOConfig()
{
  memset(inputLastChange,HIGH,sizeof(inputLastChange));
  memset(pinDebounceCfg,DEFAULT_INPUT_DEBOUNCE,sizeof(pinDebounceCfg));
  memset(pinCfg, UNCONFIGURED, sizeof(pinCfg));

  for(uint8_t j = 0; j < NUM_DIGITAL_PINS; j ++)
  {
    if(isReservedPin(j))
    { 
      continue;
    }
    pinMode(j,INPUT_PULLUP);
  }
}

void HandleSetOutputPin(const IoDataFrame* data )
{

  if(isReservedPin(data->pin))
  {
    return;
  }

  if(!isConfigured(data->pin))
  {
    return;
  }

  if(isInputPin(data->pin))
  {
    return;
  }

  if(!isValidGPIOLevel(data->value))
  {
    return;
  }

  if(digitalRead(data->pin) == data->value)
  {
    return;
  }

  digitalWrite(data->pin,data->value);
  IoDataFrame responseData({IoDataFrame::REPORT_PIN_CURRENT_VALUE,
    data->pin,
    data->value});

  slip.sendpacket((uint8_t*)&responseData, sizeof(responseData));
}

void HandleSetPinMode(const IoDataFrame* data )
{
  if(isReservedPin(data->pin))
  {
    return;
  }

  if(!isValidGPIOMode(data->value))
  {
    return;
  }

  if(getPinConfig(data->pin) == data->value )
  {
    return;
  }

  if(data->value == UNCONFIGURED)
  {
    pinMode(data->pin,INPUT_PULLUP);
  }
  else
  {
    pinMode(data->pin,data->value);
  }

  setPinConfig(data->pin, data->value);
}

void HandleSetInputPinDebounce(const IoDataFrame* data )
{
  if(isReservedPin(data->pin))
  {
    return;
  }

  pinDebounceCfg[data->pin] = data->value;
}

void HandleResetConfig(const IoDataFrame* /*data*/ )
{

}

void HandleGetPinValue(const IoDataFrame* data )
{
  if(isReservedPin(data->pin))
  {
    return;
  }

  IoDataFrame responseData({IoDataFrame::REPORT_PIN_CURRENT_VALUE,
        data->pin,
        digitalRead(data->pin)});
  slip.sendpacket((uint8_t*)&responseData, sizeof(responseData));
}

void slipReadCallback(uint8_t * buff,uint8_t len)
{ 
  IoDataFrame* data = (IoDataFrame*) buff;

  switch(data->command)
  {
    case IoDataFrame::SET_OUTPUT_PIN_VALUE:
      HandleSetOutputPin(data);
      break;
    case IoDataFrame::SET_PIN_MODE:
      HandleSetPinMode(data);
      break;
    case IoDataFrame::GET_PIN_VALUE:
      HandleGetPinValue(data);
      break;
    case IoDataFrame::ERASE_CONFIG:
      HandleResetConfig(data);
      break;
    case IoDataFrame::SET_INPUT_PIN_DEBOUNCE:
      HandleSetInputPinDebounce(data);
      break;
    default:
      break;
  }
}

bool inline checkPinChangeAndDebounce(uint8_t pin)
{
  uint8_t tmp = digitalRead(pin);
  if(tmp == inputLastValue[pin])
  {
    inputLastChange[pin] = 0;
    return true;
  }

  if(inputLastChange[pin] < pinDebounceCfg[pin] )
  {
    inputLastChange[pin]++;
    return true;
  }

  if(notifyBuffer.push(InputValue({pin,tmp})))
  {
    inputLastValue[pin] = tmp;
    inputLastChange[pin] = 0;
    return true;
  }

  return false;
}

void taskReadInputPin();
void taskNotifyIOChange();
void taskProcessSlip();
void taskStartUp();

Task t1(INPUT_POLL_INTERVAL, TASK_FOREVER, &taskReadInputPin);
Task t2(0, TASK_FOREVER, &taskNotifyIOChange);
Task t3(0, TASK_FOREVER, &taskProcessSlip);
Task t4(500, NUM_DIGITAL_PINS*3, &taskStartUp);

void taskReadInputPin()
{
  for(uint8_t j = 0; j < NUM_DIGITAL_PINS; j ++)
  {
    if(isReservedPin(j))
    {
      continue;
    }

    if(!isInputPin(j))
    {
      continue;
    }

    if(!checkPinChangeAndDebounce(j))
    {
      break;
    }
  }
}

void taskNotifyIOChange()
{
  for(uint8_t i = 0; i < 5; i ++)
  {

    InputValue inputChange;
    if(!notifyBuffer.lockedPop(inputChange))
    {
      break;
    }

    /*Serial.print("GPIO pin:");
    Serial.print(inputChange.pin, DEC);
    Serial.print(" new value:");
    Serial.print(inputChange.value, DEC);
    Serial.println(" change detected");*/

    IoDataFrame data({IoDataFrame::REPORT_PIN_CURRENT_VALUE,inputChange});
    slip.sendpacket((uint8_t*)&data, sizeof(data));
  }
}

void taskProcessSlip()
{
  slip.proc();
}

void taskStartUp()
{
  static uint8_t j = 0;
  uint8_t ndx = j++ % NUM_DIGITAL_PINS;
  if(isConfigured(ndx))
  {
    return;
  }
 
  IoDataFrame data({IoDataFrame::REQUEST_CONFIGURATION,ndx,0});
  slip.sendpacket((uint8_t*)&data, sizeof(data));
}

void setup() {
  Serial.begin(115200);

  Serial1.begin(9600);
  slip.setCallback(slipReadCallback);

  initializeIOConfig();

  runner.addTask(t1);
  runner.addTask(t2);
  runner.addTask(t3);
  runner.addTask(t4);
  t1.enable();
  t2.enable();
  t3.enable();
  t4.enable();
}

// the loop function runs over and over again forever
void loop() {
  runner.execute();
}