#include <Arduino.h>
#include <Encoder.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>


const byte pin_dimmer_pwm = 9;
const byte pin_foot_pedal = 8;
const byte pin_pushbutton = 3;

Encoder encoder(2, 4);
Adafruit_SSD1306 display(128, 64, &Wire, -1);

enum DeviceState
{
  IDLE,
  EXPOSURE,
  SCROLL_MODE,
  EDIT_DURATION,
  EDIT_POWER,  
};
DeviceState currentState = IDLE;

enum EditSelection
{
  RETURN,
  POWER,
  DURATION,
};
EditSelection currentSelection = RETURN;

unsigned long exposure_start_time = 0;
unsigned long exposure_duration = 60000; // 60 seconds
uint16_t dimmer_value = 6000; // 75 % power, 15 W

const unsigned long MAX_EXPOSURE_DURATION = 600000; // 10 minutes
const unsigned long MIN_EXPOSURE_DURATION = 0; // 0 seconds
const uint16_t MAX_DIMMER_VALUE = 8000; // 100 % power
const uint16_t MIN_DIMMER_VALUE = 0; // 0 % power

const unsigned int MAX_POWER = 20; //Maximum power 20 W
const unsigned int DIMMER_PER_WATT = MAX_DIMMER_VALUE / MAX_POWER;

const unsigned long DEBOUNCE_DELAY = 50;
const unsigned long DISPLAY_UPDATE_INTERVAL = 100;
const int ENCODER_STEP_DURATION = 250;
const int ENCODER_STEP_POWER = 10;

void update_display();
void display_power_and_duration();

void setup()
{
  pinMode(pin_dimmer_pwm, OUTPUT);
  analogWrite(pin_dimmer_pwm, 0);

  pinMode(pin_foot_pedal, INPUT_PULLUP);
  pinMode(pin_pushbutton, INPUT_PULLUP);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();

  // Setup timer 1 for PWM output
  // Phase and frequency correct pwm mode
  // No prescaler
  // Top value 8000
  // PWM frequency 1 kHz
  noInterrupts();
  TCCR1A = 0 | (1 << COM1A1);
  TCCR1B = 0 | (1 << WGM13) | (1 << CS10);
  ICR1 = MAX_DIMMER_VALUE;
  OCR1A = 0;
  interrupts();
}

void loop()
{ 
  // Only if the pushbutton is high (released), it is ready to detect a new pressed event
  // This is to avoid multiple detections of the same button press
  // Additionally a 50 ms delay is added to avoid bouncing
  static bool pushbutton_ready = true;
  static unsigned long last_pushbutton = millis();
  if(!pushbutton_ready && digitalRead(pin_pushbutton))
  {
    pushbutton_ready = true;
  }

  // Same as for the pushbutton, but for the foot pedal
  static bool foot_pedal_ready = true;
  static unsigned long last_foot_pedal = millis();
  if(!foot_pedal_ready && digitalRead(pin_foot_pedal))
  {
    foot_pedal_ready = true;
  }

  switch (currentState)
  {
    case IDLE:
      if(!digitalRead(pin_foot_pedal) && millis() - last_foot_pedal > DEBOUNCE_DELAY && foot_pedal_ready)
      {
        last_foot_pedal = millis();
        foot_pedal_ready = false;
        currentState = EXPOSURE;
        exposure_start_time = millis();
        noInterrupts();
        OCR1A = dimmer_value;
        interrupts();
      }
      else if(!digitalRead(pin_pushbutton) && millis() - last_pushbutton > DEBOUNCE_DELAY && pushbutton_ready)
      {
        last_pushbutton = millis();
        pushbutton_ready = false;
        currentSelection = RETURN;
        currentState = SCROLL_MODE;
      }
      break;

    case EXPOSURE:
      if(!digitalRead(pin_foot_pedal) && millis() - last_foot_pedal > DEBOUNCE_DELAY && foot_pedal_ready)
      {
        last_foot_pedal = millis();
        foot_pedal_ready = false;
        currentState = IDLE;
        noInterrupts();
        OCR1A = 0;
        interrupts();
      }
      if(millis() - exposure_start_time >= exposure_duration)
      {
        currentState = IDLE;
        noInterrupts();
        OCR1A = 0;
        interrupts();
      }
      break;

    case SCROLL_MODE:
      currentSelection = static_cast<EditSelection>((encoder.readAndReset() % 3 + 3) % 3);
      
      if(!digitalRead(pin_pushbutton) && millis() - last_pushbutton > DEBOUNCE_DELAY && pushbutton_ready)
      {
        last_pushbutton = millis();
        pushbutton_ready = false;
        switch (currentSelection)
        {
          case DURATION:
            encoder.write(0);
            currentState = EDIT_DURATION;
            break;
          case POWER:
            encoder.write(0);
            currentState = EDIT_POWER;
            break;
          case RETURN:
            currentState = IDLE;
            break;
        }
      }
      break;

    case EDIT_DURATION:
      exposure_duration += encoder.readAndReset() * ENCODER_STEP_DURATION;
      exposure_duration = constrain(exposure_duration, MIN_EXPOSURE_DURATION, MAX_EXPOSURE_DURATION);
      if(!digitalRead(pin_pushbutton) && millis() - last_pushbutton > DEBOUNCE_DELAY && pushbutton_ready)
      {
        pushbutton_ready = false;
        last_pushbutton = millis();
        currentState = SCROLL_MODE;
      }
      break;

    case EDIT_POWER:
      dimmer_value += encoder.readAndReset() * ENCODER_STEP_POWER;
      dimmer_value = constrain(dimmer_value, MIN_DIMMER_VALUE, MAX_DIMMER_VALUE);
      if(!digitalRead(pin_pushbutton) && millis() - last_pushbutton > DEBOUNCE_DELAY && pushbutton_ready)
      {
        pushbutton_ready = false;
        last_pushbutton = millis();
        currentState = SCROLL_MODE;
      }
      break;
  }
  update_display();
}

void update_display()
{
  static long int last_display_update = millis();
  if(millis() - last_display_update < DISPLAY_UPDATE_INTERVAL) return;
  last_display_update = millis();

  display.clearDisplay();
  display.setTextColor(WHITE);
  
  char buf[32];

  switch (currentState)
  {
    case IDLE:
      display.setTextSize(4);
      display.setCursor(16, 2);
      display.print("Idle");
      display_power_and_duration();
      break;

    case EXPOSURE:
      display.setTextSize(4);
      display.setCursor(4, 2);
      display.print("UV ON");
      display.setCursor(28, 34);
      snprintf(buf, sizeof(buf), "%3lu", (exposure_start_time + exposure_duration - millis()) / 1000);
      display.print(buf);
      break;

    case SCROLL_MODE:
      display.setTextSize(4);
      display.setCursor(16, 2);
      display.print("Edit");
      display_power_and_duration();

      switch(currentSelection)
      {
        case DURATION:
          display.setCursor(3, 54);
          display.write(0x10);
          break;

        case POWER:
          display.setCursor(3, 42);
          display.write(0x10);
          break;

        case RETURN:
          display.setCursor(3, 8);
          display.setTextSize(2);
          display.write(0x1B);
          break;
      }
      break;

    case EDIT_DURATION:
      display.setTextSize(4);
      display.setCursor(16, 2);
      display.print("Edit");
      display.setTextSize(1);
      display.setCursor(3, 54);
      display.write(0xAF);
      display_power_and_duration();
      break;

    case EDIT_POWER:
      display.setTextSize(4);
      display.setCursor(16, 2);
      display.print("Edit");
      display.setTextSize(1);
      display.setCursor(3, 42);
      display.write(0xAF);
      display_power_and_duration();
      break;
  } 
  display.display();
}


void display_power_and_duration()
{
  display.setTextSize(1);
  display.setCursor(0, 0);
  char buffer[32];
  // Calculate the decimal part of the power (tenths of a watt)
  unsigned int power_whole = dimmer_value / DIMMER_PER_WATT;
  unsigned int power_decimal = (dimmer_value % DIMMER_PER_WATT) * 10 / DIMMER_PER_WATT;
  snprintf(buffer, sizeof(buffer), "Power:    %2u.%u W", power_whole, power_decimal);
  display.print(buffer);
  display.setCursor(0, 10);
  snprintf(buffer, sizeof(buffer), "Exposure: %3lu s", exposure_duration / 1000);
  display.print(buffer);
}