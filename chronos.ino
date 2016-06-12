/*
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <TimerOne.h>
#include <EEPROM.h>

#define MAX_PRESETS 128
#define SLAVE_MASK 0x8000
#define SWAP_MASK 0x4000
#define TIME_MASK 0x3FFF

#define PRESET_CHANGE_MASK 0xC0
#define DATA_BYTE_MASK 0x7F

#define CLOCK_TICK 0xF8
#define CLOCK_START 0xFA

#define TICKS_PER_SIXTEENTH 6
#define TICKS_PER_EIGHTH_TRIPLET 8
#define TICKS_PER_EIGHTH 12
#define TICKS_PER_QUARTER_TRIPLET 16
#define TICKS_PER_DOTTED_EIGHTH 18
#define TICKS_PER_QUARTER 24
#define TICKS_PER_DOTTED_QUARTER 36
#define TICKS_PER_HALF 48
#define TICKS_PER_DOTTED_HALF 72
#define TICKS_PER_WHOLE 96
#define TICKS_PER_DOTTED_WHOLE 144
#define TICKS_PER_2_WHOLE 192
#define TICKS_PER_3_WHOLE 288
#define TICKS_PER_4_WHOLE 384

#define MIN_DELAY_MS 20

#define HIGH_LEVEL_MS 10

#define SLAVE_BTN 2
#define SAVE_BTN 3
#define SLAVE_LED 4
#define SWAP_BTN 5
#define SWAP_LED 6
#define GATE_OUT 13
#define MIDI_CLK 13
#define GATE_POT A0

#define TICKS_DIV 78 //1023 / 13

#define IS_PRESET_SLAVE() get_preset_bit(SLAVE_MASK)
#define IS_PRESET_SWAP() get_preset_bit(SWAP_MASK)
#define CHANGE_PRESET_SLAVE() change_preset_bit(SLAVE_MASK)
#define CHANGE_PRESET_SWAP() change_preset_bit(SWAP_MASK)

byte tick_ops[] = {
  TICKS_PER_SIXTEENTH,
  TICKS_PER_EIGHTH_TRIPLET,
  TICKS_PER_EIGHTH,
  TICKS_PER_QUARTER_TRIPLET,
  TICKS_PER_DOTTED_EIGHTH,
  TICKS_PER_QUARTER,
  TICKS_PER_DOTTED_QUARTER,
  TICKS_PER_HALF,
  TICKS_PER_DOTTED_HALF,
  TICKS_PER_WHOLE,
  TICKS_PER_DOTTED_WHOLE,
  TICKS_PER_2_WHOLE,
  TICKS_PER_3_WHOLE,
  TICKS_PER_4_WHOLE
};

volatile unsigned short presets[MAX_PRESETS];
// if slave this represents midi time ticks; otherwise ms
volatile unsigned int ticks = 0;
volatile byte current_preset = 0;
int last_value = 0;
int buttonSlaveStatus = LOW;
int buttonSaveStatus = LOW;
int buttonSwapStatus = LOW;
boolean preset_msg = false;
boolean gate_on = false;
unsigned long time_last_gate_rise;

void setup() {
  // Setting MIDI baud rate
  Serial1.begin(31250);
  // Setting pins
  pinMode(SLAVE_LED, OUTPUT);
  pinMode(SWAP_LED, OUTPUT);
  pinMode(GATE_OUT, OUTPUT);
  pinMode(SLAVE_BTN, INPUT);
  pinMode(SWAP_BTN, INPUT);
  pinMode(SAVE_BTN, INPUT);
  // Reading tempo knob
  last_value = analogRead(GATE_POT);
  // Reading presets from EEPROM
  for (int i = 0; i < MAX_PRESETS; i++) {
    int preset_addr = i * 2;
    byte msb = EEPROM.read(preset_addr);
    byte lsb = EEPROM.read(preset_addr + 1);
    presets[i] = word(msb, lsb);
    if (presets[i] == 0) {
      presets[i] += MIN_DELAY_MS;
    }
  }
  digitalWrite(SLAVE_LED, IS_PRESET_SLAVE());
  digitalWrite(SWAP_LED, IS_PRESET_SWAP());
  // Setting preset values
  update_time(get_preset_time());
  reset_ticks();
  // Setting ms handler
  Timer1.initialize(1000);
  Timer1.attachInterrupt(processTickMs);
}

void processTickMs(void) {
  if (!IS_PRESET_SLAVE()) {
    ticks++;
  }
}

void loop() {
  read_gate_pot();
  read_slave_button();
  read_swap_button();
  read_save_button();
  read_midi_msg();
  check_time_if_free();
  check_and_pull_tap_tempo();
}

void check_time_if_free() {
  if (!IS_PRESET_SLAVE()) {
    noInterrupts();
    if (ticks >= get_preset_time()) {
      push_tap_tempo();
      ticks = 0;
    }
    interrupts();
  }
}

boolean get_preset_bit(int mask) {
  return (presets[current_preset] & mask) == mask;
}

void change_preset_bit(int mask) {
    presets[current_preset] ^= mask;
}

int get_preset_time() {
  return presets[current_preset] & TIME_MASK;
}

void set_time(short value) {
  presets[current_preset] = (value & TIME_MASK) | (presets[current_preset] & SLAVE_MASK);
}

void read_gate_pot() {
  int value = analogRead(GATE_POT);
  if (value != last_value) {
    last_value = value;
    update_time(last_value);
  }
}

void update_time(int value) {
  int p_value;
  if (IS_PRESET_SLAVE()) {
    //103 > 1024 / 10
    p_value = tick_ops[value / TICKS_DIV];
  }
  else {
    //We use this value as ms.
    p_value = get_ms(value);
  }
  set_time(p_value);
}

boolean pushed_button(int buttonId, int * lastStatus) {
  boolean pushed = false;
  int currentStatus = digitalRead(buttonId);
  if (currentStatus != *lastStatus) {
    if (currentStatus == HIGH) {
      pushed = true;
    }
  }
  *lastStatus = currentStatus;
  return pushed;
}

void read_slave_button() {
  if (pushed_button(SLAVE_BTN, &buttonSlaveStatus)) {
    Serial.println("Changing mode...");
    CHANGE_PRESET_SLAVE();
    digitalWrite(SLAVE_LED, IS_PRESET_SLAVE());
    update_time(last_value);
    reset_ticks();
  }
}

void read_swap_button() {
  if (pushed_button(SWAP_BTN, &buttonSwapStatus)) {
    Serial.println("Changing swap...");
    CHANGE_PRESET_SWAP();
    digitalWrite(SWAP_LED, IS_PRESET_SWAP());
  }
}

void read_save_button() {
  if (pushed_button(SAVE_BTN, &buttonSaveStatus)) {
    Serial.print("Saving preset ");
    Serial.println(current_preset);
    byte msb = highByte(presets[current_preset]);
    byte lsb = lowByte(presets[current_preset]);
    int preset_addr = current_preset * 2;
    //TODO: check if we need to save
    EEPROM.write(preset_addr, msb);
    EEPROM.write(preset_addr + 1, lsb);
  }
}

void read_midi_msg() {
  if (Serial1.available() > 0) {
    int input = Serial1.read();
    if (input == CLOCK_START) {
      Serial.println("Clock start");
      if (IS_PRESET_SLAVE()) {
        reset_ticks();
      }
    }
    else if (input == CLOCK_TICK) {
      //Serial.println("Tick!");
      if (IS_PRESET_SLAVE()) {
        ticks++;
        if (ticks >= get_preset_time()) {
          ticks = 0;
          push_tap_tempo();
        }
      }
    }
    else if ((input & PRESET_CHANGE_MASK) == PRESET_CHANGE_MASK) {
      preset_msg = true;
    }
    else if (preset_msg) {
      current_preset = input;
      Serial.println("Changing preset");
      digitalWrite(SLAVE_LED, IS_PRESET_SLAVE());
      digitalWrite(SWAP_LED, IS_PRESET_SWAP());
      preset_msg = false;
      reset_ticks();
    }
    else {
      Serial.println(input, BIN);
    }
  }
}

void push_tap_tempo() {
  gate_on = true;
  digitalWrite(GATE_OUT, HIGH);
  time_last_gate_rise = millis();
}

void check_and_pull_tap_tempo() {
  unsigned long current_time = millis();
  if (gate_on && current_time >= (time_last_gate_rise + HIGH_LEVEL_MS)) {
    gate_on = false;
    digitalWrite(GATE_OUT, LOW);
  }
}

void reset_ticks() {
  noInterrupts();
  ticks = get_preset_time() - 1;
  interrupts();
}

unsigned short get_ms(unsigned short value) {
  unsigned short p_value;
  if (value < 512) {
    p_value = value;
    p_value = p_value < MIN_DELAY_MS ? MIN_DELAY_MS : p_value;
  }
  else if (value < 640) {
    p_value = value << 1;
  }
  else if (value < 768) {
    p_value = value << 2;
  }
  else if (value < 896) {
    p_value = value << 3;
  }
  else {
    p_value = value << 4;
  }
  return p_value;
}
