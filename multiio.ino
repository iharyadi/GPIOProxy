#include <RingBuf.h>

namespace tsk
{
#include <TaskScheduler.h>
#include <TaskSchedulerDeclarations.h>
#include <TaskSchedulerSleepMethods.h>
}

#include <slip.h>

#if defined(ARDUINO_AVR_LARDU_328E)
#include <SoftwareSerial.h>
#endif

#define UNCONFIGURED 0xFF

#define INPUT_POLL_INTERVAL 1
#define INPUT_REPORT_INTERVAL 5000
#define REQUEST_CONFIG_INTERVAL_PER_PIN 350
#define REQUEST_CONFIG_DELAY 10000

#define DEFAULT_INPUT_DEBOUNCE 3

#define DEBOUNCE_NORMAL 0
#define DEBOUNCE_IGNORE_LEVEL 1
#define DEFAULT_DEBOUNCE_MODE DEBOUNCE_NORMAL

#define MAX_QUEUE_SIZE 10

#if defined(ARDUINO_SAM_DUE)
constexpr bool useInterrupt = true;
#define VOLATILE volatile
#else
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

static VOLATILE uint8_t pinCfg[NUM_DIGITAL_PINS];
static VOLATILE uint8_t pinDebounceValueCfg[NUM_DIGITAL_PINS];
static VOLATILE uint8_t pinDebounceModeCfg[NUM_DIGITAL_PINS];
static VOLATILE uint8_t pinLastChange[NUM_DIGITAL_PINS];
static VOLATILE uint8_t pinLastValue[NUM_DIGITAL_PINS];
static uint8_t pinLastValueReported[NUM_DIGITAL_PINS];

void HandleSetOutputPinImpl(uint8_t pin, uint8_t level);
bool checkPinChangeAndDebounceIgnoreLevelInterruptHandler(uint8_t pin);
bool checkPinChangeAndDebounceIgnoreLevelInterruptTimeout(uint8_t pin);
bool checkPinChangeAndDebounceInterruptHandler(uint8_t pin);
bool checkPinChangeAndDebounceInterruptTimeout(uint8_t pin);

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
  return pin == 0 || pin == 1 || pin == 4 || pin == 5 || pin >= NUM_DIGITAL_PINS;
#else
  return pin == 0 || pin == 1 || pin == 18 || pin == 19 || pin >= NUM_DIGITAL_PINS;
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
  return pinDebounceModeCfg[pin] == DEBOUNCE_NORMAL;
}

uint8_t inline getPinValueHelper(uint8_t pin)
{
  return (isInputPin(pin) && !isNormalDebounce(pin)) ? (pinLastChange[pin] == 0) ? LOW : HIGH : digitalRead(pin);
}

#if defined(ARDUINO_AVR_LARDU_328E)
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

template <uint8_t N, uint8_t T>
struct TimerArray : TimerArray<N - 1, T>
{
  tsk::Task task = {0, TASK_ONCE, timerHandlerTblHigh[N - 1], &runner, false, NULL, NULL};
  TimerArray()
  {
    TimerArray<0, T>::tasks[N - 1] = &task;
  }
};

template <uint8_t T>
struct TimerArray<0, T>
{
  tsk::Task *tasks[T];

  tsk::Task &operator[](uint8_t ndx) { return *tasks[ndx]; };
};

TimerArray<NUM_DIGITAL_PINS, NUM_DIGITAL_PINS> timerArray;

void initializeIOConfig()
{
  memset(const_cast<uint8_t *>(pinLastChange), HIGH, sizeof(pinLastChange));
  memset(const_cast<uint8_t *>(pinDebounceValueCfg), DEFAULT_INPUT_DEBOUNCE, sizeof(pinDebounceValueCfg));
  memset(const_cast<uint8_t *>(pinDebounceModeCfg), DEFAULT_DEBOUNCE_MODE, sizeof(pinDebounceModeCfg));
  memset(const_cast<uint8_t *>(pinCfg), UNCONFIGURED, sizeof(pinCfg));
  memset(const_cast<uint8_t *>(pinLastValueReported), 0, sizeof(pinLastValueReported));

  for (uint8_t j = 0; j < NUM_DIGITAL_PINS; j++)
  {
    if (isReservedPin(j))
    {
      continue;
    }
    pinMode(j, INPUT);
  }
}

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

    pinLastValue[data->pin] = digitalRead(data->pin);
    pinLastChange[data->pin] = 0;
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
    pinDebounceValueCfg[data->pin] = data->value;
    pinLastValue[data->pin] = digitalRead(data->pin);
    pinLastChange[data->pin] = 0;
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
    pinDebounceModeCfg[data->pin] = data->value;
    pinLastValue[data->pin] = digitalRead(data->pin);
    pinLastChange[data->pin] = 0;
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

void HandlePulseOutputPin(const IoDataFrame *data)
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

  const IoOutputPulseDataFrame *pulseData = reinterpret_cast<const IoOutputPulseDataFrame *>(data);

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
  task.set(pulseData->delay,TASK_ONCE,cb);
  task.restartDelayed();
}

void slipReadCallback(uint8_t *buff, uint8_t len)
{
  IoDataFrame *data = (IoDataFrame *)buff;

  switch (data->command)
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

bool checkPinChangeAndDebounceInterruptHandler(uint8_t pin)
{
  uint8_t tmp = digitalRead(pin);

  if (pinDebounceValueCfg[pin] == 0)
  {
    notifyBuffer.push(GPIOValue({pin, tmp}));
    pinLastValue[pin] = tmp;
    pinLastChange[pin] = 0;
    return true;
  }

  if (tmp == pinLastValue[pin])
  {
    pinLastChange[pin] = 0;
    return true;
  }

  if (pinLastChange[pin] < pinDebounceValueCfg[pin])
  {
    pinLastChange[pin] = 1;
    return true;
  }

  return false;
}

bool checkPinChangeAndDebounceInterruptTimeout(uint8_t pin)
{
  if (pinLastChange[pin] == 0)
  {
    return true;
  }

  if (pinLastChange[pin] < pinDebounceValueCfg[pin])
  {
    pinLastChange[pin]++;
    return true;
  }

  uint8_t tmp = digitalRead(pin);
  if (notifyBuffer.push(GPIOValue({pin, tmp})))
  {
    pinLastValue[pin] = tmp;
    pinLastChange[pin] = 0;
    return true;
  }

  return false;
}

bool checkPinChangeAndDebounceIgnoreLevelInterruptHandler(uint8_t pin)
{
  if (pinDebounceValueCfg[pin] == 0)
  {
    return true;
  }

  uint8_t tmp = digitalRead(pin);

  if (pinLastChange[pin] == 0)
  {
    if (tmp != pinLastValue[pin])
    {
      if (notifyBuffer.push(GPIOValue({pin, HIGH})))
      {
        pinLastValue[pin] = tmp;
        pinLastChange[pin]++;
        return true;
      }
    }
  }
  else
  {
    pinLastChange[pin] = 1;
    pinLastValue[pin] = tmp;
  }
  return false;
}

bool checkPinChangeAndDebounceIgnoreLevelInterruptTimeout(uint8_t pin)
{
  if (pinLastChange[pin] == 0)
  {
    return true;
  }

  if (pinLastChange[pin] < pinDebounceValueCfg[pin])
  {
    pinLastChange[pin]++;
    return true;
  }

  if (notifyBuffer.push(GPIOValue({pin, LOW})))
  {
    pinLastValue[pin] = digitalRead(pin);
    pinLastChange[pin] = 0;
    return true;
  }

  return false;
}

bool checkPinChangeAndDebounce(uint8_t pin)
{
  uint8_t tmp = digitalRead(pin);
  if (tmp == pinLastValue[pin])
  {
    pinLastChange[pin] = 0;
    return true;
  }

  if (pinLastChange[pin] < pinDebounceValueCfg[pin])
  {
    pinLastChange[pin]++;
    return true;
  }

  if (notifyBuffer.push(GPIOValue({pin, tmp})))
  {
    pinLastValue[pin] = tmp;
    pinLastChange[pin] = 0;
    return true;
  }

  return false;
}

bool checkPinChangeAndDebounceIgnoreLevel(uint8_t pin)
{
  uint8_t tmp = digitalRead(pin);

  if (pinLastChange[pin] == 0)
  {
    if (tmp != pinLastValue[pin])
    {
      if (notifyBuffer.push(GPIOValue({pin, HIGH})))
      {
        pinLastValue[pin] = tmp;
        pinLastChange[pin]++;
        return true;
      }
    }
  }
  else
  {
    if (tmp != pinLastValue[pin])
    {
      pinLastChange[pin] = 1;
      pinLastValue[pin] = tmp;
    }
    else
    {
      pinLastChange[pin]++;
      if (pinLastChange[pin] >= pinDebounceValueCfg[pin])
      {
        if (notifyBuffer.push(GPIOValue({pin, LOW})))
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
void taskReportPinStart();

tsk::Task t1(INPUT_POLL_INTERVAL, TASK_FOREVER, &taskReadInputPin);
tsk::Task t2(1, TASK_FOREVER, &taskNotifyIOChange);
tsk::Task t3(5, TASK_FOREVER, &taskProcessSlip);
tsk::Task t4(REQUEST_CONFIG_INTERVAL_PER_PIN, NUM_DIGITAL_PINS * 3, &taskStartUp);
tsk::Task t5(INPUT_REPORT_INTERVAL, TASK_FOREVER, &taskReportPinStart);
tsk::Task t6(2, NUM_DIGITAL_PINS, &taskReportPin);

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
  pinLastValueReported[value.pin] = 0;
}

void taskReportPinStart()
{
  t6.restart();
}

void taskReportPin()
{
  //static uint8_t i = 0;
  //uint8_t ndx = i++ % NUM_DIGITAL_PINS;
  uint8_t ndx = (uint8_t) t6.getIterations();

  if (isReservedPin(ndx))
  {
    return;
  }

  if (!isConfigured(ndx))
  {
    return;
  }

  if (pinLastValueReported[ndx] < 60)
  {
    pinLastValueReported[ndx]++;
    return;
  }

  uint8_t value = LOW;
  {
    ScopedDisableInterrupt interruptScope;
    value = getPinValueHelper(ndx);
    pinLastValueReported[ndx] = 0;
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
  static uint8_t j = 0;
  uint8_t ndx = j++ % NUM_DIGITAL_PINS;
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

  initializeIOConfig();

  runner.addTask(t1);
  runner.addTask(t2);
  runner.addTask(t3);
  runner.addTask(t4);
  runner.addTask(t5);
  runner.addTask(t6);
  t1.enable();
  t2.enable();
  t3.enable();
  t4.enable();
  t4.delay(REQUEST_CONFIG_DELAY);
  t5.enable();
}

// the loop function runs over and over again forever
void loop()
{
  runner.execute();
}