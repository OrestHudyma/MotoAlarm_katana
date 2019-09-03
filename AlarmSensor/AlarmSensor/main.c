/*
 * AlarmSensor.c
 *
 * Created: 24-May-19 15:25:01
 * Author : ohud
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define F_CPU 1200000UL

#define POWER_STAB_DELAY 2000

unsigned char alarm_event_flag = 0;
unsigned char mercury_sensors_state;

ISR(PCINT0_vect)
{
	alarm_event_flag = 1;
}

int main(void)
{
    _delay_ms (POWER_STAB_DELAY);	// Wait for power to get stable
	
	DDRA = 0x00;	// Configure port A as input
	
	// Configure device for mercury angle sensors support on port B 
	DDRB = 0x00;					// Set port B to be input
	PORTB = 0xFF;					// Connect port B pull up
	GIMSK |= (1<<PCIE);				// Enable PCINT interrupts
	PCMSK = 0xFF;					// Enable PCINT interrupts for all PCINT pins
	mercury_sensors_state = PINB;	// Remember initial mercury sensors state
	
    while (1) 
    {
		if(alarm_event_flag)
		{			
			PORTB |= (1<<PA0);	// Set pin PA0 to high
			DDRA |= (1<<PA0);	// Configure pin PA0 as output
			
			 _delay_ms (POWER_STAB_DELAY);
			 
			DDRA &= (0<<PA0);	// Configure pin PA0 as input
			PORTB &= (0<<PA0);	// Set pin PA0 to high 
		}		
    }
}

