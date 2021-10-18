#include <RingBuf.h>

#if !defined(ARDUINO_SAM_DUE)
#include <EEPROM.h>
#endif

namespace tsk
{
#include <TaskScheduler.h>
#include <TaskSchedulerDeclarations.h>
#include <TaskSchedulerSleepMethods.h>
}

#include <slip.h>

#if defined(ARDUINO_AVR_LARDU_328E)  || defined (ARDUINO_AVR_UNO )
#include <SoftwareSerial.h>
#endif

#define UNCONFIGURED 0xFF

#define INPUT_POLL_INTERVAL 1
#define INPUT_REPORT_INTERVAL 5000
#define REQUEST_CONFIG_INTERVAL_PER_PIN 350
#define REQUEST_CONFIG_DELAY 10000
#if defined(ARDUINO_AVR_LARDU_328E) || defined (ARDUINO_AVR_UNO )
#define INPUT_REPORT_SPLIT_INTERVAL 10
#define NOTIFY_PIN_STATUS_INTERVAL 5
#else
#define INPUT_REPORT_SPLIT_INTERVAL 5
#define NOTIFY_PIN_STATUS_INTERVAL 0
#endif

#define DEFAULT_INPUT_DEBOUNCE 3

#define DEBOUNCE_NORMAL 0
#define DEBOUNCE_IGNORE_LEVEL 1
#define DEFAULT_DEBOUNCE_MODE DEBOUNCE_NORMAL

#define MAX_QUEUE_SIZE 10

#if defined(ARDUINO_SAM_DUE)
constexpr bool useInterrupt = true;
#define USE_EEPROM 0
#define VOLATILE volatile
#elif defined(ARDUINO_AVR_LARDU_328E)
#define USE_EEPROM 0
constexpr bool useInterrupt = false;
#define VOLATILE
#else
#define USE_EEPROM 1
constexpr bool useInterrupt = false;
#define VOLATILE
#endif

template <bool b>
class ScopedDisableInterruptImp
{
public:
  ScopedDisableInterruptImp()
  {
    if (count == 0)
    {
      noInterrupts();
    }
    count++;
  }

  ~ScopedDisableInterruptImp()
  {
    count--;
    if (count == 0)
    {
      interrupts();
    }
  }

private:
  static uint32_t count;
};

template <bool b>
uint32_t ScopedDisableInterruptImp<b>::count = 0;

template <>
class ScopedDisableInterruptImp<false>
{
public:
  ScopedDisableInterruptImp()
  {
  }

  ~ScopedDisableInterruptImp()
  {
  }
};

typedef ScopedDisableInterruptImp<useInterrupt> ScopedDisableInterrupt;

struct __attribute__((packed)) GPIOValue
{
  GPIOValue(uint8_t pin_, uint8_t value_) : pin(pin_), value(value_)
  {
  }

  GPIOValue(){};

  uint8_t pin = 0xFF;
  uint8_t value = LOW;
};

struct __attribute__((packed)) IoDataFrame
{
  enum CommandType
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

  IoDataFrame(uint8_t command_, const GPIOValue &data) : command(command_), pin(data.pin), value(data.value){};
  IoDataFrame(uint8_t command_, uint8_t pin_, uint8_t value_) : command(command_), pin(pin_), value(value_){};
  IoDataFrame(){};
  uint8_t command = 0xFF;
  uint8_t pin = 0;
  uint8_t value = LOW;
};

struct __attribute__((packed)) IoOutputPulseDataFrame : IoDataFrame
{
  uint32_t delay;
};

RingBuf<GPIOValue, MAX_QUEUE_SIZE> notifyBuffer;

void HandleSetOutputPinImpl(uint8_t pin, uint8_t level);
bool checkPinChangeAndDebounceIgnoreLevelInterruptHandler(uint8_t pin);
bool checkPinChangeAndDebounceIgnoreLevelInterruptTimeout(uint8_t pin);
bool checkPinChangeAndDebounceInterruptHandler(uint8_t pin);
bool checkPinChangeAndDebounceInterruptTimeout(uint8_t pin);

bool inline isInputPin(uint8_t pin);
bool inline isReservedPin(uint8_t pin);

#if USE_EEPROM
struct Configuration
{

private:
  static const int STATUS_SIZE = 1;
  template<int offset> struct EEPROM_WRAPPER
  {
    EERef operator []( int address)
    {
       return EEPROM[ address + offset + STATUS_SIZE ];
    }
  
    void memset(uint8_t val,size_t s)
    {
      for (int index = 0 ; index < s ; index++) {
        EEPROM[ index + offset + STATUS_SIZE ] = val;
      }
    }
  };

public:

  EEPROM_WRAPPER<0> pinCfg;
  EEPROM_WRAPPER<0+NUM_DIGITAL_PINS>   pinDebounceValueCfg;
  EEPROM_WRAPPER<0+NUM_DIGITAL_PINS+NUM_DIGITAL_PINS> pinDebounceModeCfg;

  VOLATILE uint8_t pinLastChange[NUM_DIGITAL_PINS];
  VOLATILE uint8_t pinLastValue[NUM_DIGITAL_PINS];
  uint8_t pinLastValueReported[NUM_DIGITAL_PINS];
  
  void begin()
  {
    if(EEPROM.length() == 0 || EEPROM[0] > 0)
    {
      pinDebounceValueCfg.memset(DEFAULT_INPUT_DEBOUNCE, NUM_DIGITAL_PINS);
      pinDebounceModeCfg.memset(DEFAULT_DEBOUNCE_MODE, NUM_DIGITAL_PINS);
      pinCfg.memset(UNCONFIGURED, NUM_DIGITAL_PINS);
      EEPROM[0] = 0;
    }

    memset(const_cast<uint8_t *>(pinLastValue), 0, sizeof(pinLastValue));
    memset(const_cast<uint8_t *>(pinLastValueReported), 0, sizeof(pinLastValueReported));
    memset(const_cast<uint8_t *>(pinLastChange), HIGH, sizeof(pinLastChange));

    for (uint8_t j = 0; j < NUM_DIGITAL_PINS; j++)
    {
      if (isReservedPin(j))
      {
        continue;
      }

      if(pinCfg[j] == UNCONFIGURED)
      {
        pinMode(j, INPUT);
      }
      else
      {
        pinMode(j, pinCfg[j]);
      }
    }
  }

  void reset()
  {
    pinDebounceValueCfg.memset(DEFAULT_INPUT_DEBOUNCE, NUM_DIGITAL_PINS);
    pinDebounceModeCfg.memset(DEFAULT_DEBOUNCE_MODE, NUM_DIGITAL_PINS);
    pinCfg.memset(UNCONFIGURED, NUM_DIGITAL_PINS);
    EEPROM[0] = 0;
  
    for (uint8_t j = 0; j < NUM_DIGITAL_PINS; j++)
    {
      if (isReservedPin(j))
      {
        continue;
      }

      if(pinCfg[j] == UNCONFIGURED)
      {
        pinMode(j, INPUT);
      }
    }
  }
};
#else //USE_EEPROM
struct Configuration
{
  VOLATILE uint8_t pinCfg[NUM_DIGITAL_PINS];
  VOLATILE uint8_t pinDebounceValueCfg[NUM_DIGITAL_PINS];
  VOLATILE uint8_t pinDebounceModeCfg[NUM_DIGITAL_PINS];
  VOLATILE uint8_t pinLastChange[NUM_DIGITAL_PINS];
  VOLATILE uint8_t pinLastValue[NUM_DIGITAL_PINS];
  uint8_t pinLastValueReported[NUM_DIGITAL_PINS];
  
  void begin()
  {
    reset();
  }

  void reset()
  {
    memset(const_cast<uint8_t *>(pinDebounceValueCfg), DEFAULT_INPUT_DEBOUNCE, sizeof(pinDebounceValueCfg));
    memset(const_cast<uint8_t *>(pinDebounceModeCfg), DEFAULT_DEBOUNCE_MODE, sizeof(pinDebounceModeCfg));
    memset(const_cast<uint8_t *>(pinCfg), UNCONFIGURED, sizeof(pinCfg));
    memset(const_cast<uint8_t *>(pinLastValueReported), 0, sizeof(pinLastValueReported));
    memset(const_cast<uint8_t *>(pinLastValue), 0, sizeof(pinLastValue));
    memset(const_cast<uint8_t *>(pinLastChange), HIGH, sizeof(pinLastChange));

    for (uint8_t j = 0; j < NUM_DIGITAL_PINS; j++)
    {
      if (isReservedPin(j))
      {
        continue;
      }
      pinMode(j, INPUT);
    }
  }
};
#endif //USE_EEPROM

static Configuration config;

bool inline isInputPin(uint8_t pin)
{
  return config.pinCfg[pin] == INPUT_PULLUP || config.pinCfg[pin] == INPUT;
};

bool inline isConfigured(uint8_t pin)
{
  return config.pinCfg[pin] != UNCONFIGURED;
};

bool inline isReservedPin(uint8_t pin)
{
#if defined(ARDUINO_AVR_LARDU_328E) || defined (ARDUINO_AVR_UNO )
  return pin == 0 || pin == 1 || pin == 4 || pin == 5 || pin >= NUM_DIGITAL_PINS;
#else
  return pin == 0 || pin == 1 || pin == 18 || pin == 19 || pin >= NUM_DIGITAL_PINS;
#endif
}

void inline setPinConfig(uint8_t pin, uint8_t mode)
{
  config.pinCfg[pin] = mode;
}

uint8_t inline getPinConfig(uint8_t pin)
{
  return config.pinCfg[pin];
}

bool inline isValidGPIOLevel(uint8_t level)
{
  return level == HIGH || level == LOW;
}

bool isValidGPIOMode(uint8_t mode)
{
  return mode == INPUT || mode == INPUT_PULLUP || mode == OUTPUT || mode == UNCONFIGURED;
}

bool inline isNormalDebounce(uint8_t pin)
{
  return config.pinDebounceModeCfg[pin] == DEBOUNCE_NORMAL;
}

uint8_t inline getPinValueHelper(uint8_t pin)
{
  return (isInputPin(pin) && !isNormalDebounce(pin)) ? (config.pinLastChange[pin] == 0) ? LOW : HIGH : digitalRead(pin);
}

#if defined(ARDUINO_AVR_LARDU_328E) || defined (ARDUINO_AVR_UNO )
SoftwareSerial Serial1(4, 5);
SoftwareSlip slip(Serial1);
#else
HardwareSlip slip(Serial1);
#endif

tsk::Scheduler runner;

template <uint8_t N, template <uint8_t IPin> class H, decltype(&H<N - 1>::handler)... func>
struct HandlerTbl : HandlerTbl<N - 1, H, H<N - 1>::handler, func...>
{
};

template <template <uint8_t IPin> class H, decltype(&H<0>::handler)... func>
struct HandlerTbl<0, H, func...>
{
  const decltype(&H<0>::handler) func_array[sizeof...(func)] = {func...};
  decltype(&H<0>::handler) operator[](uint8_t ndx) const { return func_array[ndx]; };
  typedef decltype(&H<0>::handler) handler_type;
};

template <uint8_t TPin>
struct TimerHandlerLow
{
  static void handler()
  {
    HandleSetOutputPinImpl(TPin, LOW);
  };
};

template <uint8_t TPin>
struct TimerHandlerHigh
{
  static void handler()
  {
    HandleSetOutputPinImpl(TPin, HIGH);
  };
};

static const HandlerTbl<NUM_DIGITAL_PINS, TimerHandlerLow> timerHandlerTblLow;
static const HandlerTbl<NUM_DIGITAL_PINS, TimerHandlerHigh> timerHandlerTblHigh;

template <uint8_t TPin>
struct IntHandlerNormal
{
  static void handler()
  {
    checkPinChangeAndDebounceInterruptHandler(TPin);
  }
};

template <uint8_t TPin>
struct IntHandlerIgnoreLevel
{
  static void handler()
  {

    checkPinChangeAndDebounceIgnoreLevelInterruptHandler(TPin);
  }
};

static const HandlerTbl<NUM_DIGITAL_PINS, IntHandlerNormal> intNormalHandlerTbl;
static const HandlerTbl<NUM_DIGITAL_PINS, IntHandlerIgnoreLevel> intIgnoreLevelHandlerTbl;

template <bool b>
struct timeoutHandler
{
  template <uint8_t TPin>
  struct timeoutHandlerNormal
  {
    static bool handler()
    {
      checkPinChangeAndDebounceInterruptTimeout(TPin);
    }
  };
  template <uint8_t TPin>
  using Normal = timeoutHandlerNormal<TPin>;

  template <uint8_t TPin>
  struct timeoutHandlerIgnoreLevel
  {
    static bool handler()
    {

      checkPinChangeAndDebounceIgnoreLevelInterruptTimeout(TPin);
    }
  };
  template <uint8_t TPin>
  using IgnoreLevel = timeoutHandlerIgnoreLevel<TPin>;
};

template <>
struct timeoutHandler<false>
{
  template <uint8_t TPin>
  struct timeoutHandlerNormal
  {
    static bool handler()
    {
      checkPinChangeAndDebounce(TPin);
    }
  };
  template <uint8_t TPin>
  using Normal = timeoutHandlerNormal<TPin>;

  template <uint8_t TPin>
  struct timeoutHandlerIgnoreLevel
  {
    static bool handler()
    {
      checkPinChangeAndDebounceIgnoreLevel(TPin);
    }
  };
  template <uint8_t TPin>
  using IgnoreLevel = timeoutHandlerIgnoreLevel<TPin>;
};

static const HandlerTbl<NUM_DIGITAL_PINS, timeoutHandler<useInterrupt>::Normal> timeoutNormalHandlerTbl;
static const HandlerTbl<NUM_DIGITAL_PINS, timeoutHandler<useInterrupt>::IgnoreLevel> timeoutIgnoreLevelHandlerTbl;

static const HandlerTbl<NUM_DIGITAL_PINS, timeoutHandler<useInterrupt>::Normal>::handler_type *const timeoutHandlerMap[] = {
    timeoutNormalHandlerTbl.func_array,
    timeoutIgnoreLevelHandlerTbl.func_array};


template <uint8_t T>
struct TimerArray
{
private:
  tsk::Task* tasks[T] = {NULL};

  tsk::Task& GetTask(uint8_t pin)
  {
    if(tasks[pin] != NULL)
    {
      return *tasks[pin];
    }

    tasks[pin] = new tsk::Task(0, TASK_ONCE, NULL,&runner,false);

    return *tasks[pin];
  }

public:

  TimerArray(){}
  ~TimerArray()
  {
    for(int i = 0; i < T; i++)
    {
      if(tasks[i])
      {
        runner.deleteTask(*tasks[i]);
        delete tasks[i];
        tasks[i] = NULL;
      }
    }
  }

  void clean()
  {
    for(int i = 0; i < T; i++)
    {
      if(tasks[i] && (config.pinCfg[i] == UNCONFIGURED || isInputPin(i)) )
      {
        runner.deleteTask(*tasks[i]);
        delete tasks[i];
        tasks[i] = NULL;
      }
    }
  }

  tsk::Task& operator[](uint8_t ndx) { return GetTask(ndx); };
};

TimerArray<NUM_DIGITAL_PINS> timerArray;

void HandleSetOutputPin(const struct IoDataFrame *data)
{
  HandleSetOutputPinImpl(data->pin, data->value);
}

void HandleSetOutputPinImpl(uint8_t pin, uint8_t value)
{

  if (isReservedPin(pin))
  {
    return;
  }

  if (!isConfigured(pin))
  {
    return;
  }

  if (isInputPin(pin))
  {
    return;
  }

  if (!isValidGPIOLevel(value))
  {
    return;
  }

  if (digitalRead(pin) == value)
  {
    return;
  }

  digitalWrite(pin, value);

  {
    ScopedDisableInterrupt interruptScope;
    notifyBuffer.push(GPIOValue({pin, value}));
  }
}

template <bool b>
void configureInterrupt(uint8_t pin)
{
  if (isInputPin(pin))
  {
    if (isNormalDebounce(pin))
    {
      attachInterrupt(digitalPinToInterrupt(pin), intNormalHandlerTbl[pin], CHANGE);
    }
    else
    {
      attachInterrupt(digitalPinToInterrupt(pin), intIgnoreLevelHandlerTbl[pin], CHANGE);
    }
  }
  else
  {
    attachInterrupt(digitalPinToInterrupt(pin), NULL, CHANGE);
  }
}

template <>
void configureInterrupt<false>(uint8_t)
{
}

void HandleSetPinMode(const IoDataFrame *data)
{
  if (isReservedPin(data->pin))
  {
    return;
  }

  if (!isValidGPIOMode(data->value))
  {
    return;
  }

  if (getPinConfig(data->pin) == data->value)
  {
    return;
  }

  {
    ScopedDisableInterrupt interruptScope;

    if (data->value == UNCONFIGURED)
    {
      pinMode(data->pin, INPUT);
    }
    else
    {
      pinMode(data->pin, data->value);
    }

    config.pinLastValue[data->pin] = digitalRead(data->pin);
    config.pinLastChange[data->pin] = 0;
    setPinConfig(data->pin, data->value);
    configureInterrupt<useInterrupt>(data->pin);
  }
}

void HandleSetInputPinDebounce(const IoDataFrame *data)
{
  if (isReservedPin(data->pin))
  {
    return;
  }

  {
    ScopedDisableInterrupt interruptScope;
    config.pinDebounceValueCfg[data->pin] = data->value;
    config.pinLastValue[data->pin] = digitalRead(data->pin);
    config.pinLastChange[data->pin] = 0;
  }
}

void HandleSetInputPinDebounceMode(const IoDataFrame *data)
{
  if (isReservedPin(data->pin))
  {
    return;
  }

  {
    ScopedDisableInterrupt interruptScope;
    config.pinDebounceModeCfg[data->pin] = data->value;
    config.pinLastValue[data->pin] = digitalRead(data->pin);
    config.pinLastChange[data->pin] = 0;
    configureInterrupt<useInterrupt>(data->pin);
  }
}

void HandleResetConfig(const IoDataFrame * /*data*/)
{
}

void HandleGetPinValue(const IoDataFrame *data)
{
  if (isReservedPin(data->pin))
  {
    return;
  }

  {
    ScopedDisableInterrupt interruptScope;
    notifyBuffer.push(GPIOValue({data->pin, getPinValueHelper(data->pin)}));
  }
}

void HandlePulseOutputPin(IoOutputPulseDataFrame *data)
{
  if (isReservedPin(data->pin))
  {
    return;
  }

  if (!isConfigured(data->pin))
  {
    return;
  }

  if (isInputPin(data->pin))
  {
    return;
  }

  if (!isValidGPIOLevel(data->value))
  {
    return;
  }

  tsk::Task &task = timerArray[data->pin];
  if (task.isEnabled())
  {
    return;
  }

  uint8_t initValue = LOW;
  tsk::TaskCallback cb = NULL;

  if (data->value == LOW)
  {
    initValue = HIGH;
    cb = timerHandlerTblHigh[data->pin];
  }
  else
  {
    initValue = LOW;
    cb = timerHandlerTblLow[data->pin];
  }

  if (digitalRead(data->pin) != initValue)
  {
    digitalWrite(data->pin, initValue);
  }

  HandleSetOutputPinImpl(data->pin, data->value);
  task.set(data->delay,TASK_ONCE,cb);
  task.restartDelayed();
}

template<typename T> void HandlerInvoker(void (*handler)(T*),  uint8_t *buff, uint8_t len)
{
  if(sizeof(T) != len)
  {
    return;
  }

  T *data = reinterpret_cast<T*>(buff);

  handler(data);
}

void slipReadCallback(uint8_t *buff, uint8_t len)
{
  if(len < sizeof(IoDataFrame))
  {
    return;
  }

  IoDataFrame *data = (IoDataFrame *)buff;

  switch (data->command)
  {
  case IoDataFrame::SET_OUTPUT_PIN_VALUE:
    HandlerInvoker(HandleSetOutputPin,buff,len);
    break;
  case IoDataFrame::SET_PIN_MODE:
    HandlerInvoker(HandleSetPinMode,buff,len);
    break;
  case IoDataFrame::GET_PIN_VALUE:
    HandlerInvoker(HandleGetPinValue,buff,len);
    break;
  case IoDataFrame::ERASE_CONFIG:
    HandlerInvoker(HandleResetConfig,buff,len);
    break;
  case IoDataFrame::SET_INPUT_PIN_DEBOUNCE:
    HandlerInvoker(HandleSetInputPinDebounce,buff,len);
    break;
  case IoDataFrame::PULSE_OUTPUT_PIN:
    HandlerInvoker(HandlePulseOutputPin,buff,len);
    break;
  case IoDataFrame::SET_INPUT_PIN_DEBOUNCE_MODE:
    HandlerInvoker(HandleSetInputPinDebounceMode,buff,len);
    break;
  default:
    break;
  }
}

bool checkPinChangeAndDebounceInterruptHandler(uint8_t pin)
{
  uint8_t tmp = digitalRead(pin);

  if (config.pinDebounceValueCfg[pin] == 0)
  {
    notifyBuffer.push(GPIOValue({pin, tmp}));
    config.pinLastValue[pin] = tmp;
    config.pinLastChange[pin] = 0;
    return true;
  }

  if (tmp == config.pinLastValue[pin])
  {
    config.pinLastChange[pin] = 0;
    return true;
  }

  if (config.pinLastChange[pin] < config.pinDebounceValueCfg[pin])
  {
    config.pinLastChange[pin] = 1;
    return true;
  }

  return false;
}

bool checkPinChangeAndDebounceInterruptTimeout(uint8_t pin)
{
  if (config.pinLastChange[pin] == 0)
  {
    return true;
  }

  if (config.pinLastChange[pin] < config.pinDebounceValueCfg[pin])
  {
    config.pinLastChange[pin]++;
    return true;
  }

  uint8_t tmp = digitalRead(pin);
  if (notifyBuffer.push(GPIOValue({pin, tmp})))
  {
    config.pinLastValue[pin] = tmp;
    config.pinLastChange[pin] = 0;
    return true;
  }

  return false;
}

bool checkPinChangeAndDebounceIgnoreLevelInterruptHandler(uint8_t pin)
{
  if (config.pinDebounceValueCfg[pin] == 0)
  {
    return true;
  }

  uint8_t tmp = digitalRead(pin);

  if (config.pinLastChange[pin] == 0)
  {
    if (tmp != config.pinLastValue[pin])
    {
      if (notifyBuffer.push(GPIOValue({pin, HIGH})))
      {
        config.pinLastValue[pin] = tmp;
        config.pinLastChange[pin]++;
        return true;
      }
    }
  }
  else
  {
    config.pinLastChange[pin] = 1;
    config.pinLastValue[pin] = tmp;
  }
  return false;
}

bool checkPinChangeAndDebounceIgnoreLevelInterruptTimeout(uint8_t pin)
{
  if (config.pinLastChange[pin] == 0)
  {
    return true;
  }

  if (config.pinLastChange[pin] < config.pinDebounceValueCfg[pin])
  {
    config.pinLastChange[pin]++;
    return true;
  }

  if (notifyBuffer.push(GPIOValue({pin, LOW})))
  {
    config.pinLastValue[pin] = digitalRead(pin);
    config.pinLastChange[pin] = 0;
    return true;
  }

  return false;
}

bool checkPinChangeAndDebounce(uint8_t pin)
{
  uint8_t tmp = digitalRead(pin);
  if (tmp == config.pinLastValue[pin])
  {
    config.pinLastChange[pin] = 0;
    return true;
  }

  if (config.pinLastChange[pin] < config.pinDebounceValueCfg[pin])
  {
    config.pinLastChange[pin]++;
    return true;
  }

  if (notifyBuffer.push(GPIOValue({pin, tmp})))
  {
    config.pinLastValue[pin] = tmp;
    config.pinLastChange[pin] = 0;
    return true;
  }

  return false;
}

bool checkPinChangeAndDebounceIgnoreLevel(uint8_t pin)
{
  uint8_t tmp = digitalRead(pin);

  if (config.pinLastChange[pin] == 0)
  {
    if (tmp != config.pinLastValue[pin])
    {
      if (notifyBuffer.push(GPIOValue({pin, HIGH})))
      {
        config.pinLastValue[pin] = tmp;
        config.pinLastChange[pin]++;
        return true;
      }
    }
  }
  else
  {
    if (tmp != config.pinLastValue[pin])
    {
      config.pinLastChange[pin] = 1;
      config.pinLastValue[pin] = tmp;
    }
    else
    {
      config.pinLastChange[pin]++;
      if (config.pinLastChange[pin] >= config.pinDebounceValueCfg[pin])
      {
        if (notifyBuffer.push(GPIOValue({pin, LOW})))
        {
          config.pinLastValue[pin] = tmp;
          config.pinLastChange[pin] = 0;
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
void taskReportPinStart();

tsk::Task t1(INPUT_POLL_INTERVAL, TASK_FOREVER, &taskReadInputPin,&runner,false);
tsk::Task t2(NOTIFY_PIN_STATUS_INTERVAL, TASK_FOREVER, &taskNotifyIOChange,&runner,false);
tsk::Task t3(0, TASK_FOREVER, &taskProcessSlip,&runner,false);
tsk::Task t4(REQUEST_CONFIG_INTERVAL_PER_PIN, NUM_DIGITAL_PINS * 3, &taskStartUp,&runner,false);
tsk::Task t5(INPUT_REPORT_INTERVAL, TASK_FOREVER, &taskReportPinStart,&runner,false);
tsk::Task t6(INPUT_REPORT_SPLIT_INTERVAL, NUM_DIGITAL_PINS, &taskReportPin,&runner,false);

void taskReadInputPin()
{
  for (uint8_t j = 0; j < NUM_DIGITAL_PINS; j++)
  {
    if (isReservedPin(j))
    {
      continue;
    }

    if (!isInputPin(j))
    {
      continue;
    }

    if (!timeoutHandlerMap[!isNormalDebounce(j)][j]())
    {
      break;
    }
  }
}

void taskNotifyIOChange()
{
  GPIOValue value;
  {
    ScopedDisableInterrupt interruptScope;
    if (!notifyBuffer.pop(value))
    {
      return;
    }
  }

  delayMicroseconds(100);
  IoDataFrame data({IoDataFrame::REPORT_PIN_CURRENT_VALUE, value});
  slip.sendpacket((uint8_t *)&data, sizeof(data));
  config.pinLastValueReported[value.pin] = 0;
}

void taskReportPinStart()
{
  timerArray.clean();
  t6.restart();
}

void taskReportPin()
{
  uint8_t ndx = (uint8_t) t6.getIterations();

  if (isReservedPin(ndx))
  {
    return;
  }

  if (!isConfigured(ndx))
  {
    return;
  }

  if (config.pinLastValueReported[ndx] < 60)
  {
    config.pinLastValueReported[ndx]++;
    return;
  }

  uint8_t value = LOW;
  {
    ScopedDisableInterrupt interruptScope;
    value = getPinValueHelper(ndx);
    config.pinLastValueReported[ndx] = 0;
  }

  IoDataFrame data({IoDataFrame::REPORT_PIN_CURRENT_VALUE, GPIOValue(ndx, value)});
  slip.sendpacket((uint8_t *)&data, sizeof(data));
}

void taskProcessSlip()
{
  slip.proc();
}

void taskStartUp()
{
  
  uint8_t ndx = (uint8_t) (t4.getIterations() % NUM_DIGITAL_PINS);
  if (isConfigured(ndx))
  {
    return;
  }

  IoDataFrame data({IoDataFrame::REQUEST_CONFIGURATION, ndx, 0});
  slip.sendpacket((uint8_t *)&data, sizeof(data));
}

void setup()
{
  //Serial.begin(115200);

  Serial1.begin(9600);
  slip.setCallback(slipReadCallback);

  config.begin();

  t1.enable();
  t2.enable();
  t3.enable();
  t4.enableDelayed(REQUEST_CONFIG_DELAY);
  t5.enable();
}

// the loop function runs over and over again forever
void loop()
{
  runner.execute();
}