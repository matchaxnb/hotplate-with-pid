/* wiring for hotplate */

/**
 
Cables and accessories to make/source
---
hardware:
MAX6675-compatible SPI read thermocouple
A low-current relay (SSR is best)
Hitachi-compatible LCD
A rotary encoder for user input (5 pin, with button, no breakout)
1 rc filter assembly (1 capacitor = 10uF, 1 resistor = 3.9kO) -> to D3 for PWM to Analog output
---
Power rails:
PR33V: 8-pin 3.3V (+) power rail with screw terminals
0: to Arduino (feed)
1: to LCD LEDP
2: to LCD VDD
3: to TH_VCC

PRGND: 8-pin GND (-) power rail with screw terminals
0: to Arduino (sink)
1: to LCD LEDM
2: to LCD_VSS
3: to TH_GND
4: to encoder GND
5: to RC filter GND

Wire PR33V to pin 3.3V
Wire PRGND to one of the GND pins

--- Cables
LCD: 8-wire cable with small splits

plan wiggle space.

0--------0 VO (contrast, small RC-filter mounted on it)
----- (split here close to Arduino as we mount the RC filter)
1--------1 RS
2--------2 RW
3--------3 EN
----- (split here close to LCD-end, we skip DATA0...DATA3)
4--------4 DATA4
5--------5 DATA5
6--------6 DATA6
7--------7 DATA7

LCD: 2-wire cable for power supply
0-------0 VCC 3.3V (to power rail +)
1-------1 GND (to power rail -)

MAX6675: 5-wire cable for thermostat

0-------0 VCC 3.3V (to power rail +)
1-------1 GND (to power rail -)
--------- (split here close to Arduino end)
2-------2 SCK
3-------3 CS
4-------4 SO

Relay: 2-wire cable for control with split to power rail.
0-------0 REL_P
1-------1 GND (to power rail -)

Rotary encoder: 3-wire cable for data
0-------0 RE_B (button)
1-------1 RE_P1
2-------2 RE_P2
**/

/*** PINS ***/
// defines for pins

// SPI Thermocouple MAX6675
#define TH_SO  A0
#define TH_CS  A1
#define TH_SCK A2

// Relay
#define REL_P A5

// Display
#ifdef PWM_CONTRAST
#define LCD_VO 5  // LCD backlight intensity
#endif

#define LCD_RS 6   // Register Select
#define LCD_RW 7   // Read-Write Selector
#define LCD_EN 8   // Clock Enable
#define LCD_D4 9   // Data-4
#define LCD_D5 10  // Data-5
#define LCD_D6 11  // Data-6
#define LCD_D7 12   // Data-7

// backlight for the LCD. we should feed that from a direct voltage pin // #define LCD_LEDP 10 // Backlight Anode (Led-Plus) = 3.3V
// #define LCD_LEDM 11 // Backlight Cathode (Led-Minus) = GND
// #define BL_P A6 // Backlight-
// #define BL_M A7 // GND

// Rotary Encoder
#define RE_P1 2
#define RE_P2 3
#define RE_B  4

/*** END PINS ***/
