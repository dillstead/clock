#include <Wire.h>

#include "RTClib.h"
#include "ButtonGroup.h"

// Mode that maps time to proper analog votage.
#define DISPLAY_MODE   0
// Mode the outputs the maximum analog voltage for calibrating the meters.
#define CALIBRATE_MODE 1

const byte secondInputPin = 2;
const byte minuteInputPin = 3;
const byte hourInputPin = 4;
const byte calibratePin = 5;
const byte secondOutputPin = 9;
const byte minuteOutputPin = 10;
const byte hourOutputPin = 11;

RTC_DS1307 rtc;
ButtonGroup clockButtons(buttonCallback, NULL);

// Global state variables that are updated depending on what buttons
// are being pressed.
unsigned int timeAdjustment;
// DISPLAY or CALIBRATE
byte mode;
// The previously seen second.
byte prevSecond;
// The number of milliseconds at the start of the last second.
// Should work fine in the face of overflow.
unsigned long prevSecondStartMs;

const char *mapPinToButtonName(byte pin)
{
    switch (pin)
    {
    case secondInputPin:
    {
        return "second button";
    }
    case minuteInputPin:
    {
        return "minute button";
    }
    case hourInputPin:
    {
        return "hour button";
    }
    default:
    {
        return "calibrate button";
    }
    }
    return NULL;
}

void buttonCallback(byte pin, int newState, int oldState, void *data)
{
    // Pin going from HIGH to LOW indicates a button press.
    if (oldState == HIGH && newState == LOW)
    {
        // Determine what pin changes.  If a time pin changes, adjust
        // the time accordingly.  If the calibration pin changes,
        // switch the mode.
        if (pin == calibratePin)
        {
            mode = !mode;
        }
        else
        {
            if (pin == secondInputPin)
            {
                // Increase by 1 second.
                timeAdjustment += 1;
            }
            else if (pin == minuteInputPin)
            {
                // Increase by 60 seconds.
                timeAdjustment += 60;
            }
            else
            {
                // Increase by 3600 seconds.
                timeAdjustment += 3600;
            }
        }

        char buffer[50];
        sprintf(buffer, "Pin %s changed from HIGH to LOW, adjust %d",
                mapPinToButtonName(pin), timeAdjustment);
        Serial.println(buffer);
    }
}


int mapHourToVoltage(DateTime &adjustedTime)
{
    // Here's how the hours map:
    // H  M
    // 00:00 -> 0
    // 00:01 -> 0
    // 00:02 -> 0
    // 00:03 -> 1
    // 00:04 -> 1
    // 00:05 -> 1
    // 00:06 -> 2
    // ...
    // 00:59 -> 19
    // 01:00 -> 20
    // ...
    // 11:59 -> 239
    // 12:00 -> 00:00 -> 0
    return adjustedTime.hour() % 12 * 20 + adjustedTime.minute() / 3;
}

int mapMinuteToVoltage(DateTime &adjustedTime)
{
    // Here's how the minutes map:
    // M  S
    // 00:00 -> 0
    // 00:01 -> 0
    // 00:02 -> 0
    // ...
    // 00:14 -> 0
    // 00:15 -> 1
    // ...
    // 00:59 -> 3
    // 01:00 -> 4
    // ...
    // 59:59 -> 239
    // 00:00 -> 0
    return adjustedTime.minute() * 4 + adjustedTime.second() / 15;
}

int mapSecondToVoltage(DateTime &adjustedTime, unsigned int prevSecondMs)
{
    // Here's how the seconds map:
    // S  MS
    // 00:000 -> 0
    // 00:001 -> 0
    // ...
    // 00:249 -> 0
    // 00:250 -> 1
    // ...
    // 00:999 -> 3
    // 01:000 -> 4
    // ...
    // 59:999 -> 239
    // 00:000 -> 0
    return adjustedTime.second() * 4 + prevSecondMs / 250;
}

void setup() 
{
    // Power up the RTC.
    pinMode(A2, OUTPUT);
    pinMode(A3, OUTPUT);
    digitalWrite(A2, LOW);
    digitalWrite(A3, HIGH);

    // Initialize serial communications and RTC.
    Serial.begin(57600);
    Wire.begin();
    rtc.begin();

    // All clock buttons are using Arduino's internal pullup resistor.
    // A LOW reading indicates that the button is pressed.
    pinMode(secondInputPin, INPUT_PULLUP);
    clockButtons.registerPin(secondInputPin, HIGH);
    pinMode(minuteInputPin, INPUT_PULLUP);
    clockButtons.registerPin(minuteInputPin, HIGH);
    pinMode(hourInputPin, INPUT_PULLUP);
    clockButtons.registerPin(hourInputPin, HIGH);
    pinMode(calibratePin, INPUT_PULLUP);
    clockButtons.registerPin(calibratePin, HIGH);

    // Always start is display mode, calibrate mode is entered by a
    // button press.
    mode = DISPLAY_MODE;

    // Kickstart the RTC if it's not yet running.
    if (!rtc.isrunning())
    {
        Serial.println("RTC not running");
        rtc.adjust(DateTime(__DATE__, __TIME__));
    }
}

void loop() 
{
    // Set intial values for the global state variables.
    timeAdjustment = 0;
    unsigned long time = rtc.now().unixtime();
    // Number of milliseconds since the start of the previous second.
    unsigned int prevSecondMs = 0;
    boolean printTimeAndVoltage = false;

    // Read any button presses that might adjust the time or mode.
    clockButtons.readAllPins();

    time += timeAdjustment;
    DateTime adjustedTime(time);

    // If the time has changed, make sure to set the RTC.
    if (timeAdjustment > 0)
    {
        rtc.adjust(adjustedTime);
    }

    // Compute the number of milliseconds since the beginning of the previous
    // second
    if (adjustedTime.second() != prevSecond)
    {
        prevSecond = adjustedTime.second();
        prevSecondStartMs = millis();
        printTimeAndVoltage = true;
    }
    else
    {
        prevSecondMs = millis() - prevSecondStartMs;
    }

    // Map the time to analog voltage output. Analog voltage output
    // ranges from 0 to 255.  To make the mapping easier, we'll only use
    // 0 to 239.
    int secondVoltage = 239;
    int minuteVoltage = 239;
    int hourVoltage = 239;
    if (mode == DISPLAY_MODE)
    {
        secondVoltage = mapSecondToVoltage(adjustedTime, prevSecondMs);
        minuteVoltage = mapMinuteToVoltage(adjustedTime);
        hourVoltage = mapHourToVoltage(adjustedTime);
    }

    // Output the proper voltage level.
    analogWrite(secondOutputPin, secondVoltage);
    analogWrite(minuteOutputPin, minuteVoltage);
    analogWrite(hourOutputPin, hourVoltage);

    if (printTimeAndVoltage)
    {
        char buffer[50];
        sprintf(buffer, "time: %d:%d:%d", adjustedTime.hour(),
                adjustedTime.minute(), adjustedTime.second());
        Serial.println(buffer);
        sprintf(buffer, "voltage level: %d:%d:%d", hourVoltage, minuteVoltage,
                secondVoltage);
        Serial.println(buffer);
    }
}
