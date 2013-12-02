#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>

#include "ascii.h"

#define bit(reg, bit, on) reg = (on ? (reg | _BV(bit)) : (reg & ~_BV(bit)));

#define sce(on) bit(PORTA, 0, on)
#define rst(on) bit(PORTA, 1, on)
#define dc(on) bit(PORTA, 2, on)
#define sclk(on) bit(PORTA, 4, on)
#define dn(on) bit(PORTA, 5, on)

#define LED _BV(OC0A)

#define LCD_X 84
#define LCD_Y 48

#define LCD_COMMAND false
#define LCD_DATA  true

volatile uint32_t jiffies = 0;
volatile bool tick = false;

void
initPWM0()
{
	DDRA |= (1 << PA7);	// Set OC0B to output

	// Phase-correct PWM, non-inverting mode
	TCCR0A |= _BV(COM0A1) | _BV(WGM00);

	// No clock prescaling
	TCCR0B |= ((1 << CS00));
}

inline void
setPWM0A(uint8_t num)
{
	OCR0A = num;
}

void
LCDWrite(bool datap, uint8_t v)
{
	int i;
	
	dc(datap);
	sce(false);

	// Push out over SPI
	USIDR = v;
	USISR = _BV(USIOIF);
	while ((USISR & _BV(USIOIF)) == 0) {
		USICR = _BV(USIWM0) | _BV(USICS1) | _BV(USICLK) | _BV(USITC);
	}

	sce(true);
}

void
LCDChar(char c)
{
	int i;
	
	LCDWrite(true, 0);
	for (i = 0; i < 5; i += 1) {
		LCDWrite(true, ASCII[c - 0x20][i]);
	}
	LCDWrite(true, 0);
}

void
LCDString(char *s)
{
	for (; *s; s += 1) {
		LCDChar(*s);
	}
}

void
LCDGoto(int x, int y)
{
	LCDWrite(false, 0x80 | x);	// Column.
	LCDWrite(false, 0x40 | y);	// Row.  ?
}

void
LCDClear(void)
{
	int i;
	
	LCDGoto(0, 0);
	for (i = 0; i < (LCD_X * LCD_Y / 8); i++) {
		LCDWrite(true, 0x00);
	}

	LCDGoto(0, 0);		// After we clear the display, return to the home position
}

void
write_digit(uint8_t d)
{
	LCDChar('0' + (d % 10));
}

void
write_time(uint16_t secs)
{
	uint16_t min;
	
	min = secs / 60;
	secs = secs % 60;
	
	if (min >= 10) {
		write_digit(min / 10);
	} else {
		LCDChar(' ');
	}
	write_digit(min % 10);
	LCDChar(':');
	write_digit(secs / 10);
	write_digit(secs % 10);	
}

void
loop(void)
{
	static uint8_t v = 0;

	if (tick) {
		int i;

		tick = 0;

		OCR0A = (jiffies % 10) * 2;

		if (jiffies % 10 == 0) {
			LCDGoto(0, 0);
			LCDString("Jam    ");
			write_time(jiffies / 10);
		}
	}
}


int
main(void)
{
	init();
	initPWM0();

	DDRA = 0xFF;

	// Reset display
	rst(false);
	rst(true);

	LCDWrite(false, 0x21);	// Tell LCD that extended commands follow
	LCDWrite(false, 0xB0);	// Set LCD Vop (Contrast): Try 0xB1(good @ 3.3V) or 0xBF if your display is too dark
	LCDWrite(false, 0x04);	// Set Temp coefficent
	LCDWrite(false, 0x13);	// LCD bias mode 1:48: Try 0x13 or 0x14

	LCDWrite(false, 0x20);	// We must send 0x20 before modifying the display control mode
	LCDWrite(false, 0x0C);	// Set display control, normal mode. 0x0D for inverse
	
	LCDClear();

	for (;;) {
		loop();
	}
	return 0;
}
