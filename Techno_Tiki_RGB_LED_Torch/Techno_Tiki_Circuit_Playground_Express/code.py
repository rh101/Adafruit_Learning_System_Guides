# SPDX-FileCopyrightText: 2018 Tony DiCola for Adafruit Industries
# SPDX-FileCopyrightText: 2018 Mikey Sklar for Adafruit Industries
#
# SPDX-License-Identifier: MIT

# Techno-Tiki RGB LED Torch with IR Remote Control
# Created by Tony DiCola for Arduino
# Ported to CircuitPython by Mikey Sklar
#
# See guide at: https://learn.adafruit.com/techno-tiki-rgb-led-torch
#
# Released under a MIT license: http://opensource.org/licenses/MIT

import time
import adafruit_irremote
import board
import neopixel
import pulseio

# pylint: disable=global-statement

brightness = 1        # 0-1, higher number is brighter

# Adafruit IR Remote Codes:
# Button       Code         Button  Code
# -----------  ------       ------  -----
# VOL-:        255            0/10+:    207
# Play/Pause:  127            1:        247
# VOL+:        191            2:        119
# SETUP:       223            3:        183
# STOP/MODE:   159            4:        215
# UP:          95            5:        87
# DOWN:        79            6:        151
# LEFT:        239            7:        231
# RIGHT:       175            8:        103
# ENTER/SAVE:  111            9:        167
# Back:        143

# Adafruit IR Remote Codes:
volume_down = 255
play_pause = 127
volume_up = 191
setup = 223
up_arrow = 95
stop_mode = 159
left_arrow = 239
enter_save = 111
right_arrow = 175
down_arrow = 79
num_1 = 247
num_2 = 119
num_3 = 183
num_4 = 215
num_5 = 87
num_6 = 151
num_7 = 231
num_8 = 103
num_9 = 167

# Define which remote buttons are associated with sketch actions.
color_change = right_arrow      # Button that cycles through color animations.
animation_change = left_arrow   # Button that cycles through animation types (only two supported).
speed_change = up_arrow         # Button that cycles through speed choices.
power_off = volume_down         # Button that turns off the pixels.
power_on = volume_up            # Button that turns on the pixels. Must be pressed twice.

# The colorPalette two-dimensional array below has a row for each color animation and a column
# for each step within the animation.  Each value is a 24-bit RGB color.  By looping through
# the columns of a row the colors of pixels will animate.
color_steps = 8         # Number of steps in the animation.
color_count = 8         # number of columns/steps

# Build lookup table/palette for the color animations so they aren't computed at runtime.
color_palette = [
    # Complimentary colors
    ([255, 0, 0], [218, 36, 36], [182, 72, 72], [145, 109, 109],
     [109, 145, 145], [72, 182, 182], [36, 218, 218], [0, 255, 255]),   # red cyan
    ([255, 255, 0], [218, 218, 36], [182, 182, 72], [145, 145, 109],
     [109, 109, 145], [72, 72, 182], [36, 36, 218], [0, 0, 255]),       # yellow blue
    ([0, 255, 0], [36, 218, 36], [72, 182, 72], [109, 145, 109],
     [145, 109, 145], [182, 72, 182], [218, 36, 218], [255, 0, 255]),   # green magenta

    # Adjacent colors (on color wheel).
    ([255, 255, 0], [218, 255, 0], [182, 255, 0], [145, 255, 0],
     [109, 255, 0], [72, 255, 0], [36, 255, 0], [0, 255, 0]),           # yello green
    ([0, 255, 0], [0, 255, 36], [0, 255, 72], [0, 255, 109],
     [0, 255, 145], [0, 255, 182], [0, 255, 218], [0, 255, 255]),       # green cyan
    ([0, 255, 255], [0, 218, 255], [0, 182, 255], [0, 145, 255],
     [0, 109, 255], [0, 72, 255], [0, 36, 255], [0, 0, 255]),           # cyan blue
    ([0, 0, 255], [36, 0, 255], [72, 0, 255], [109, 0, 255],
     [145, 0, 255], [182, 0, 255], [218, 0, 255], [255, 0, 255]),       # blue magenta
    ([255, 0, 255], [255, 0, 218], [255, 0, 182], [255, 0, 145],
     [255, 0, 109], [255, 0, 72], [255, 0, 36], [255, 0, 0])            # magenta red
]

   # Other combos
   #([255, 0, 0], [218, 36, 0], [182, 72, 0], [145, 109, 0],
    #[109, 145, 0], [72, 182, 0], [36, 218, 0], [0, 255, 0]),           # red green
   #([255, 255, 0], [218, 255, 36], [182, 255, 72], [145, 255, 109],
    #[109, 255, 145], [72, 255, 182], [36, 255, 218], [0, 255, 255]),   # yellow cyan
   #([0, 255, 0], [0, 218, 36], [0, 182, 72], [0, 145, 109],
    #[0, 109, 145], [0, 72, 182], [0, 36, 218], [0, 0, 255]),           # green blue
   #([0, 255, 255], [36, 218, 255], [72, 182, 255], [109, 145, 255],
    #[145, 109, 255], [182, 72, 255], [218, 36, 255], [255, 0, 255]),   # cyan magenta
   #([0, 0, 255], [36, 0, 218], [72, 0, 182], [109, 0, 145],
    #[145, 0, 109], [182, 0, 72], [218, 0, 36], [255, 0, 0]),           # blue red
   #([255, 0, 255], [255, 36, 218], [255, 72, 182], [255, 109, 145],
    #[255, 145, 109], [255, 182, 72], [255, 218, 36], [255, 255, 0]),   # magenta yellow

   # Solid colors fading to dark.
   #([255, 0, 0], [223, 0, 0], [191, 0, 0], [159, 0, 0],
    #[127, 0, 0], [95, 0, 0], [63, 0, 0], [31, 0, 0]),                  # red
   #([255, 153, 0], [223, 133, 0], [191, 114, 0], [159, 95, 0],
    #[127, 76, 0], [95, 57, 0], [63, 38, 0], [31, 19, 0]),              # orange
   #([255, 255, 0], [223, 223, 0], [191, 191, 0], [159, 159, 0],
    #[127, 127, 0], [95, 95, 0], [63, 63, 0], [31, 31, 0]),             # yellow
   #([0, 255, 0], [0, 223, 0], [0, 191, 0], [0, 159, 0],
    #[0, 127, 0], [0, 95, 0], [0, 63, 0], [0, 31, 0]),                  # green
   #([0, 0, 255], [0, 0, 223], [0, 0, 191], [0, 0, 159],
    #[0, 0, 127], [0, 0, 95], [0, 0, 63], [0, 0, 31]),                  # blue
   #([75, 0, 130], [65, 0, 113], [56, 0, 97], [46, 0, 81],
    #[37, 0, 65], [28, 0, 48], [18, 0, 32], [9, 0, 16]),                # indigo
   #([139, 0, 255], [121, 0, 223], [104, 0, 191], [86, 0, 159],
    #[69, 0, 127], [52, 0, 95], [34, 0, 63], [17, 0, 31]),              # violet
   #([255, 255, 255], [223, 223, 223], [191, 191, 191], [159, 159, 159],
    #[127, 127, 127], [95, 95, 95], [63, 63, 63], [31, 31, 31]),        # white
   #([255, 0, 0], [255, 153, 0], [255, 255, 0], [0, 255, 0],
    #[0, 0, 255], [75, 0, 130], [139, 0, 255], [255, 255, 255]),        # rainbow colors

# List of animations speeds (in seconds).  This is how long an animation spends before
# changing to the next step.  Higher values are slower.
speeds = [.4, .2, .1, .05, .025]

# Global state used by the sketch
color_index = 0
animation_index = 0
speed_index = 2

pixel_pin = board.D1    # Pin where NeoPixels are connected
pixel_count = 10        # Number of NeoPixels

speed = .1              # Animation speed (in seconds).
                        # This is how long to spend in a single animation frame.
                        # Higher values are slower.

animation = 1           # Type of animation, can be one of these values:
                        # 0 - Solid color pulse
                        # 1 - Moving color pulse

# initialize LEDS
strip = neopixel.NeoPixel(board.NEOPIXEL, pixel_count, brightness=brightness, auto_write=False)

# initialize Remote Control
ir_code_min = 60
ir_code_max = 70
pulsein = pulseio.PulseIn(board.REMOTEIN, maxlen=100, idle_state=True)
decoder = adafruit_irremote.GenericDecode()

def read_nec():
# Check if a NEC IR remote command is the correct length.
# Save the third decoded value as our unique identifier.

    pulses = decoder.read_pulses(pulsein, max_pulse=5000)
    command = None

    try:
        if len(pulses) >= ir_code_min and len(pulses) <= ir_code_max:
            code = decoder.decode_bits(pulses)
            if len(code) > 3:
                command = code[2]
    except adafruit_irremote.IRNECRepeatException:  # Catches the repeat signal
        pass
    except adafruit_irremote.IRDecodeException:  # Failed to decode
        pass

    return command

def handle_remote():
    global color_index, animation_index, speed_index

    ir_code = read_nec()

    if ir_code is None:
        time.sleep(.1)
        return

    if ir_code == color_change:
        color_index = (color_index + 1) % color_count
    elif ir_code == animation_change:
        animation_index = (animation_index + 1) % 2
    elif ir_code == speed_change:
        speed_index = (speed_index + 1) % 5
    elif ir_code == power_off:
        strip.fill([0, 0, 0])
        strip.show()

while True:  # Loop forever...

    # Main loop will update all the pixels based on the animation.
    for i in range(pixel_count):

        # Animation 0, solid color pulse of all pixels.
        if animation_index == 0:
            current_step = (time.monotonic() / speeds[speed_index]) % (color_steps * 2 - 2)
            if current_step >= color_steps:
                current_step = color_steps - (current_step - (color_steps - 2))

        # Animation 1, moving color pulse.  Use position to change brightness.
        elif animation == 1:
            current_step = (time.monotonic() / speeds[speed_index] + i) % (color_steps * 2 - 2)
            if current_step >= color_steps:
                current_step = color_steps - (current_step - (color_steps - 2))

        strip[i] = color_palette[int(color_index)][int(current_step)]

    # Next check for any IR remote commands.
    handle_remote()

    # Show the updated pixels.
    strip.show()
