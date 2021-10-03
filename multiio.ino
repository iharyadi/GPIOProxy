#include <RingBuf.h>

#include <TaskScheduler.h>
#include <TaskSchedulerDeclarations.h>
#include <TaskSchedulerSleepMethods.h>

#include <slip.h>

#if defined(ARDUINO_AVR_LARDU_328E)
#include <SoftwareSerial.h>
#endif

#define UNCONFIGURED 0xFF

#define INPUT_POLL_INTERVAL 1
#define DEFAULT_INPUT_DEBOUNCE 3 

#define DEBOUNCE_NORMAL 0 
#define DEBOUNCE_IGNORE_LEVEL 1
#define DEFAULT_DEBOUNCE_MODE DEBOUNCE_NORMAL 

#define MAX_QUEUE_SIZE  5

struct  __attribute__((packed))  GPIOValue
{
  GPIOValue(uint8_t pin_,uint8_t value_):pin(pin_),value(value_)
  {
  }

  GPIOValue(){};

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
    REQUEST_CONFIGURATION,
    PULSE_OUTPUT_PIN,
    SET_INPUT_PIN_DEBOUNCE_MODE,
  };

  IoDataFrame(uint8_t command_, const GPIOValue& data):
  command(command_), pin(data.pin), value(data.value){};
  IoDataFrame(uint8_t command_, uint8_t pin_, uint8_t value_):
  command(command_), pin(pin_), value(value_){};
  IoDataFrame(){};
  uint8_t command = 0xFF;
  uint8_t pin = 0;      
  uint8_t value = LOW;  
};

struct __attribute__((packed)) IoOutputPulseDataFrame:IoDataFrame
{
  uint32_t delay;
};

RingBuf<GPIOValue, MAX_QUEUE_SIZE> notifyBuffer;

static uint8_t pinCfg[NUM_DIGITAL_PINS];
static uint8_t pinDebounceCfg[NUM_DIGITAL_PINS];
static uint8_t pinDebounceModeCfg[NUM_DIGITAL_PINS];
static uint8_t pinLastChange[NUM_DIGITAL_PINS];
static uint8_t pinLastValue[NUM_DIGITAL_PINS];
static uint8_t pinLastValueReported[NUM_DIGITAL_PINS];

void HandleSetOutputPinImpl(uint8_t pin, uint8_t level);
  
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
#if defined(ARDUINO_AVR_LARDU_328E)
  return pin == 0 || pin == 1 || pin == 4 || pin == 5 || pin >= NUM_DIGITAL_PINS ;
#else
  return pin == 0 || pin == 1 || pin == 18 || pin == 19 || pin >= NUM_DIGITAL_PINS ;
#endif
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


#if defined(ARDUINO_AVR_LARDU_328E)
SoftwareSerial Serial1(4, 5);
SoftwareSlip slip(Serial1);
#else
HardwareSlip slip(Serial1);
#endif

tsk::Scheduler runner;

template<uint8_t N, template <uint8_t IPin> class H, decltype(&H<N-1>::handler)... func> struct HandlerTbl: HandlerTbl< N-1, H, H<N-1>::handler, func... > {};

template<template <uint8_t IPin> class H, decltype(&H<0>::handler)... func> struct HandlerTbl<0,H, func...> { 
  decltype(&H<0>::handler) const func_array[sizeof...(func)] =  {func...};
  decltype(&H<0>::handler) operator [] (uint8_t ndx) const { return func_array[ndx]; };
};

template <uint8_t TPin> struct TimerHandlerLow
{
  static void handler(){
     HandleSetOutputPinImpl(TPin, LOW);
  };
};

template <uint8_t TPin> struct TimerHandlerHigh
{
  static void handler(){
    HandleSetOutputPinImpl(TPin, HIGH);
  };
};

static const HandlerTbl<NUM_DIGITAL_PINS, TimerHandlerLow> timerHandlerTblLow;
static const HandlerTbl<NUM_DIGITAL_PINS, TimerHandlerHigh> timerHandlerTblHigh;

template <uint8_t N, uint8_t T> struct TimerArray:TimerArray<N-1, T>
{
  tsk::Task task =  {0, TASK_ONCE, timerHandlerTblHigh[N-1], &runner, false, NULL, NULL};
  TimerArray()
  {
     TimerArray<0, T>::tasks[N-1]=&task;
  }
};

template <uint8_t T> struct TimerArray<0, T>
{
  tsk::Task* tasks [T];

  tsk::Task& operator [](uint8_t ndx){return *tasks[ndx];};
};

TimerArray<NUM_DIGITAL_PINS,NUM_DIGITAL_PINS> timerArray;

void initializeIOConfig()
{
  memset(pinLastChange,HIGH,sizeof(pinLastChange));
  memset(pinDebounceCfg,DEFAULT_INPUT_DEBOUNCE,sizeof(pinDebounceCfg));
  memset(pinDebounceModeCfg,DEFAULT_DEBOUNCE_MODE,sizeof(pinDebounceModeCfg));
  memset(pinCfg, UNCONFIGURED, sizeof(pinCfg));
  memset(pinLastValueReported,0,sizeof(pinLastValueReported));

  for(uint8_t j = 0; j < NUM_DIGITAL_PINS; j ++)
  {
    if(isReservedPin(j))
    { 
      continue;
    }
    pinMode(j,INPUT);
  }
}

void HandleSetOutputPin(const struct IoDataFrame* data )
{
  HandleSetOutputPinImpl(data->pin, data->value);
}

void HandleSetOutputPinImpl(uint8_t pin, uint8_t value)
{

  if(isReservedPin(pin))
  {
    return;
  }

  if(!isConfigured(pin))
  {
    return;
  }

  if(isInputPin(pin))
  {
    return;
  }

  if(!isValidGPIOLevel(value))
  {
    return;
  }

  if(digitalRead(pin) == value)
  {
    return;
  }

  digitalWrite(pin,value);

  notifyBuffer.lockedPush(GPIOValue({pin,value}));
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
    pinMode(data->pin,INPUT);
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

void HandleSetInputPinDebounceMode(const IoDataFrame* data )
{
  if(isReservedPin(data->pin))
  {
    return;
  }

  pinDebounceModeCfg[data->pin] = data->value;
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

  notifyBuffer.lockedPush(GPIOValue({data->pin,digitalRead(data->pin)}));
}

void HandlePulseOutputPin(const IoDataFrame* data)
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

    tsk::Task& task = timerArray[data->pin];
    if(task.isEnabled())
    {
        return;
    }

    if(!isValidGPIOLevel(data->value))
    {
        return;
    }

    const IoOutputPulseDataFrame* pulseData =  reinterpret_cast<const IoOutputPulseDataFrame*>(data);

    uint8_t initValue = LOW;
    tsk::TaskCallback cb =  NULL;
    
    if(data->value == LOW)
    {
        initValue = HIGH;
        cb = timerHandlerTblHigh[data->pin];
    }
    else
    {
        initValue = LOW;
        cb = timerHandlerTblLow[data->pin];
    }

    if(digitalRead(data->pin) != initValue)
    {
        digitalWrite(data->pin, initValue);
    }

    HandleSetOutputPinImpl(data->pin, data->value);
    task.setCallback(cb);
    task.restartDelayed(pulseData->delay);
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
    case IoDataFrame::PULSE_OUTPUT_PIN:
      HandlePulseOutputPin(data);
      break;
    case IoDataFrame::SET_INPUT_PIN_DEBOUNCE_MODE:
      HandleSetInputPinDebounceMode(data);
      break;
    default:
      break;
  }
}

bool inline checkPinChangeAndDebounce(uint8_t pin)
{
  uint8_t tmp = digitalRead(pin);
  if(tmp == pinLastValue[pin])
  {
    pinLastChange[pin] = 0;
    return true;
  }

  if(pinLastChange[pin] < pinDebounceCfg[pin] )
  {
    pinLastChange[pin]++;
    return true;
  }

  if(notifyBuffer.lockedPush(GPIOValue({pin,tmp})))
  {
    pinLastValue[pin] = tmp;
    pinLastChange[pin] = 0;
    return true;
  }

  return false;
}

bool inline checkPinChangeAndDebounceIgnoreLevel(uint8_t pin)
{
    uint8_t tmp = digitalRead(pin);

    if(pinLastChange[pin] == 0)
    {
        if(tmp != pinLastValue[pin])
        {
            if(notifyBuffer.lockedPush(GPIOValue({pin,HIGH})))
            {
                pinLastValue[pin] = tmp;
                pinLastChange[pin]++;
                return true;
            }
        }
    }
    else
    {
        if(tmp != pinLastValue[pin])
        {
            pinLastChange[pin] = 1;
            pinLastValue[pin] = tmp;
        }
        else
        {
            pinLastChange[pin]++;
            if(pinLastChange[pin] >= pinDebounceCfg[pin])
            {
                if(notifyBuffer.lockedPush(GPIOValue({pin,LOW})))
                {
                    pinLastValue[pin] = tmp;
                    pinLastChange[pin] = 0;
                    return true;
                }
            }
        }
    }
    return false;
}

void taskReadInputPin();
void taskNotifyIOChange();
void taskProcessSlip();
void taskStartUp();
void taskReportPin();

tsk::Task t1(INPUT_POLL_INTERVAL, TASK_FOREVER, &taskReadInputPin);
tsk::Task t2(0, TASK_FOREVER, &taskNotifyIOChange);
tsk::Task t3(0, TASK_FOREVER, &taskProcessSlip);
tsk::Task t4(350, NUM_DIGITAL_PINS*3, &taskStartUp);
tsk::Task t5(5000, TASK_FOREVER, &taskReportPin);

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

    if(pinDebounceModeCfg[j] == DEBOUNCE_NORMAL)
    {
        if(!checkPinChangeAndDebounce(j))
        {
            break;
        }
    }
    else
    {
        if(!checkPinChangeAndDebounceIgnoreLevel(j))
        {
            break;
        }
    }
  }
}

void taskNotifyIOChange()
{
  for(uint8_t i = 0; i < MAX_QUEUE_SIZE; i ++)
  {
    GPIOValue value;
    if(!notifyBuffer.lockedPop(value))
    {
      break;
    }

    IoDataFrame data({IoDataFrame::REPORT_PIN_CURRENT_VALUE,value});
    slip.sendpacket((uint8_t*)&data, sizeof(data));
    pinLastValueReported[value.pin] = 0;
    delayMicroseconds(100);
  }
}

void taskReportPin()
{
  for(uint8_t j = 0; j < NUM_DIGITAL_PINS; j ++)
  {
    if(isReservedPin(j))
    {
      continue;
    }

    if(!isConfigured(j))
    {
      continue;
    }

    if(pinLastValueReported[j] < 60)
    {
        pinLastValueReported[j]++;
        continue;
    }

    uint8_t value = LOW;

    if(pinDebounceModeCfg[j] == DEBOUNCE_NORMAL)
    {
        value = digitalRead(j);
    }
    else
    {
        value = (pinLastChange[j] == 0) ? LOW:HIGH; 
    }

    IoDataFrame data({IoDataFrame::REPORT_PIN_CURRENT_VALUE,GPIOValue(j,value)});
    slip.sendpacket((uint8_t*)&data, sizeof(data));
    pinLastValueReported[j] = 0;
    delayMicroseconds(100);
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
  //Serial.begin(115200);

  Serial1.begin(9600);
  slip.setCallback(slipReadCallback);

  initializeIOConfig();

  runner.addTask(t1);
  runner.addTask(t2);
  runner.addTask(t3);
  runner.addTask(t4);
  runner.addTask(t5);
  t1.enable();
  t2.enable();
  t3.enable();
  t4.enable();
  t4.delay(10000);
  t5.enable();
}

// the loop function runs over and over again forever
void loop() {
  runner.execute();
}