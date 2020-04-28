#include <RingBuf.h>

#include <TaskScheduler.h>
#include <TaskSchedulerDeclarations.h>
#include <TaskSchedulerSleepMethods.h>

#include <EEPROMWearLevel.h>
#include <slip.h>

#define EEPROM_LAYOUT_VERSION 0
#define AMOUNT_OF_INDEXES 2
#define INDEX_CONFIGURATION_IO 0
#define INDEX_DEBOUNCE_IO 1
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
    ERASE_CONFIG
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

class WrapperPortConfig
{
public:
  WrapperPortConfig()
  {
    memset(cfg,INPUT_PULLUP,sizeof(cfg));
  };

  WrapperPortConfig(uint8_t val):defaultVal{val}
  {
    memset(cfg,val,sizeof(cfg));
  };

  uint8_t lenght()
  {
    return NUM_DIGITAL_PINS;
  };

  uint8_t& operator [] (uint8_t j) {
    return cfg[j]; 
  };

  bool isInputPin(uint8_t j)
  {
    return cfg[j] == INPUT_PULLUP || cfg[j] == INPUT;
  };

  void resetConfig()
  {
    memset(cfg,defaultVal,sizeof(cfg));
  }

private:
  uint8_t cfg[NUM_DIGITAL_PINS];
  uint8_t defaultVal = INPUT_PULLUP;
};

static WrapperPortConfig portConfig;
static WrapperPortConfig portDebounce(DEFAULT_INPUT_DEBOUNCE);
static uint8_t inputLastChange[NUM_DIGITAL_PINS];
static uint8_t inputLastValue[NUM_DIGITAL_PINS];

HardwareSlip slip(Serial1);

Scheduler runner;

void HandleSetOutputPin(const IoDataFrame* data )
{
  if(!portConfig.isInputPin(data->pin))
  {
    if(digitalRead(data->pin) != data->value)
    {
      digitalWrite(data->pin,data->value);
      IoDataFrame responseData({IoDataFrame::REPORT_PIN_CURRENT_VALUE,
        data->pin,
        data->value});

      slip.sendpacket((uint8_t*)&responseData, sizeof(responseData));
    }
  }
}

void HandleSetPinMode(const IoDataFrame* data )
{
  if(portConfig[data->pin] != data->value )
  {
    pinMode(data->pin,data->value);
    portConfig[data->pin] = data->value;
    EEPROMwl.put(INDEX_CONFIGURATION_IO,portConfig);
  }
}

void HandleSetInputPinDebounce(const IoDataFrame* data )
{
  if(portDebounce[data->pin] != data->value )
  {
    portDebounce[data->pin] = data->value;
    EEPROMwl.put(INDEX_DEBOUNCE_IO,portDebounce);
  }
}

void HandleResetConfig(const IoDataFrame* /*data*/ )
{
  /*portConfig.resetConfig();
  EEPROMwl.put(INDEX_CONFIGURATION_IO,portConfig);

  portDebounce.resetConfig();
  EEPROMwl.put(INDEX_DEBOUNCE_IO, portDebounce);*/
  EEPROMwl.begin(0xFF, AMOUNT_OF_INDEXES);
  EEPROMwl.begin(EEPROM_LAYOUT_VERSION, AMOUNT_OF_INDEXES);
}

void HandleGetPinValue(const IoDataFrame* data )
{
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

bool inline isReservedPin(uint8_t pin)
{
  return (pin == 0 || pin == 1 || pin == 18 || pin == 19);
}

void initializeIOConfig()
{
  memset(inputLastChange,0,sizeof(inputLastChange));
  memset(inputLastValue,0,sizeof(inputLastValue));

  EEPROMwl.get(INDEX_CONFIGURATION_IO, portConfig);
  EEPROMwl.get(INDEX_DEBOUNCE_IO,portDebounce);
  for(uint8_t j = 0; j < portConfig.lenght(); j ++)
  {
    if(isReservedPin(j))
    { 
      continue;
    }

    pinMode(j,portConfig[j]);
    inputLastValue[j]=digitalRead(j);

    IoDataFrame data({IoDataFrame::REPORT_PIN_CURRENT_VALUE,j,digitalRead(j)});
    slip.sendpacket((uint8_t*)&data, sizeof(data));
    delay(500);
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

  if(inputLastChange[pin] < portDebounce[pin] )
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

void taskReadInputPin()
{
  for(uint8_t j = 0; j < portConfig.lenght(); j ++)
  {
    if(isReservedPin(j))
    {
      continue;
    }

    if(!portConfig.isInputPin(j))
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

Task t1(INPUT_POLL_INTERVAL, TASK_FOREVER, &taskReadInputPin);
Task t2(0, TASK_FOREVER, &taskNotifyIOChange);
Task t3(0, TASK_FOREVER, &taskProcessSlip);

void setup() {
  Serial.begin(115200);

  Serial1.begin(9600);
  slip.setCallback(slipReadCallback);

  EEPROMwl.begin(EEPROM_LAYOUT_VERSION, AMOUNT_OF_INDEXES);

  initializeIOConfig();

  runner.addTask(t1);
  runner.addTask(t2);
  runner.addTask(t3);
  t1.enable();
  t2.enable();
  t3.enable();
}

// the loop function runs over and over again forever
void loop() {
  runner.execute();
}