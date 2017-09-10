/*
File:   main.cpp
Author: J. Ian Lindsay
Date:   2017.08.21

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA


Supported build targets: Teensy3 and Raspi.
*/
#include <Arduino.h>

#include <Platform/Platform.h>
#include <DataStructures/StringBuilder.h>
#include <Platform/Peripherals/I2C/I2CAdapter.h>
#include <Drivers/ADP8866/ADP8866.h>
#include <Drivers/PMIC/BQ24155/BQ24155.h>
#include <Drivers/PMIC/LTC294x/LTC294x.h>

#include "DigitabulumPMU-r2.h"

#include <Transports/ManuvrSerial/ManuvrSerial.h>
#include <XenoSession/Console/ManuvrConsole.h>

#define HOST_BAUD_RATE  115200


const I2CAdapterOptions i2c_opts(
  0,   // Device number
  18, // sda
  19, // scl
  (I2C_ADAPT_OPT_FLAG_SDA_PU | I2C_ADAPT_OPT_FLAG_SCL_PU),
  400000   // 400kHz
);

const ADP8866Pins adp_opts(
  21,  // IO19 (Reset)
  20   // IO18 (IRQ)
);

const BatteryOpts battery_opts (
  2600,    // We will assume a common 18650 for now. 2600mAh capacity.
  3.6f,    // Battery dead (in volts)
  3.75f,   // Battery weak (in volts)
  4.15f,   // Battery float (in volts)
  4.3f     // Battery max (in volts)
);

const LTC294xOpts gas_gauge_opts(
  16,    // IO13 (Alert pin)
  LTC294X_OPT_ACD_AUTO | LTC294X_OPT_INTEG_SENSE
);

const BQ24155Opts charger_opts(
  68,  // Sense resistor is 68 mOhm.
  14,  // STAT
  15   // ISEL
);

const PowerPlantOpts powerplant_opts(
  22,  // 2.5v select pin.
  23,  // Aux regulator enable pin.
  DIGITAB_PMU_FLAG_ENABLED | DIGITAB_PMU_FLAG_V_25  // Regulator enabled @2.5v
);


Kernel* kernel = nullptr;


/*******************************************************************************
* Entry-point for teensy3...                                                   *
*******************************************************************************/

uint8_t analog_write_val = 0;
int8_t direction = 1;   // Zero for hold, 1 for brighten, -1 for darken.

void logo_fade() {
  if (direction < 0) analog_write_val--;
  else if (direction > 0) analog_write_val++;
  else return;

  if (0 == analog_write_val) {
    direction = direction * -1;
  }
  else if (200 == analog_write_val) {
    direction = direction * -1;
  }
  analogWrite(4, analog_write_val);
}



void setup() {
  platform.platformPreInit();
  kernel = platform.kernel();

  analogWriteResolution(12);   // Setup the DAC.

  gpioDefine(13, GPIOMode::OUTPUT);
  gpioDefine(4,  GPIOMode::OUTPUT);
}


void loop() {
  //AudioMemory(2);

  // Setup the first i2c adapter and Subscribe it to Kernel.
  I2CAdapter i2c(&i2c_opts);
  kernel->subscribe((EventReceiver*) &i2c);

  BQ24155 charger(&charger_opts);
  i2c.addSlaveDevice((I2CDeviceWithRegisters*) &charger);

  LTC294x gas_gauge(&gas_gauge_opts, battery_opts.capacity);
  i2c.addSlaveDevice((I2CDeviceWithRegisters*) &gas_gauge);

  PMU pmu(&charger, &gas_gauge, &powerplant_opts, &battery_opts);
  kernel->subscribe((EventReceiver*) &pmu);

  ADP8866 leds(&adp_opts);
  i2c.addSlaveDevice((I2CDeviceWithRegisters*) &leds);
  kernel->subscribe((EventReceiver*) &leds);

  platform.bootstrap();

  kernel->createSchedule(40, -1, false, logo_fade)->enableSchedule(true);

  ManuvrSerial  _console_xport("U", HOST_BAUD_RATE);  // Indicate USB.
  kernel->subscribe((EventReceiver*) &_console_xport);
  ManuvrConsole _console((BufferPipe*) &_console_xport);
  kernel->subscribe((EventReceiver*) &_console);

  while (1) {
    kernel->procIdleFlags();
  }
}


#if defined(__MANUVR_LINUX)
  // For linux builds, we provide a shunt into the loop function.
  int main(int argc, char *argv[]) {
    setup();
    loop();
  }
#endif
