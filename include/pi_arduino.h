

//Translation layer between arduino types/functions and Circle types/functions
#ifndef _pi_arduino_h
#define _pi_arduino_h

//We need some define that exists in there
#include <circle/gpiopin.h>
#include <circle/timer.h>

typedef unsigned char		uint8_t;
typedef unsigned short		uint16_t;
typedef unsigned int		uint32_t;
typedef signed char		    int8_t;
typedef signed short		int16_t;
typedef signed int		    int32_t;
typedef long unsigned int   size_t;


#define CHANGE 2
#define FALLING 3
#define RISING 4

#define INPUT 0x0
#define OUTPUT 0x1
#define INPUT_PULLUP 0x2

#define true 0x1
#define false 0x0

#define lowByte(w) ((uint8_t) ((w) & 0xff))
#define highByte(w) ((uint8_t) ((w) >> 8))

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) (bitvalue ? bitSet(value, bit) : bitClear(value, bit))


#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))

#define millis() CTimer::GetClockTicks()/1000
#define delay(x) CTimer::SimpleMsDelay(x)

#endif
