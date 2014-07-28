multitrack
==========

Lightweight multi-track recorder with one or two track recording and stereo mix-down for output.

This project is inspired by the need to run a multitrack recorder in a home recording studio on a budget. It is tested on a Raspberry Pi Model B.
The Raspberry Pi is chosen as a low power, silent device. Files are saved to a USB flash drive and audio is via USB stereo soundcard.

Can record one or two channels of audio whilst playing back any / all tacks, mixed down to stereo. This provides a method of recording but it is intended to perform mixing and mastering in a separate dedicated DAW. Files will be available to import in to another application such as Ardour.

There is a curses user interface, purposefully kept simple. It is intended to add other interfaces such as hardware buttons, MIDI, network, etc.

Key commands:

Up / down arrows - select channel
m - toggle selected channel mute
M - toggle all channels mute (make same as channel 1)
a - toggle record from A (left) input
b - toggle record from B (right) input
L - pan fully left
R - pan fully right
C - pan centre
q - Quit
space - start / stop
home - move playhead to beginning

Compile with: g++ -lncurses -lasound multitrack.cpp -o multitrack
