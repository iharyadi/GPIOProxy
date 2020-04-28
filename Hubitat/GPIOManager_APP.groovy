definition(
    name: "GPIO Manager",
    namespace: "iharyadi",
    author: "Iman Haryadi",
    description: "Manage GPIO with DTH association",
    category: "Convenience",
    iconUrl: "",
    iconX2Url: "")

preferences {
	page(name: "mainPage")
}

private int MAX_PIN()
{
    return 70    
}

private boolean isReservedPin(int pin)
{
  if(pin == 0 || pin == 1 || pin == 18 || pin == 19)
  {
    return true;
  }

  return false;
}

def pinList()
{
    def pins = []
    for (int i = 0 ; i < MAX_PIN(); i ++)
    {
        if(isReservedPin(i))
        {
            continue
        }
        
        pins += i;
    }
    return pins
}

def unusedOption()
{
    return "unused"
}

def availableDriver()
{
    return [unusedOption() , "Contact" , "Motion", "ShockSensor", "Tone", "Relay"]
}

def mainPage() {
	dynamicPage(name: "mainPage", title: " ", install: true, uninstall: true) {
		section {
			input "thisName", "text", title: "Name this GPIO Manager", submitOnChange: true
			if(thisName) app.updateLabel("$thisName")
			input "envSensor", "capability.temperatureMeasurement", title: "Select Environment Sensors", submitOnChange: true, required: true, multiple: false
			paragraph "Enter pin configuration"
			pinList().each {
                input(
                    name: "pinDTH${it}",
                    type: "enum",
                    title: "DTH Pin$it",
                    required: false,
                    multiple: false,
                    options: availableDriver(),
                    defaultValue: unusedOption(),
                    submitOnChange: true
                )
                
                if (settings."pinDTH${it}" != null && settings."pinDTH${it}" != "unused") {
                  def label = settings."pinDTH${it}"
                  input(
                      name: "deviceLabel${it}",
                      type: "text",
                      title: "${label} pin${it} device name",
                      description: "Name the device connected to ${label}",
                      required: (settings."pinDTH${it}" != null && settings."pinDTH${it}" != "unused"),
                      defaultValue: "${label}_${it}"
                  )
                }
			}
		}
	}
}

def installed() {
    updateChildHandler()
}

def updateChildHandler()
{
    if(!envSensor)
    {
        return
    }
    
    log.info "settings ${settings}"
    settings.each {
        
        log.info "it ${it}"
   
    }
    
    pinList().each { it->
        if (settings."pinDTH${it}" == null || settings."pinDTH${it}" == "unused") {
            return
        }
        envSensor.createGPIOChild(settings."pinDTH${it}",it, settings."deviceLabel${it}")
    }
}

def updated() {
	updateChildHandler()
}
