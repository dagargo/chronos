# Chronos
Chronos is an Arduino sketch to generate tap tempo signals and synchronize them to MIDI clock.

## Dependencies
- TimerOne

## Description
Chronos has two working modes.
- In slave mode, it generates the tap tempo signal by synchronizing it to MIDI input. The tempo knob acts as a multiplier, which goes from sixteenth to 4 bars.
- In free mode, it generates the tap tempo signal based on an internal clock. In this case, the knob acts as a timer, which goes from 20ms to more than 16 seconds.

It offers 128 presets selectable by MIDI preset change messages, which store all the configuration.

Additionally, it has a feature called swap to activate another pin, which is also stored in the presets and has no particular intention.
