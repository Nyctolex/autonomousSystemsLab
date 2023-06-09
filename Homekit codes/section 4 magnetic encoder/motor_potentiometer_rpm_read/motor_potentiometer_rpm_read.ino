// Encoder Example

// Define Pins

#define ENCODER_PINA 2
#define ENCODER_PINB 3
#define RED_LED_PIN 9
#define BLUE_LED_PIN 11
#define GREEN_LED_PIN 10
#define CLOCKWISE_PIN 5
#define COUNTERCLOCKWISE_PIN 6
#include "src/mapper/mapper.h"
#include "src/motor/motor.h"
#include "src/motor//ledHandler.h"

class MagneticEncoder
{
public:
    static volatile bool switchChanged; // declare
    static volatile int encoderCounts;
    static volatile int encoderPreviousCount;
    static volatile unsigned long previous_millis;
    static volatile unsigned long current_millis;
    void attach(int _encoder_pinA, int _encoder_pinB)
    {
        // initialize encoder, attache ISR functions
        pinMode(_encoder_pinA, INPUT);
        pinMode(_encoder_pinB, INPUT);
    }
    void begin()
    {
        encoderCounts = 0;
        // Attached interrupt to encoder pins
        attachInterrupt(digitalPinToInterrupt(ENCODER_PINA), encoderA, CHANGE);
        attachInterrupt(digitalPinToInterrupt(ENCODER_PINB), encoderB, CHANGE);
    }

    static float rpm()
    {
        // return the rpm since last call
        current_millis = millis();
        unsigned long time_delta_millisec = current_millis - previous_millis;
        float rotation_delta = float(encoderCounts - encoderPreviousCount) / 360;
        float time_delta_minues = time_delta_millisec / 60000.0;
        float rpm = (float)rotation_delta / time_delta_minues;
        encoderPreviousCount = encoderCounts;
        previous_millis = millis();
        return rpm;
    }

    // Encoder ISR functions - Interupt Service Routine
    // EncoderA ISR
    static void encoderA()
    {
        // look for a low-to-high on channel B
        if (digitalRead(ENCODER_PINA) == HIGH)
        {
            // check channel A to see which way encoder is turning
            digitalRead(ENCODER_PINB) ? encoderCounts++ : encoderCounts--;
        }
        else
        {
            // check channel A to see which way encoder is turning
            digitalRead(ENCODER_PINB) ? encoderCounts-- : encoderCounts++;
        }

    } // End EncoderA ISR
    // EncoderB ISR
    static void encoderB()
    {
        // look for a low-to-high on channel B

        if (digitalRead(ENCODER_PINB) == HIGH)
        {
            // check channel A to see which way encoder is turning
            digitalRead(ENCODER_PINA) ? encoderCounts-- : encoderCounts++;
        }
        else
        {
            // check channel A to see which way encoder is turning
            digitalRead(ENCODER_PINA) ? encoderCounts++ : encoderCounts--;
        }
    } // End EncoderB ISR
};


class SensorHandler
{
public:
  static const int sensorMinValue = 0;
  static const int sensorMaxValue = 1023;
  static const int speendMinValue = 0;
  static const int speedMaxValue = 255;
  int scaleSize = sensorMaxValue - sensorMinValue;
  int middleStartThreshole, middleEndThreshole;
  Map *SpeedScaler;
  byte sensorPin;
  SensorHandler(byte sensor_pin, float middleThreshole)
  {
    float middleValue = (sensorMinValue + sensorMaxValue) / 2;
    middleStartThreshole = (int)((float)middleValue - (float)scaleSize * middleThreshole / 2);
    middleEndThreshole = (int)((float)middleValue + (float)scaleSize * middleThreshole / 2);
    int maxValidScaleValue = max(middleStartThreshole, this->sensorMaxValue - middleEndThreshole);
    this->sensorPin = sensor_pin;
    SpeedScaler = new Map(sensorMinValue, maxValidScaleValue, speendMinValue, speedMaxValue);
  }
  int get_speed(){
    int sensorValue = analogRead(this->sensorPin);
    int unscaled_speed;
    if (sensorValue < middleStartThreshole){
      unscaled_speed = middleStartThreshole - sensorValue;
    } else if (sensorValue > middleEndThreshole){
      unscaled_speed = sensorValue - middleEndThreshole;
    } else {
      unscaled_speed = 0;
    }
    int scale_speed = SpeedScaler->map_value(unscaled_speed);
    return scale_speed;
  }
  int get_state()
  {
    int sensorValue = analogRead(A0);
    if (sensorValue < middleStartThreshole)
    {
      return State::forward;
    }
    else if (sensorValue > middleEndThreshole)
    {
      return State::backward;
    }
    else
    {
      return State::coast;
    }

  }
};


// define all class varibles
volatile bool MagneticEncoder::switchChanged;
volatile int MagneticEncoder::encoderCounts;
volatile int MagneticEncoder::encoderPreviousCount;
volatile unsigned long MagneticEncoder::previous_millis;
volatile unsigned long MagneticEncoder::current_millis;
MagneticEncoder magneticSensor; // make an instance of myClass

Motor *motor;
LedHandler *leds;
SensorHandler *sensor;
void setup()
{
    motor = new Motor(CLOCKWISE_PIN, COUNTERCLOCKWISE_PIN);
    motor->update(State::coast, 0);
    leds = new LedHandler(BLUE_LED_PIN, RED_LED_PIN,GREEN_LED_PIN );
    sensor = new SensorHandler(A0, 0.3);

    // initialize serial communication at 115200 bits per second:

    Serial.begin(115200);
    magneticSensor.attach(ENCODER_PINA, ENCODER_PINB);
    magneticSensor.begin();
}

// the loop routine runs over and over again forever:
void loop()
{
    int speed = sensor->get_speed();
    int state = sensor->get_state();
    motor->update(state, speed);
    leds->update(state);
    Serial.print("RPM:");
    Serial.println(magneticSensor.rpm());
    delay(100);// delay in between reads for stability
}
