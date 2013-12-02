#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <avr/io.h>
#include <util/delay.h>

#include "avr.h"

// Number of shift registers in your scoreboard
// If you want scores to go over 199, you need 8
const int nsr = 8;

// 
// Timing stuff
// 

// 
// 2**32 deciseconds = 13.610221 years
// 
// As long as you unplug your scoreboard once every 10 years or so,
// you're good.
// 
volatile uint32_t jiffies = 0;	// Elapsed time in deciseconds
volatile bool tick = false;	// Set high when jiffy clock ticks


// Clocks are in deciseconds
uint16_t score_a = 0;
uint16_t score_b = 0;
int16_t period_clock = -(30 * 60 * 10);
int16_t jam_duration = -(2 * 60 * 10);
int16_t lineup_duration = (-30 * 10);
int16_t jam_clock = 0;
enum {
	SETUP,
	JAM,
	LINEUP,
	TIMEOUT,
	KONAMI
} state = SETUP;


// NES Controller buttons

#define BTN_A _BV(7)
#define BTN_B _BV(6)
#define BTN_SELECT _BV(5)
#define BTN_START _BV(4)
#define BTN_UP _BV(3)
#define BTN_DOWN _BV(2)
#define BTN_LEFT _BV(1)
#define BTN_RIGHT _BV(0)

const uint8_t konami_code[] = {
	BTN_UP, BTN_UP, BTN_DOWN, BTN_DOWN,
	BTN_LEFT, BTN_RIGHT, BTN_LEFT, BTN_RIGHT,
	BTN_B, BTN_A,
	0
};
int konami_pos = 0;
const uint8_t test_pattern[] = {
	_BV(2), _BV(3), _BV(4), _BV(5), _BV(6), _BV(1), _BV(0), _BV(7)
};

const uint8_t seven_segment_digits[] = {
	0x7b, 0x60, 0x37, 0x76, 0x6c, 0x5e, 0x5f, 0x70, 0x7f, 0x7e
};

const uint8_t setup_digits[] = {
	0x1b, 0x12, 0x72
};

void
latch()
{
	sltch(true);
	sltch(false);
}

void
pulse()
{
	sclk(true);
	sclk(false);
}

void
write(uint8_t number)
{
	int i;
	int j;

	// MSB first
	for (i = 7; i >= 0; i -= 1) {
		sin(number & (1 << i));
		pulse();
	}
}

void
write_num(uint16_t number, int digits)
{
	int i;

	for (i = 0; i < digits; i += 1) {
		uint8_t out = seven_segment_digits[number % 10];

		// Overflow indicator
		if ((i == digits - 1) && (number > 9)) {
			// Blink to indicate double-rollover
			if ((number > 19) && (jiffies % 3 == 0)) {
				// leave it blank
			} else {
				out ^= 0x80;
			}
		}

		write(out);
		number /= 10;
	}
}

/*
 * Update all the digits
 */
void
draw()
{

	uint16_t jclk;
	uint16_t pclk;
	bool blank = ((state == TIMEOUT) && (jiffies % 8 == 0));
	
	// Segments test mode
	if (KONAMI == state) {
		int i;
		
		for (i = 0; i < 12; i += 1) {
			write(test_pattern[jiffies % (sizeof test_pattern)]);;
		}
		
		latch();
		pulse();
		return;
	}

	jclk = (abs(jam_clock / 600) % 10) * 1000;
	jclk += abs(jam_clock) % 600;

	pclk = (abs(period_clock / 10) / 60) * 100;
	pclk += abs(period_clock / 10) % 60;
	
#ifdef DEMO
	if (jam_clock == 0) {
		if (state == LINEUP) {
			state = JAM;
			jam_clock = jam_duration;
		} else {
			state = LINEUP;
			jam_clock = lineup_duration;
		}
	}
	
	if (period_clock == 0) {
		period_clock = - (30 * 60 * 10);
		jam_clock = jam_duration;
		state = JAM;
	}
#endif

	// Score A
	write_num(score_b, 2);

	// Jam clock, least significant half
	write_num(jclk % 100, 2);

	// Period clock
	if (blank) {
		write(0);
		write(0);
		write(0);
		write(0);
	} else if (state == SETUP) {
		write(setup_digits[2]);
		write(setup_digits[1]);
		write(setup_digits[1]);
		write(setup_digits[0]);
	} else {
		write_num(pclk, 4);
	}

	// Jam clock, most significant half
	write_num(jclk / 100, 2);

	// Score A
	write_num(score_a, 2);

	if (false) {
		int i;
		for (i = 0; i < 12; i += 1) {
			write_num(jiffies / 10, 1);
		}
	}
	// Tell chips to start displaying new values 
	latch();
	pulse();
}

/*
 * Probe the NES controller
 */
uint8_t
nesprobe()
{
	int i;
	uint8_t state = 0;

	nesltch(true);
	nesltch(false);

	for (i = 0; i < 8; i += 1) {
		state <<= 1;
		if (nesout()) {
			// Button not pressed
		} else {
			state |= 1;
		}
		nesclk(true);
		nesclk(false);
	}

	// Only report button down events.
	return state;
}

void
update_controller()
{
	static uint8_t last_val = 0;
	static uint32_t last_change = 0;
	static uint32_t last_typematic = 0;
	uint8_t cur;
	uint8_t pressed;
	int inc = 1;

	cur = nesprobe();
	pressed = (last_val ^ cur) & cur;
	if (last_val != cur) {
		last_change = jiffies;
		last_val = cur;
	}
	
	if (pressed == konami_code[konami_pos]) {
		konami_pos += 1;
	
		if (konami_code[konami_pos] == 0) {
			state = KONAMI;
			konami_pos = 0;
			return;
		} else if (konami_pos > 3) {
			return;
		}
	}
	
	// Select means subtract
	if (cur & BTN_SELECT) {
		inc = -1;
	}

	if ((pressed & BTN_A) && ((state != JAM) || (jam_clock == 0))) {
		state = JAM;
		jam_clock = jam_duration;
	}

	if ((pressed & BTN_B) && ((state != LINEUP) || (jam_clock == 0))) {
		state = LINEUP;
		jam_clock = lineup_duration;
	}

	if ((pressed & BTN_START) && (state != TIMEOUT)) {
		state = TIMEOUT;
		jam_clock = 1;
	}

	if ((state == TIMEOUT) || (state == SETUP)) {
		uint8_t v = pressed;

		if ((jiffies - last_change > 10) && (last_typematic < jiffies)) {
			v = cur;
			last_typematic = jiffies;
		}
		if (v & BTN_UP) {
			period_clock -= 10;
		}
		if (v & BTN_DOWN) {
			period_clock += 10;
		}
	}

	if (pressed & BTN_LEFT) {
		score_a += inc;
	}

	if (pressed & BTN_RIGHT) {
		score_b += inc;
	}
}

/*
 *
 * Main program
 *
 */


void
setup()
{
	// The TLC5941 required some setup.
	// The TPIC doesn't.
	// Hooray.

	PORTB = 0xff;
}

void
loop()
{
	update_controller();

	if (tick) {
		tick = false;

		if (jiffies % 5 == 0) {
			PORTB ^= 0xff;
		}

		switch (state) {
		case SETUP:
			break;
		case JAM:
		case LINEUP:
			if (period_clock) {
				period_clock += 1;
			}
			// fall through
		case TIMEOUT:
			if (jam_clock) {
				jam_clock += 1;
			}
		}
	}

	draw();
}

int
main(void)
{
	init();
	setup();
	for (;;) {
		loop();
	}
	return 0;
}
