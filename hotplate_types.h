/** Wiring
 *  
 *  
 */

 
enum SystemState {
  IN_PROCESS,
  COOLING,
  IDLE,
};

enum ReflowStepsSequence {
  PREHEAT,
  SOAK,
  RAMP_UP,
  REFLOW
};

const char ReflowStepsNames[4][16] {
    {"Pre-heat"},
        {"Soak"},
        { "Ramp up"},
        {"Reflow"},
};

typedef struct {
  unsigned int temperature;
  unsigned int seconds;
  ReflowStepsSequence state;
} ReflowStep;

typedef struct {
  const char name[8];
  ReflowStep steps[4];
} ReflowProfile;

// type for PID control
typedef struct {
  double SetPoint;
  double Measured;
  double OutputDuration;
} PIDControl;
// type for state management

typedef struct {
  unsigned long SystemUptime;
  // this is for PID control
  unsigned long EvaluationWindowStart;
  byte cycleTime;
  int WindowSize;
  PIDControl PidControl;
  // input management
  byte ButtonsPressedMask;
  // this is for process management
  SystemState LastState;
  SystemState State;
  char * ActiveProfileName;
  char * ActiveStepName;
  int ActiveProfileNumber;
  int ActiveStepNumber;
  double DesiredTemperature;
  unsigned long StepFinishTime;
  byte LCDContrast;
  byte toCallback;
  byte verbosity;
  byte helpStepper;
  byte ticks;
} AppState;

// general settings
// 200 ms general reactivity
#define TIME_INCREMENT 200
// we will control the plate with 5 seconds increments initially
#define PID_WINDOW 5000
// sweet spot
#define SWEET_SPOT 3

// if we adjust the window, it will never be above 30 seconds
#define PID_WINDOW_MAX 30000
// number of profiles
#define PROFILES_NUM 3



// PID initial settings
#define PID_KP 0
#define PID_KI 5
#define PID_KM 1

// utility macros
#define BUTTON_STATE digitalRead(RE_B)
#define BUTTON_IS_PUSHED (digitalRead(RE_B) == 0)
#define BUTTON_IS_NOT_PUSHED (digitalRead(RE_B) == 1)

// button was pressed for at least n periods and then released
#define BUTTON_AT_LEAST(n) appState.ButtonsPressedMask >= (1 << (n)) && appState.ButtonsPressedMask % 2 == 0
// button was pressed for at most n periods (n > 0) and then released
#define BUTTON_AT_MOST(n) appState.ButtonsPressedMask > (1 << (n)) && appState.ButtonsPressedMask % 2 == 0
// button was released this turn
#define BUTTON_RELEASED appState.ButtonsPressedMask > 0 && appState.ButtonsPressedMask % 2 == 0
// utility values
#define BLANK_LCD_LINE "                "

// EEPROM saves
#define EEPROM_FORMAT 1
#define EEPROM_START_ADDRESS 8
