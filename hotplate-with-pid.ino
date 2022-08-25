/**
  Hotplate with PID

  - App design -

  This is a strict stepped time Arduino app. No inline delays happen, each cycle aims to last a bit more than 200 ms before looping over.
  I/O polling is handled in its own control functions

 */

/*
   How to use without a rotary encoder:
   Long presses will toggle from one state to the other
   (passive to active, active to stopping)
   Short presses will switch from one profile to the next

   How to use with a rotary encoder:
   Press+Rotate to change LCD contrast
   Rotate to change profile (when in passive mode)
   Rotate to display another data panel (when in active mode)
   Double-press to start/stop the reflow ops
   Press 5 times to save settings to EEPROM (LCD contrast typically)

 */
#include <max6675.h>
#include <LiquidCrystal.h>
#include <EncoderButton.h>
#include <ArduPID.h>
#include <EEPROM.h>
#include "hotplate_types.h"
#include "hotplate_wiring.h"
#include <stdarg.h>
// reset function
void(* resetFunc) (void) = 0;
// types for defining the data

// controlled devices
MAX6675 thermocouple(TH_SCK, TH_CS, TH_SO);
LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
EncoderButton button(RE_P1, RE_P2, RE_B);

AppState appState;
ArduPID pidController;
const ReflowProfile profiles[PROFILES_NUM] = {
    {
        {.name = "S63P37"},
        .steps = {
            {
                .temperature = 150,
                .seconds = 60,
                .state = PREHEAT,
            },
            {
                .temperature = 165,
                .seconds = 150,
                .state = SOAK,
            },
            {
                .temperature = 235,
                .seconds = 60,
                .state = RAMP_UP,
            },
            {
                .temperature = 235,
                .seconds = 20,
                .state = REFLOW,
            },
        }
    },
    {
        {.name = "SAC305"},
        .steps = {
            {
                .temperature = 150,
                .seconds = 60,
                .state = PREHEAT,
            },
            {
                .temperature = 165,
                .seconds = 150,
                .state = SOAK,
            },
            {
                .temperature = 250,
                .seconds = 60,
                .state = RAMP_UP,
            },
            {
                .temperature = 250,
                .seconds = 20,
                .state = REFLOW,
            },
        }
    },
    {
        {.name = "TBD"},
        .steps = {
            {
                .temperature = 150,
                .seconds = 60,
                .state = PREHEAT,
            },
            {
                .temperature = 165,
                .seconds = 200,
                .state = SOAK,
            },
            {
                .temperature = 230,
                .seconds = 60,
                .state = RAMP_UP,
            },
            {
                .temperature = 230,
                .seconds = 20,
                .state = REFLOW,
            },
        }
    },
};
// code

void dumpReflowStep(const String prefix, int stepNumber, ReflowStep s) {
    Serial.print(prefix);
    Serial.print(F("Step #"));
    Serial.print(stepNumber);
    Serial.print(F(" ["));
    Serial.print(s.state);
    Serial.print(F("] Time: "));
    Serial.print(s.seconds);
    Serial.print(F(" seconds. Temperature: "));
    Serial.print(s.temperature);
    Serial.println(F("°C"));
}
// dump a reflow profile to the serial console
void dumpReflowProfile(ReflowProfile p) {
    int i;
    Serial.print("# ReflowProfile ");
    Serial.println(p.name);
    Serial.println("## Steps:");
    for (i = 0; i < 4; i++) {
        dumpReflowStep("###", i + 1, p.steps[i]);
    }
    Serial.print("# End ReflowProfile ");
    Serial.println(p.name);
    Serial.println("# --");
}

void initSerial() {
    Serial.begin(9600);
}

void initPins() {
    // relay
    pinMode(REL_P, OUTPUT);
    digitalWrite(REL_P, LOW);
    

    // we force the LCD to write mode
    pinMode(LCD_RW, OUTPUT);
    digitalWrite(LCD_RW, LOW);
#ifdef PWM_CONTRAST
    pinMode(LCD_VO, OUTPUT);
#endif
    /*
    // these pins for contrast selector, we should not do that this way
    pinMode(BL_P, OUTPUT);
    pinMode(BL_M, OUTPUT);
    digitalWrite(BL_P, HIGH);
    digitalWrite(BL_M, LOW);
     */
}

void initLCD() {
    lcd.begin(16, 2);
}

void initState() {
    appState.ActiveProfileName = malloc(sizeof(char) * 16);
    appState.ActiveStepName = malloc(sizeof(char) * 16);
    snprintf(appState.ActiveProfileName, 1, "");;
    snprintf(appState.ActiveStepName, 1, "");;
    appState.ButtonsPressedMask = 0;
    appState.ActiveProfileNumber = 1;
    appState.cycleTime = 0;
    appState.SystemUptime = 0;
    appState.LCDContrast = 127;
    appState.verbosity = 0;
    appState.helpStepper = 0;
    appState.State = IDLE;
    appState.StepFinishTime = 0;

    adjustOutputLimits(PID_WINDOW);

    // the PID controller will issue turn-on orders with a maximum of PID_WINDOW
    pidController.begin(&appState.PidControl.Measured, &appState.PidControl.OutputDuration, &appState.DesiredTemperature, 1, 0, 0);
    pidController.setOutputLimits(0, appState.WindowSize);
    pidController.start();
//    pidController.setMode(AUTOMATIC);
    loadActiveProfile(appState.ActiveProfileNumber, 0);
}

void adjustOutputLimits(int newOutputLimit) {
    if (newOutputLimit < TIME_INCREMENT || newOutputLimit > PID_WINDOW_MAX) {
        Serial.println(F("-- cannot adjust output limits below TIME_INCREMENT or above PID_WINDOW_MAX"));
        return;
    }
    appState.WindowSize = newOutputLimit;
}

void toggleReflow() {
    if (appState.State == IDLE) {
        startReflow();
    } else if (appState.State == COOLING) {
        lcdInfoLineN(1, F("Cooling, cannot toggle reflow"));
    } else {
        endReflow();
    }
}

void startReflow() {
    Serial.println(F("-- starting reflow --"));
    appState.State = IN_PROCESS;
    appState.ticks = 0;
    lcdInfoLineN(1, F("Starting reflow"));
}

void endReflow() {
    appState.State = IDLE;
    appState.DesiredTemperature = 0;
    appState.StepFinishTime = 0;
    lcdInfoLineN(1, F("Interrupt Reflow"));
}
void debug() {
    //Serial.print("Cycle time ");
    //Serial.println(appState.cycleTime);
#ifdef NO_USE_ROTARY
    Serial.print("BPM ");
    Serial.println(appState.ButtonsPressedMask);
#endif
    //char uptime[8];
    //snprintf(uptime, 8, "%d", appState.SystemUptime / 1000);
    //lcdDebugStr(1, "U:", uptime);
    
}

// display temperatures on LCD
//
//  0______________F      
// |123.5°C> 097.3°C| 0
// |pre-heating     | 1
//  ————————————————
//  0______________F
// |123.5°C< 137.3°C| 0
// |overshoot       | 1
//  ————————————————
//  0______________F
// |123.5°C~ 123.9°C| 0
// |sweet spot      | 1
//  ————————————————
void displayTemp() {
    // 7 characters for number 1 
    char l[3];
    snprintf(l, 3, "%03d", (int)appState.PidControl.Measured);

    lcd.setCursor(0, 1);
    lcd.print(l);
    // unit sign
    lcd.setCursor(6, 1);
    lcd.print((char)223);
    lcd.print("C");
    lcd.setCursor(10, 1);
    snprintf(l, 3, "%03d", (int)appState.DesiredTemperature);
    lcd.print(l);
    lcd.print((char)223);
    lcd.print("C");
    lcd.setCursor(8, 1);
    if (abs(appState.DesiredTemperature - appState.PidControl.Measured) < SWEET_SPOT) {
        lcd.print("~ ");
    } else if (appState.DesiredTemperature > appState.PidControl.Measured) {
        lcd.print("< ");
    } else {
        lcd.print("> ");
    }
}

void probeTemp() {
    appState.PidControl.Measured = thermocouple.readCelsius();
}

void setup() {
    // initialize serial port. we will use serial plotter
    initSerial();
    initPins();
    initLCD();
#ifndef NO_USE_ROTARY
    initRotaryInput();
#endif

    initState();
    loadEeprom();
    appState.toCallback = appState.toCallback | 1;
    // debug();
}

void saveEeprom() {
    byte eeprom_format = EEPROM_FORMAT;
    int eepromAddress = EEPROM_START_ADDRESS;
    EEPROM.update(0, eeprom_format);

    EEPROM.update(eepromAddress, appState.LCDContrast);
    Serial.print("done saving eeprom: LCDContrast = ");
    Serial.println(appState.LCDContrast);
}

void loadEeprom() {
    byte format;
    EEPROM.get(0, format);
    if (format != EEPROM_FORMAT) {
        Serial.print("--Format marker in EEPROM not good: ");
        Serial.println(format);
        Serial.print("--Expected ");
        Serial.println(EEPROM_FORMAT);
        return;
    }
    _loadEeprom();
    Serial.println("--- loaded state");
    Serial.print("LCDContrast");
    Serial.println(appState.LCDContrast);
}

void _loadEeprom() {
    byte lcdContrast;
    int eepromAddress = EEPROM_START_ADDRESS;

    EEPROM.get(eepromAddress, lcdContrast);
    // 
    appState.LCDContrast = lcdContrast;
}

#ifndef NO_USE_ROTARY
/*

   How to use with a rotary encoder:
   Press+Rotate to change LCD contrast
   Rotate to change profile (when in passive mode)
   Rotate to display another data panel (when in active mode)
   Double-press to start/stop the reflow ops
   Single-press to show info 3
   Press 5 times to save settings to EEPROM (LCD contrast typically)
 */
void initRotaryInput() {
    Serial.println(F("--- init rotary input"));
    button.setClickHandler(onButtonClicked);
    button.setEncoderHandler(onButtonRotated);
    button.setEncoderPressedHandler(onButtonPressedAndRotated);
    button.setIdleTimeout(1000);
    button.setRateLimit(100);
    button.setMultiClickInterval(300);
}

#ifdef PWM_CONTRAST
void onButtonPressedAndRotated(EncoderButton& btn) {
    char l[16];
    Serial.println(F("--- onButtonPressedAndRotated"));
    int newContrast = appState.LCDContrast + btn.increment();
    if (newContrast < 3) {
        newContrast = 3;
    } else if (newContrast > 120) {
        newContrast = 120;
    }
    appState.LCDContrast = newContrast;
    appState.toCallback = appState.toCallback | 1;
    Serial.print(F("-- Rotary: contrast: "));
    Serial.println(appState.LCDContrast);
    if (appState.State != IN_PROCESS) {
        return;
    }
    snprintf(l, 16, "Contrast: %d", appState.LCDContrast);
    lcdInfoLineN(1, l);

}
#else
void onButtonPressedAndRotated(EncoderButton& btn) {
    return;
}
#endif

void onButtonClicked(EncoderButton& btn) {
    Serial.print(F("--- onButtonClicked:"));
    int clickCount = btn.clickCount();
    Serial.println(clickCount);
    if (clickCount >= 8) {
        testMode();
    }
    else if (clickCount >= 5) {
        saveEeprom();
    } else if (clickCount >= 2) {
        toggleReflow();
    } else {
//        togglePanelInfo();
      showHelp();
      appState.helpStepper += 1;
      appState.helpStepper %= 4;
    }
}

void showHelp() {
    switch(appState.helpStepper) {
        case 0:
            lcdInfoLineN(1, "Knob: prof sel.");
            lcdInfoLineN(2, "dbl-click: start");
            break;
        case 1:
            lcdInfoLineN(1, "Prof 1: Leaded");
            lcdInfoLineN(2, "...");
            break;
        case 2:
            lcdInfoLineN(1, "Prof 2: LeadFree");
            lcdInfoLineN(2, "...");
            break;
        case 3:
            lcdInfoLineN(1, "Prof 3: tbd");
            lcdInfoLineN(2, "...");
            break;
    }
}

void togglePanelInfo() {
    /** Panel 1
     *  c: NNN   P: #
     *  s: NNN   S: #
showing: 
c: current temperature  P: current profile
s:  target temperature  S: current step
     **/
    char l[16];
    snprintf(l, 16, "c:%03d  P: %d", (int)appState.PidControl.Measured, appState.ActiveProfileNumber);
    Serial.println(l);
    lcdInfoLineN(1, l);
    snprintf(l, 16, "s:%03d  S: %d", (int)appState.DesiredTemperature, appState.ActiveStepNumber);
    Serial.println(l);
    lcdInfoLineN(2, l);

}
void onButtonRotated(EncoderButton& btn) {
    int increment = btn.increment();
    int sign = increment > 0 ? 1 : -1;
    Serial.println("Rotated");
    char l[16];
    if (appState.State == IDLE) {
        goToNextProfile(sign);
    } else {
        goToNextPanel(sign);
    }
}
#endif

void updateAppState() {
    // set LastState to the current value of State, to detect state transitions
    appState.LastState = appState.State;
}

void setRelay(int state) {
    if (state != HIGH && state != LOW) {
        Serial.println(F("-- invalid relay state --"));
        return;
    }
    if (appState.verbosity > 0xf0) {
        Serial.print("-- switch relay ");
        Serial.println(state);
    }
    digitalWrite(REL_P, state);
}

void actOnPID() {
  pidController.debug(&Serial, "pidController",
   PRINT_INPUT   | 
   PRINT_OUTPUT   |
   PRINT_SETPOINT |
   PRINT_BIAS     |
   PRINT_P        |
   PRINT_I        |
   PRINT_D);
  

    if (appState.SystemUptime - appState.EvaluationWindowStart > appState.WindowSize) {
        // we are in a new evaluation window, let's slide it
        appState.EvaluationWindowStart += appState.WindowSize;
    }
    // now let's see what PID tells us to do
    if (appState.PidControl.OutputDuration > appState.SystemUptime - appState.EvaluationWindowStart) {
        setRelay(1);
    } else {
        setRelay(0);
    }
}

#ifdef NO_USE_ROTARY
void handleInput() {
    // collect button state and update button pressed mask
    // each step that passes, we shift left the ButtonsPressedMask byte. As long as the button is pressed
    // it will double its value. When the button is released it will end with a 0
    appState.ButtonsPressedMask = (appState.ButtonsPressedMask << 1) + (BUTTON_IS_PUSHED ?  1 : 0);
}
#else
void handleInput() {
    button.update();
}
#endif

void lcdInfoLineN(int n, const char *text) {
    lcd.setCursor(0, n - 1);
    lcd.print(BLANK_LCD_LINE);
    lcd.setCursor(0, n - 1);
    lcd.print(text);
}

void lcdInfoLineN(int n, const __FlashStringHelper *text) {
    lcd.setCursor(0, n - 1);
    lcd.print(BLANK_LCD_LINE);
    lcd.setCursor(0, n - 1);
    lcd.print(text);
}

void loadActiveProfile(unsigned int profileNumber, unsigned int stepNumber) {
    unsigned int stepsCount;
    const ReflowProfile *profile;
    const ReflowStep * step;
    if (profileNumber < 1 || profileNumber > PROFILES_NUM) {
        return;
    }
    if (stepNumber < 0) {
        return;
    }
    // set numbers in appState
    appState.ActiveProfileNumber = profileNumber;
    appState.ActiveStepNumber = stepNumber;
    // load informational data
    profile = &profiles[profileNumber-1];
    strncpy(appState.ActiveProfileName, profile->name, 16);
    lcdInfoLineN(1, "Profile:");
    lcd.setCursor(8, 0);
    lcd.print(appState.ActiveProfileName);

    stepsCount = *(&(profile->steps) + 1) - (profile->steps);

    
    if (stepNumber == 0) {
        snprintf(appState.ActiveStepName, 7, "WAITING");
        Serial.println("Waiting");
    } else if (stepNumber > stepsCount){
        snprintf(appState.ActiveStepName, 4, "DONE");
        // end the reflow
        endReflow();
        lcdInfoLineN(1, F("Finish Reflow"));
        return;
    } else {
        step = &(profile->steps[stepNumber -1]);
        strncpy(appState.ActiveStepName, ReflowStepsNames[stepNumber - 1], 16);
        Serial.println(ReflowStepsNames[0]);
        Serial.println(ReflowStepsNames[1]);
        Serial.println(ReflowStepsNames[2]);
        Serial.println(ReflowStepsNames[3]);
        Serial.println(stepNumber - 1);
        Serial.println(ReflowStepsNames[stepNumber - 1]);
        // load wanted duration and temperature
        appState.DesiredTemperature = step->temperature;
        appState.StepFinishTime = (step->seconds) * 1000UL + appState.SystemUptime;
        Serial.print("Active step:"); Serial.println(appState.ActiveStepName);
        Serial.print("Step: seconds:");Serial.println(step->seconds);
        Serial.print("T:"); Serial.println(step->temperature);
        Serial.print("Uptime:"); Serial.println(appState.SystemUptime);
        Serial.print("Step finish time:"); Serial.println(appState.StepFinishTime);
    }
    Serial.print("Steps: "); Serial.print(stepNumber); Serial.print("/"); Serial.println(stepsCount);
}

#ifdef NO_USE_ROTARY
// TODO: rewrite using EncoderButton-style handling
void updateSystemInputState() {
    // in case we pressed the button for 3200 ms, we will reset
    if (appState.ButtonsPressedMask ^ 0xffff == 0) {
        Serial.println(F("button reset"));
        resetFunc();
    }
    // in IDLE and cooling modes we want to avoid IN_PROCESS, so desired temperature should be 0
    // note: in freezing environments this may triggevoid(* resetFunc) (void) = 0; r the relay
    if (appState.State == IDLE || appState.State == COOLING) {
        appState.DesiredTemperature = 0;
    }
    // pressed for 10 periods = 2 seconds
    if (appState.State == IN_PROCESS) {
        if (appState.ButtonsPressedMask ^ ((1 << 10) - 1) == 0) {
            endReflow();
        }
    } else if (appState.State == COOLING) {
        lcdInfoLineN(1, F("Cooling down"));
        if (appState.PidControl.Measured < 50) {
            lcdInfoLineN(1, F("Cool down done."));
            lcdInfoLineN(2, F("Switch to IDLE."));
            appState.State = IDLE;
        }
    } else if (appState.State == IDLE) {
        if (BUTTON_AT_LEAST(5) && BUTTON_RELEASED) {
            // test if we pressed button for more than 5 periods and released it - start reflow
            startReflow();
        } else if (BUTTON_AT_MOST(5) && BUTTON_RELEASED) {
            // short press, we switch profiles
            goToNextProfile(1);
        }
    }
}
#else 
void updateSystemInputState() {
    // dummy blank
    return;
}
#endif

void goToNextProfile(int direction) {
    char l[16];
    if (appState.ActiveProfileNumber + direction < 1) {
        appState.ActiveProfileNumber = PROFILES_NUM;
    } else if (appState.ActiveProfileNumber + direction > PROFILES_NUM) {
        appState.ActiveProfileNumber = 1;
    } else {
        appState.ActiveProfileNumber += direction;
    }
    Serial.print(F("-- switching profile: "));
    Serial.print(appState.ActiveProfileNumber);
    Serial.print(" ");
    Serial.print(profiles[appState.ActiveProfileNumber - 1].name);
    Serial.println(F(" --"));
    snprintf(l, 16, "Prof %d: %s", appState.ActiveProfileNumber, profiles[appState.ActiveProfileNumber - 1].name);
    lcdInfoLineN(1, l);
    Serial.println(l);
    lcdInfoLineN(2, "DBLCLK TO START");
}

// change info panel
void goToNextPanel(int direction) {

}

void updateSystemReflowState() {
    // implement the reflow steps
    unsigned int timeLeft;
    char l[16];
    if (appState.State != IN_PROCESS) {
        return;
    }
    appState.ticks++;
    // transition from IDLE to IN_PROCESS, let's bootstrap some things
    if (appState.LastState == IDLE) {
        // transition to PRE-HEATING and load the adequate duration
        loadActiveProfile(appState.ActiveProfileNumber, 1);
    }
    // general case handling
    timeLeft = (appState.StepFinishTime - appState.SystemUptime) / 1000UL;
    const ReflowProfile *profile = &profiles[appState.ActiveProfileNumber];

    snprintf(l, 16, "%d s left", timeLeft);
    lcdInfoLineN(1, l);
    snprintf(l, 16, "%s %s T:%d", appState.ActiveProfileName, appState.ActiveStepName, (int)appState.PidControl.Measured);
    lcdInfoLineN(2, l);
    if (appState.SystemUptime > appState.StepFinishTime) {
        // go next step
        loadActiveProfile(appState.ActiveProfileNumber, appState.ActiveStepNumber + 1);
    }
    if (appState.ticks % 128 == 0) {
        Serial.println("State info");
        Serial.print("Ticks:"); Serial.println(appState.ticks);
        Serial.print("Temperature:"); Serial.println(appState.PidControl.Measured);
        Serial.print("Time left:"); Serial.println(timeLeft);
        Serial.print("System uptime:"); Serial.println(appState.SystemUptime / 1000);
        Serial.print("End step:"); Serial.println(appState.StepFinishTime / 1000);
        Serial.print("Step name:"); Serial.println(appState.ActiveStepName);
        Serial.print("Cycle time:"); Serial.println(appState.cycleTime);
        Serial.println("--");
    }
}

void wrapupLoop() {
#ifdef NO_USE_ROTARY
    // if button was released, reset the ButtonsPressed mask to 0
    if (appState.ButtonsPressedMask > 0 && appState.ButtonsPressedMask % 2 == 0) {
        appState.ButtonsPressedMask = 0;
    }
#endif
#ifdef PWM_CONTRAST
    if (appState.toCallback & 1 == 1) {
        appState.toCallback = appState.toCallback ^ 1;
        analogWrite(LCD_VO, appState.LCDContrast);
        Serial.print("Wrote to LCD_VO: ");
        Serial.println(appState.LCDContrast);
    }
#endif
}

void lcdDebugStr(byte line, char* prefix, char* data) {
    char l[16];
    snprintf(l, 16, "%s%s", prefix, data);
    lcdInfoLineN(line, l);
    Serial.print("LCD[");
    Serial.print(line);
    Serial.print("]: ");
    Serial.println(l);

}

void doDelay() {
    unsigned long su;
    // get uptime
    su = millis();
    // delay for the number of milliseconds that remains in a 200 ms cycle (our interaction frequency is 5 Hz)
    appState.cycleTime = su - appState.SystemUptime;
    // update system uptime
    appState.SystemUptime = su;
#ifdef NO_USE_ROTARY
    delay(TIME_INCREMENT - appState.cycleTime);
#else
    su = TIME_INCREMENT - appState.cycleTime;
    while (su > 50 && su < appState.cycleTime) {
        su -= 50;
        delay(50);
        button.update();
    }
    if (su < 50) {
        button.update();
        delay(su);
    }
#endif
}

void testMode() {
    byte oldVerbosity = appState.verbosity;
    appState.verbosity = 255;
    Serial.println("Initiating test. This is synchronous code");
    Serial.println("Turn on relay for the duration of the test");
    Serial.println("Pre-heat: 20 sec");
    lcdDebugStr(1, "D:", "Relay (20 sec)");
    setRelay(HIGH);
    delay(20000);
    Serial.println("Check temperature (5 samples with 5 sec interval)");
    lcdDebugStr(1, "D:", "Temperature");
    int i = 0;
    float t;
    char b[16];
    for (; i < 5; i++) {
        t = thermocouple.readCelsius();
        Serial.print("temp: ");
        Serial.println(t);
        lcdDebugStr(2, "#:", snprintf(b, 16, "%d: %f", i+1, t));
        delay(5000);
        Serial.print("Relay state[+:");
        Serial.print(digitalRead(REL_P) * 100);
        Serial.println("]");
    }
    Serial.println("temp check: done");
    lcdDebugStr(1, "T:", "Tests complete");
    lcdDebugStr(2, "--", "----- :)");
    setRelay(LOW);
    Serial.println("Turn off relay");
    lcdDebugStr(2, "D:", "Relay: turn off");
    appState.verbosity = oldVerbosity;
}
void loop() {
    debug();
    updateAppState();
    probeTemp();
    pidController.compute();
    actOnPID();
    handleInput();
    updateSystemInputState(); // blank when rotary
    updateSystemReflowState(); // only when IN_PROCESS
//    displayTemp();
    wrapupLoop();
    doDelay();
}
