/*
  Odometry && Gyro Integration with teleoperate
*/
#include <Wire.h>
#include <Zumo32U4.h>
#include <math.h>

// zumo classes
Zumo32U4Encoders encoders;
Zumo32U4Motors motors;
Zumo32U4IMU imu;

// time variables
#define SAMPLERATE 10 // 5 millis =  200 Hz
// Odometry settings
#define GEAR_RATIO 75                                                    // Motor gear ratio 100.37
#define WHEELS_DISTANCE 98                                               // Distance between tracks
#define WHEEL_DIAMETER 37.5                                              // Wheels diameter measured 38.5
#define ENCODER_PPR 12                                                   // Encoder pulses per revolution
#define GYRO_SCALE 360 / 50829.33                                         // 70 mdps/LSB
float encoder2dist = WHEEL_DIAMETER * 3.14 / (ENCODER_PPR * GEAR_RATIO); // conversition of encoder pulses to distance in mm

class PIDController
{
private:
    float kp;                  // Proportional gain
    float ki;                  // Integral gain
    float kd;                  // Derivative gain
    float Ts;                  // Sampling time
    float integral;            // Integral accumulator
    float prev_error;          // Error from previous iteration
    float prev_derivative;     // Derivative from previous iteration
    float derivative;          // Current derivative value
    float alpha;               // Filter coefficient
    float filtered_derivative; // Filtered derivative value
    unsigned long last_time;

public:
    // Constructor
    PIDController(float Kp, float Ki, float Kd, float Alpha)
    {
        this->kp = Kp;
        this->ki = Ki;
        this->kd = Kd;
        this->Ts = 0;
        this->alpha = Alpha;
        this->integral = 0.0;
        this->prev_error = 0.0;
        this->prev_derivative = 0.0;
        this->derivative = 0.0;
        this->filtered_derivative = 0.0;
        this->last_time = 0;
    }

    // PID control function
    float pid_control(float setpoint, float process_variable)
    {

        // Calculate time since last sample
        unsigned long current_time = millis();
        float dt = (current_time - last_time) / 1000.0; // convert to seconds
        this->last_time = current_time;

        // Update Ts
        this->Ts = dt;

        // Calculate error
        float error = setpoint - process_variable;

        // Calculate integral term
        this->integral += error * this->Ts;

        // Calculate derivative term with FIR filter
        this->derivative = (error - this->prev_error) / this->Ts;
        this->filtered_derivative = this->alpha * this->derivative + (1 - this->alpha) * this->prev_derivative;

        // Calculate control signal
        float control_signal = this->kp * error + this->ki * this->integral + this->kd * this->filtered_derivative;

        // Store previous error and derivative for next iteration
        this->prev_error = error;
        this->prev_derivative = this->filtered_derivative;

        return control_signal;
    }
};

class GyroHandler
{
private:
    // imu Fusion
    float gyroAngle = 0;
    int32_t gyroOffset_z = -16;
    float gyroz = 0;
    unsigned long lastMillis = 0;
    unsigned long lastMicros = 0;

public:
    float dt_time;

    GyroHandler()
    {
        if (!imu.init())
        {
            // Failed to detect the compass.
            ledRed(1);

                Serial.println(F("Failed to initialize IMU sensors."));
                delay(100);
        }

        imu.enableDefault();
        //        imu.configureForTurnSensing();

        this->gyroAngle = 0;
        //        this->gyroOffset_z = -16;
        this->gyroz = 0;
        // take time stamp
        this->lastMillis = millis();
        this->lastMicros = micros();
        this->gyroOffset();
        this->angleOffset();
    }

    // gyro calibration
    void gyroOffset()
    {
        delay(1); // delay before starting gyro readings for offset
        int32_t total = 0;
        for (uint16_t i = 0; i < 1024; i++)
        {
            // Wait for new data to be available, then read it.
            while (!imu.gyroDataReady())
            {
            }
            imu.readGyro();

            // Add the Z axis reading to the total.
            total += imu.g.z;
        }
        this->gyroOffset_z = total / 1024;
    }

    // gyro calibration
    void angleOffset()
    {
        delay(1); // delay before starting gyro readings for offset
        float total = 0;
        for (uint16_t i = 0; i < 1024; i++)
        {
            total += this->gyroIntegration(true);
        }
        float angleOffset_z = total / 1024;
        this->gyroAngle -= angleOffset_z;
    }

    // gyroIntegration
    float gyroIntegration(bool motorsState)
    {
        //(90.0/1283.0 / 717.0)*90
        this->update_dt();
        imu.readGyro();
        this->gyroz = ((float)(imu.g.z - (float)this->gyroOffset_z)) * GYRO_SCALE;
        if (motorsState)
            this->gyroAngle += (float)(this->gyroz * this->dt_time); // integrate when in motion
        return this->gyroAngle;
    }

    void update_dt()
    {
        this->lastMillis = millis();
        // calculate dt sample
        unsigned long dtMicros = micros() - lastMicros;
        this->lastMicros = micros();
        this->dt_time = float(dtMicros) / 1000000.0;
    }
};

class OdometryHandler
{
public:
    float theta;
    float posx;
    float posy;
    bool external_theta;
    OdometryHandler(bool external_theta = true)
    {
        this->theta = 0;
        this->posx = 0;
        this->posy = 0;
        this->external_theta = external_theta;
    }

    void odometry(bool motorsState, float new_theta = 0)
    {
        // encoder read
        int16_t countsLeft = encoders.getCountsAndResetLeft();
        int16_t countsRight = encoders.getCountsAndResetRight();
        float dx_1 = countsRight * encoder2dist;
        float dx_2 = countsLeft * encoder2dist;
        float d_theta;
        if (this->external_theta)
        {
            d_theta = new_theta - this->theta;
        }
        else
        {
            d_theta = float(dx_1 - dx_2) / WHEELS_DISTANCE;
        }

        this->posx += cos(theta + d_theta / 2) * (dx_1 + dx_2) / 2;
        this->posy += sin(theta + d_theta / 2) * (dx_1 + dx_2) / 2;
        this->theta += d_theta;
    }
};

class PositionHandler
{
private:
    float theta;
    float posx;
    float posy;
    bool external_theta;
    OdometryHandler *odometryHandler;
    GyroHandler *gyroHandler;

public:
    PositionHandler(bool external_theta = true)
    {
        this->theta = 0;
        this->posx = 0;
        this->posy = 0;
        this->external_theta = external_theta;
        this->gyroHandler = new GyroHandler();
        this->odometryHandler = new OdometryHandler();
    }

    void update(bool motorsState)
    {
        this->theta = gyroHandler->gyroIntegration(motorsState);
        if (this->external_theta)
        {
            odometryHandler->odometry(motorsState, this->theta);
        }
        else
        {
            odometryHandler->odometry(motorsState);
        }
        this->posx = this->odometryHandler->posx;
        this->posy = this->odometryHandler->posy;
    }

    float getx(bool motorsState)
    {
        this->update(motorsState);
        return this->posx;
    }

    float gety(bool motorsState)
    {
        this->update(motorsState);
        return this->posy;
    }
    float getTheta(bool motorsState)
    {
        this->update(motorsState);
        return this->theta;
    }
};

enum rotateAxis
{
    middle = 0,
    right = 1,
    left = 2
};

class WheelsSpeed
{
public:
    float right, left;

public:
    WheelsSpeed(float rigt, float left)
    {
        this->right = right;
        this->left = left;
    }
};



void move_forward(PositionHandler *positionHandler, float distance)
{
    // float Ts = 0.01;
    // float distance_control_signal = 0.0; // Initialize the control signal to 0
    // float kp = 0.1;                      // Proportional gain
    // float ki = 0.01;                     // Integral gain
    // float kd = 0.0;                      // Derivative gain
    // float integral = 0.0;                // Integral accumulator
    // float alpha = 0.1;                   // Filter coefficient
    // float desiredDistance = distance;
    // // PIDController distance_pid_controller(0.1, 0.01, 0, 0.1);
    // int rightMotor = 0;
    // int leftMotor = 0;
    // bool motorsState = false;
    // Vector2D current_position(positionHandler->getx(motorsState), positionHandler->gety(motorsState));
    // float initialAngle = positionHandler->getTheta(motorsState);
    // Vector2D distanceVector(sin(initialAngle), cos(initialAngle));
    // distanceVector = distanceVector * distance;

    // Vector2D desired_position = current_position + distanceVector;
    // distance = (desired_position - current_position).norm();
    // float stime = millis();
    // while (distance < 10)
    // {
    //     if (millis()- stime > 10*1000){
    //                 break;
    //     }
    //     motorsState = (leftMotor || rightMotor) == 0 ? 0 : 1;
    //     distance = (desired_position - current_position).norm();
    //     distance_control_signal = distance_pid_controller.pid_control(desiredDistance, distance);
    //     leftMotor = (int)distance_control_signal / 2;
    //     rightMotor = (int)distance_control_signal / 2;

    //     motors.setLeftSpeed(leftMotor);
    //     motors.setRightSpeed(rightMotor);
    //     delay(Ts);
    // }
}





/// Start ///
PositionHandler * positionHandler;
PIDController distance_pid_controller(0.1, 0.01, 0, 0.1);
PIDController angle_pid_controller(3, 0.2, 0, 0.1);
PIDController velocity_pid_controller(400, 0.1, 0, 0.1);
bool motorsState;
float gyroAngle, angle_control_signal, actual_velocity, Ts, velocity_control_signal;
int axis = middle;
float desired_angle = 90;
int leftMotor, rightMotor;
void setup()
{
    Wire.begin();
    // initialize serial:
    Serial.begin(9600);
    positionHandler = new PositionHandler(true);
    leftMotor = 0;
    rightMotor=0;
    Ts = 0.01;
    // move_forward(&positionHandler, 10);
}

void loop()
{
    motorsState = (leftMotor || rightMotor) == 0 ? 0 : 1;
    gyroAngle = positionHandler->getTheta(motorsState);
    //Serial.println(gyroAngle);

    int16_t leftCount = encoders.getCountsAndResetLeft();
    int16_t RightCount = encoders.getCountsAndResetRight();
    actual_velocity = ((leftCount + RightCount) / 2) * encoder2dist / Ts;
    velocity_control_signal = velocity_pid_controller.pid_control(actual_velocity, 0.05);
    angle_control_signal = angle_pid_controller.pid_control(desired_angle, gyroAngle);

    switch (axis)
        {
        case right:
            leftMotor = 0;
            rightMotor = (int)angle_control_signal;
            break;
        case left:
            // Apply the control signal to the motors
            leftMotor = (int)-1 * angle_control_signal;
            rightMotor = 0;

            break;

        default:
            // Apply the control signal to the motors
            leftMotor = (int)-1 * angle_control_signal / 2;
            rightMotor = (int)angle_control_signal / 2;
            break;
        }
        motors.setLeftSpeed(velocity_control_signal);
        motors.setRightSpeed(velocity_control_signal);

    // turn_degrees(positionHandler, 90, middle);
    // move_forward(positionHandler, 1000);
    Serial.println(gyroAngle);
    Serial.println("In main loop");
    delay(Ts*1000);
}
