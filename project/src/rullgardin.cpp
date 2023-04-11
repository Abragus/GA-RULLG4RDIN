#ifndef RULLGARDIN_cpp
#define RULLGARDIN_cpp

#include "rullgardin.h"

extern MultiLogger MultiLog;
#define DEBUG 0

// Rullgardin(uint8_t motorInterfaceType, uint8_t stepPin, int8_t dirPin, uint8_t enablePin, uint8_t resetPin) {
Rullgardin::Rullgardin() {
    // Create a new instance of the AccelStepper class
    motor = AccelStepper(MOTOR_INTERFACE_TYPE, STEP_PIN, DIR_PIN);

    // Set the maximum motor speed in steps per second
    motor.setMaxSpeed(1600);
    motor.setEnablePin(ENABLE_PIN);
    motor.setAcceleration(6400);
    motor.disableOutputs();
    
    pinMode(RESET_PIN,OUTPUT);
    digitalWrite(RESET_PIN, HIGH);
}

bool Rullgardin::isRunning() { return running; }

bool Rullgardin::set_up_direction(int8_t direction) {
    if (direction == 1 || direction == -1) {
        up_direction = direction;
        down_direction = direction * -1;
        return true;
    }
    return false;
}

bool Rullgardin::run() {
    if (motor.isRunning()) {
        // #if DEBUG
        //     static int loops_since_running_logged = 0;
        //     if (loops_since_running_logged >= 500) {
        //         MultiLog.println("Running: " + String(running) + ", speed: " + String(speed) + ", direction: " + String(current_direction) + ", position: " + String(motor.currentPosition()));
        //     } else {
        //         loops_since_running_logged++;
        //     }
        // #endif
        // motor.setSpeed(speed * current_direction);

        motor.enableOutputs();

        if (!motor.run()) {
            stop();
            MultiLog.println("Motor reached target position: " + String(motor.currentPosition()));
        }

        return true;
    } else {
        motor.disableOutputs();
        return false;
    }
}

void Rullgardin::stop() {
    motor.stop();
    #if DEBUG
        MultiLog.println("Motor stopped at position: " + String(motor.currentPosition()));
    #endif
    running = false;
}

void Rullgardin::open() {
    if (running && current_direction != up_direction) {
        stop();
    }
    // motor.enableOutputs();
    current_direction = up_direction;
    running = true;
    #if DEBUG
        MultiLog.println("Starting motor in open()");
    #endif
    move_to_position(100);
}

void Rullgardin::close() {
    if (running && current_direction == up_direction) {
        stop();
    }
    // motor.enableOutputs();
    current_direction = down_direction;
    running = true;
    #if DEBUG
        MultiLog.println("Starting motor in close()");
    #endif
    move_to_position(0);
}

int Rullgardin::get_position() {
    return 100 - motor.currentPosition() * 100 / max_steps;
}

void Rullgardin::move_to_position(uint8_t in_position) {
    in_position = (in_position > 100 ? 100 : in_position < 0 ? 0 : in_position); // Make sure value in range

    int target_position_raw = (100-in_position) * max_steps / 100;
    // motor.enableOutputs();
    motor.moveTo(target_position_raw);
    #if DEBUG    
        MultiLog.println("Running from position: " + String(motor.currentPosition()) + ", to position: " + String(target_position_raw));
        MultiLog.println("Starting motor in move_to_position()");
    #endif
    running = true;
}

void Rullgardin::set_current_position_as_top() {
    max_steps -= motor.currentPosition();
    motor.setCurrentPosition(0);
}

void Rullgardin::set_current_position_as_bottom() {
    max_steps = (motor.currentPosition() < theoretical_max_steps ? motor.currentPosition() : theoretical_max_steps); // Minimum
}

void Rullgardin::remove_bottom_position_limit() {
    max_steps = theoretical_max_steps;
}

uint16_t Rullgardin::get_speed() {
    return abs(motor.maxSpeed());
}

bool Rullgardin::set_speed(uint16_t new_speed) {
    #if DEBUG
        MultiLog.println("speed: " + String(speed) + ", new_speed: " + String(new_speed));
    #endif

    if (motor.maxSpeed() != new_speed && 0 < new_speed && new_speed <= hard_max_speed) {
        motor.setMaxSpeed(new_speed);
        return true;
    } 
    else 
        return false;
}

uint16_t Rullgardin::get_max_steps() {
    return max_steps;
}
#endif