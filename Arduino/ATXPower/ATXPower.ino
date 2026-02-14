/*
 * Copyright (C) 2026 Rhys Weatherley
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

// Pin assignment.
#define STATUS_LED  13      // Status LED on the Arduino.
#define POWER_LED   A0      // Output, active-high, turn on the power LED.
#define POWER_BTN   A1      // Input, active-low, power button.
#define RESET_CTRL  A2      // Output, active-high, hold computer in reset.
#define RESET_BTN   A3      // Input, active-low, reset button.
#define POWER_GOOD  8       // Input, active-low, power supply is good.
#define POWER_ON    9       // Output, active-high, turn on power supply.

// Debounce timeouts for the power button, in milliseconds.
#define POWER_BTN_ON_TIME   100     // Press for 100ms to turn the system on.
#define POWER_BTN_OFF_TIME  1000    // Hold for 1s to turn the system off.

// Minimum time to hold the computer in reset when reset button pressed.
#define RESET_TIME          500

// Time to wait for the power good signal to go high before concluding
// that the power supply is not working correctly.
#define POWER_UP_TIME       5000

// States for the state machine.
typedef enum
{
    STATE_OFF,          // Computer is turned off.
    STATE_WAIT_ON,      // Power button pressed to turn on, wait for release.
    STATE_POWERING_UP,  // Power supply is turning on, computer held in reset.
    STATE_NORMAL,       // Computer is running normally.
    STATE_WAIT_OFF,     // Power button pressed to turn off, wait for release.
    STATE_WAIT_OFF2,    // Second stage power off, wait to lower reset line.
    STATE_WAIT_RESET,   // Reset button is pressed, wait for release.
    STATE_RESET         // Reset the computer due to the reset button.

} State_t;

State_t state = STATE_OFF;

class ButtonDebouncer
{
public:
    ButtonDebouncer(int pin);

    void run();
    bool isPressed() const { return pressed; }
    void setTimeout(unsigned long ms) { timeout = ms; }

private:
    int pinnum;
    bool rawPressed;
    bool pressed;
    bool timerRunning;
    unsigned long timer;
    unsigned long timeout;
};

ButtonDebouncer::ButtonDebouncer(int pin)
    : pinnum(pin)
    , rawPressed(false)
    , pressed(false)
    , timerRunning(false)
    , timer(0)
    , timeout(50)
{
}

void ButtonDebouncer::run()
{
    bool nowPressed = (digitalRead(pinnum) == LOW);
    if (nowPressed != rawPressed) {
        // Change in the raw state of the button.  Restart the timer.
        timer = millis();
        timerRunning = true;
        rawPressed = nowPressed;
    } else if (timerRunning) {
        if ((millis() - timer) >= timeout) {
            // Debounce time has elapsed.  Update the pressed state.
            pressed = nowPressed;
            timerRunning = false;
        }
    }
}

ButtonDebouncer powerButton(POWER_BTN);
ButtonDebouncer resetButton(RESET_BTN);
ButtonDebouncer powerGood(POWER_GOOD);

void setup()
{
    // Initialise the outputs.
    digitalWrite(POWER_LED, LOW);   // Power LED is off at startup.
    digitalWrite(STATUS_LED, LOW);
    digitalWrite(RESET_CTRL, HIGH); // Hold the computer in reset for now.
    digitalWrite(POWER_ON, LOW);    // Don't turn on the power supply yet.

    // Configure all pins to the desired modes.
    pinMode(STATUS_LED, OUTPUT);
    pinMode(POWER_LED, OUTPUT);
    pinMode(POWER_BTN, INPUT_PULLUP);
    pinMode(RESET_CTRL, OUTPUT);
    pinMode(RESET_BTN, INPUT_PULLUP);
    pinMode(POWER_GOOD, INPUT_PULLUP);
    pinMode(POWER_ON, OUTPUT);

    // Currently in the off state, waiting for the power button to be pressed.
    state = STATE_OFF;
    powerButton.setTimeout(POWER_BTN_ON_TIME);
}

// Shut down the computer immediately.
void shutdown(bool withReset = true)
{
    if (withReset) {
        digitalWrite(RESET_CTRL, HIGH);
    }
    digitalWrite(POWER_ON, LOW);
    digitalWrite(POWER_LED, LOW);
    digitalWrite(STATUS_LED, LOW);
    powerButton.setTimeout(POWER_BTN_ON_TIME);
}

static unsigned long global_timer = 0;
static unsigned long global_timeout = 0;

void startTimeout(unsigned long ms)
{
    global_timer = millis();
    global_timeout = ms;
}

bool isTimedOut()
{
    return (millis() - global_timer) >= global_timeout;
}

void loop()
{
    // Debounce the current state of the inputs.
    powerButton.run();
    resetButton.run();
    powerGood.run();

    // Run the state machine.
    switch (state) {
    case STATE_OFF:
        // Wait for the user to press the power button.
        if (powerButton.isPressed()) {
            state = STATE_WAIT_ON;
        }
        break;

    case STATE_WAIT_ON:
        // Wait for the user to release the power button.
        if (!powerButton.isPressed()) {
            // Turn on the supply, while still holding the computer in reset.
            digitalWrite(POWER_ON, HIGH);
            digitalWrite(POWER_LED, HIGH);
            digitalWrite(STATUS_LED, HIGH);
            powerButton.setTimeout(POWER_BTN_OFF_TIME);
            startTimeout(POWER_UP_TIME);
            state = STATE_POWERING_UP;
        }
        break;

    case STATE_POWERING_UP:
        // Power supply is on, waiting for the power good signal.
        if (powerGood.isPressed()) {
            // Hold the computer in reset for a little longer.
            startTimeout(RESET_TIME);
            state = STATE_RESET;
        } else if (isTimedOut()) {
            // Took too long for power good to come up.  Something went wrong.
            shutdown();
            state = STATE_OFF;
        }
        break;

    case STATE_NORMAL:
        // Normal operations.
        if (powerButton.isPressed()) {
            // User turned the power off.  Shut everything down except reset.
            shutdown(false);
            powerButton.setTimeout(POWER_BTN_ON_TIME);
            state = STATE_WAIT_OFF;
        } else if (resetButton.isPressed()) {
            // Explicit request to reset the attached computer.
            digitalWrite(RESET_CTRL, HIGH);
            state = STATE_WAIT_RESET;
        } else if (!powerGood.isPressed()) {
            // Power good signal from the power supply has gone away.
            // Shut the computer down immediately.
            shutdown();
            state = STATE_OFF;
        }
        break;

    case STATE_WAIT_OFF:
        // Wait for the power button to be released in the off state.
        if (!powerButton.isPressed()) {
            state = STATE_WAIT_OFF2;
            startTimeout(RESET_TIME);
        }
        break;

    case STATE_WAIT_OFF2:
        // After the timeout, forcibly reset the computer and stop.
        // It should have already lost power by now.
        if (isTimedOut()) {
            digitalWrite(RESET_CTRL, HIGH);
            state = STATE_OFF;
        }
        break;

    case STATE_WAIT_RESET:
        // Wait for the reset button to be released.
        if (!resetButton.isPressed()) {
            // Hold the computer in reset for a little longer after release.
            startTimeout(RESET_TIME);
            state = STATE_RESET;
        }
        break;

    case STATE_RESET:
        // Time to release the computer from reset?
        if (isTimedOut()) {
            digitalWrite(RESET_CTRL, LOW);
            state = STATE_NORMAL;
        }
        break;
    }
}
