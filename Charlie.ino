#include <WiFi101.h>
#include <RTCZero.h>
#include <ThingSpeak.h>
#include <rgb_lcd.h>
#include <stdarg.h>
#include "Secrets.h"

RTCZero rtc;
rgb_lcd lcd;
const unsigned int rows = 2;
unsigned long epoch = 0;
unsigned int readings[5];

namespace {
    template<typename T>
    // Converts Inputs into Lines and Prints Them
    void printToLCD(T inputs, ...) 
    {
        va_list arguments;
        va_start(arguments, inputs);
        T lines[] = {inputs, va_arg(arguments, T)};
        va_end(arguments);
        lcd.clear();

        for (unsigned int i = 0; i < rows; i++) {
            lcd.setCursor(0, i);
            lcd.print(lines[i]);
        }
    }

    template<typename T>
    char* bufferToCharArray(int size, T inputs, ...) 
    {
        char *buffer = new char[size];
        va_list arguments;
        va_start(arguments, inputs);
        vsprintf(buffer, inputs, arguments);
        va_end(arguments);
        return buffer;
    }
}

struct Limits {
    unsigned int a = 400, b = 700, c = 500;
    int first, last, average;
    
    bool exceedLimits(const double limit = 1.0) 
    {
        return abs(first - last) > (a * limit) || first > (b * limit) || last > (b * limit) || average > (c * limit);
    }
};

void setup()
{
    rtc.begin();
    lcd.begin(16, rows);
    pinMode(A2, INPUT);

    if (connectedToWiFi() && connectedToTime()) {
        WiFiClient client;
        ThingSpeak.begin(client);
        
        rtc.setEpoch(epoch);
        lcd.setRGB(0, 210, 0);
        printTimeToLCD((char*) "Connected");
    } else {
        lcd.setRGB(204, 0, 0);
        printTimeToLCD((char*) "No Connection");
    }

    rtc.setAlarmMinutes(0);
    rtc.enableAlarm(rtc.MATCH_SS);
    rtc.attachInterrupt(alarmMatch);

    delay(5000);
    rtc.standbyMode();
}

void loop()
{  
    if (WiFi.status() == WL_CONNECTED || connectedToWiFi()) {
        unsigned int reading = analogRead(2);
        int counter = setNextAvailableReading();
    
        if (counter > -1) { // Between 0 and 4 Readings
            if (reading > 10 && reading < 798) readings[counter] = reading;
            
            lcd.setRGB(135, 206, 250);
            printTimeToLCD(bufferToCharArray(16, "Last Value: %03u", readings[counter]));
        } else { // Must Have 5 Readings
            unsigned int total = 0;
            for(unsigned int i = 0; i < 5; i++) total += readings[i];

            Limits Limits;
            Limits.first = readings[0];
            Limits.average = total / (int) (sizeof(readings) / sizeof(readings[0]));
            Limits.last = readings[4];

            if (Limits.exceedLimits()) {
                lcd.setRGB(128, 0, 128);
                printTimeToLCD((char*) "Air: Danger");
            } else if (Limits.exceedLimits(0.75)) {
                lcd.setRGB(204, 0, 0);
                printTimeToLCD((char*) "Air: Bad");
            } else if (Limits.exceedLimits(0.45)) {
                lcd.setRGB(255, 255, 0);
                printTimeToLCD((char*) "Air: OK");
            } else {
                lcd.setRGB(0, 210, 0);
                printTimeToLCD((char*) "Air: Clean");  
            }

            // Send Readings
            ThingSpeak.setField(1, Limits.first);
            ThingSpeak.setField(2, Limits.average);
            ThingSpeak.setField(3, Limits.last);
            int server = ThingSpeak.writeFields(SECRET_ID, SECRET_API);

            // Reset Readings
            memset(readings, 0, sizeof(readings));
        }
    } else {
        lcd.setRGB(204, 0, 0);
        printTimeToLCD((char*) "No Connection");
    }

    rtc.standbyMode();
}

void alarmMatch() 
{
    // Empty Trigger
}

bool connectedToWiFi() 
{
    // Alternates Based on Security
    do {
        #ifdef SECRET_PASS
        WiFi.begin(SECRET_SSID, SECRET_PASS);
        #else
        WiFi.begin(SECRET_SSID);
        #endif
    } while (WiFi.status() == WL_IDLE_STATUS);

    WiFi.maxLowPowerMode();

    return WiFi.status() == WL_CONNECTED;
}

bool connectedToTime() 
{
    if (WiFi.status() != WL_CONNECTED) return false; // Return False if No Connection
    for(unsigned int i = 0; i < 5, epoch == 0; i++, delay(1000)) epoch = WiFi.getTime();
    return epoch > 0;
}

void printTimeToLCD(char* input)
{
    printToLCD(bufferToCharArray(5, "%02u:%02u", rtc.getHours(), rtc.getMinutes()), input);
}

int setNextAvailableReading()
{
    for(int i = 0; i < sizeof(readings) / sizeof(readings[0]); i++) {
      if (readings[i] == 0) return i;
    }
    return -1; // No Available Readings (FULL)
}
