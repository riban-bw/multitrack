multitrack
==========

Lightweight multi-track audio recorder, Record one or two tracks whilst replaying a stereo mix-down of any / all tracks. Default is to enable 16 tracks but more or fewer may be used.

This project is inspired by the need to run a multitrack recorder in a home recording studio on a small budget. It is tested on a Raspberry Pi Model B.
The Raspberry Pi is chosen as a low power, silent device. Files are saved to a USB flash drive and audio is via USB stereo soundcard.

Can record one or two channels of audio whilst playing back any / all tacks, mixed down to stereo. This provides a method of recording whilst monitoring previously recorded tracks but it is intended to perform mixing and mastering in a separate dedicated DAW. A multichannel WAVE file contains all tracks which may be imported in to another application such as Ardour or Audacity.

There is a ncurses user interface, purposefully kept simple. It is intended to add other interfaces such as hardware buttons, MIDI, network, etc.

Key commands (subject to change):

up / down arrows - select channel
m - toggle selected channel mute
M - toggle selected channel mute and set all channels mute the same
a - toggle record from A (left) input
b - toggle record from B (right) input
L - pan fully left
R - pan fully right
C - pan centre
l - pan fully left and pad to allow distortionless mixdown
r - pan fully right and pad to allow distortionless mixdown
c - pan fully centre and pad to allow distortionless mixdown
e - clear error count
q - Quit
space - start / stop
G - toggle record enable
home - move playhead to beginning
end - move playhead to end

Compile with:
    g++ -std=c++11 multitrack.cpp -o multitrack -lncurses -lasound
or:
    make
Note: Requires g++ 4.7 or later for c++11 support.
