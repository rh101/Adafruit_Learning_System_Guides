// SPDX-FileCopyrightText: 2021 Phil Burgess for Adafruit Industries
//
// SPDX-License-Identifier: MIT

/*
MOVE-AND-BLINK EYES for Adafruit EyeLights (LED Glasses + Driver).

I'd written a very cool squash-and-stretch effect for the eye movement,
but unfortunately the resolution is such that the pupils just look like
circles regardless. I'm keeping it in despite the added complexity,
because this WILL look great later on a bigger matrix or a TFT/OLED,
and this way the hard parts won't require a re-write at such time.
It's a really adorable effect with enough pixels.
*/

#include <Adafruit_IS31FL3741.h> // For LED driver

// CONFIGURABLES ------------------------

#define RADIUS 3.4 // Size of pupil (3X because of downsampling later)

uint8_t eye_color[3] = { 255, 128, 0 };      // Amber pupils
uint8_t ring_open_color[3] = { 75, 75, 75 }; // Color of LED rings when eyes open
uint8_t ring_blink_color[3] = { 50, 25, 0 }; // Color of LED ring "eyelid" when blinking

// Some boards have just one I2C interface, but some have more...
TwoWire *i2c = &Wire; // e.g. change this to &Wire1 for QT Py RP2040

// GLOBAL VARIABLES ---------------------

Adafruit_EyeLights_buffered glasses(true); // Buffered spex + 3X canvas
GFXcanvas16 *canvas;                       // Pointer to canvas object

// Reading through the code, you'll see a lot of references to this "3X"
// space. This is referring to the glasses' optional "offscreen" drawing
// canvas that's 3 times the resolution of the LED matrix (i.e. 15 pixels
// tall instead of 5), which gets scaled down to provide some degree of
// antialiasing. It's why the pupils have soft edges and can make
// fractional-pixel motions.

float cur_pos[2] = { 9.0, 7.5 };  // Current position of eye in canvas space
float next_pos[2] = { 9.0, 7.5 }; // Next position "
bool in_motion = false;           // true = eyes moving, false = eyes paused
uint8_t blink_state = 0;          // 0, 1, 2 = unblinking, closing, opening
uint32_t move_start_time = 0;     // For animation timekeeping
uint32_t move_duration = 0;
uint32_t blink_start_time = 0;
uint32_t blink_duration = 0;
float y_pos[13];                 // Coords of LED ring pixels in canvas space
uint32_t ring_open_color_packed; // ring_open_color[] as packed RGB integer
uint16_t eye_color565;           // eye_color[] as a GFX packed '565' value
uint32_t frames = 0;             // For frames-per-second calculation
uint32_t start_time;

// These offsets position each pupil on the canvas grid and make them
// fixate slightly (converge on a point) so they're not always aligned
// the same on the pixel grid, which would be conspicuously pixel-y.
float x_offset[2] = { 5.0, 31.0 };
// These help perform x-axis clipping on the rasterized ellipses,
// so they don't "bleed" outside the rings and require erasing.
int box_x_min[2] = { 3, 33 };
int box_x_max[2] = { 21, 51 };

#define GAMMA  2.6 // For color correction, shouldn't need changing


// HELPER FUNCTIONS ---------------------

// Crude error handler, prints message to Serial console, flashes LED
void err(char *str, uint8_t hz) {
  Serial.println(str);
  pinMode(LED_BUILTIN, OUTPUT);
  for (;;) digitalWrite(LED_BUILTIN, (millis() * hz / 500) & 1);
}

// Given an [R,G,B] color, apply gamma correction, return packed RGB integer.
uint32_t gammify(uint8_t color[3]) {
  uint32_t rgb[3];
  for (uint8_t i=0; i<3; i++) {
    rgb[i] = uint32_t(pow((float)color[i] / 255.0, GAMMA) * 255 + 0.5);
  }
  return (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
}

// Given two [R,G,B] colors and a blend ratio (0.0 to 1.0), interpolate between
// the two colors and return a gamma-corrected in-between color as a packed RGB
// integer. No bounds clamping is performed on blend value, be nice.
uint32_t interp(uint8_t color1[3], uint8_t color2[3], float blend) {
  float inv = 1.0 - blend; // Weighting of second color
  uint8_t rgb[3];
  for(uint8_t i=0; i<3; i++) {
    rgb[i] = (int)((float)color1[i] * blend + (float)color2[i] * inv);
  }
  return gammify(rgb);
}

// Rasterize an arbitrary ellipse into the offscreen 3X canvas, given
// foci point1 and point2 and with area determined by global RADIUS
// (when foci are same point; a circle). Foci and radius are all
// floating point values, which adds to the buttery impression. 'rect'
// is a bounding rect of which pixels are likely affected. Canvas is
// assumed cleared before arriving here.
void rasterize(float point1[2], float point2[2], int rect[4]) {
  float perimeter, d;
  float dx = point2[0] - point1[0];
  float dy = point2[1] - point1[1];
  float d2 = dx * dx + dy * dy; // Dist between foci, squared
  if (d2 <= 0.0) {
    // Foci are in same spot - it's a circle
    perimeter = 2.0 * RADIUS;
    d = 0.0;
  } else {
    // Foci are separated - it's an ellipse.
    d = sqrt(d2); // Distance between foci
    float c = d * 0.5; // Center-to-foci distance
    // This is an utterly brute-force way of ellipse-filling based on
    // the "two nails and a string" metaphor...we have the foci points
    // and just need the string length (triangle perimeter) to yield
    // an ellipse with area equal to a circle of 'radius'.
    // c^2 = a^2 - b^2  <- ellipse formula
    //   a = r^2 / b    <- substitute
    // c^2 = (r^2 / b)^2 - b^2
    // b = sqrt(((c^2) + sqrt((c^4) + 4 * r^4)) / 2)  <- solve for b
    float c2 = c * c;
    float b2 = (c2 + sqrt((c2 * c2) + 4 * (RADIUS * RADIUS * RADIUS * RADIUS))) * 0.5;
    // By my math, perimeter SHOULD be...
    // perimeter = d + 2 * sqrt(b2 + c2);
    // ...but for whatever reason, working approach here is really...
    perimeter = d + 2 * sqrt(b2);
  }

  // Like I'm sure there's a way to rasterize this by spans rather than
  // all these square roots on every pixel, but for now...
  for (int y=rect[1]; y<rect[3]; y++) {   // For each row...
    float y5 = (float)y + 0.5;            // Pixel center
    float dy1 = y5 - point1[1];           // Y distance from pixel to first point
    float dy2 = y5 - point2[1];           // " to second
    dy1 *= dy1;                           // Y1^2
    dy2 *= dy2;                           // Y2^2
    for (int x=rect[0]; x<rect[2]; x++) { // For each column...
      float x5 = (float)x + 0.5;          // Pixel center
      float dx1 = x5 - point1[0];         // X distance from pixel to first point
      float dx2 = x5 - point2[0];         // " to second
      float d1 = sqrt(dx1 * dx1 + dy1);   // 2D distance to first point
      float d2 = sqrt(dx2 * dx2 + dy2);   // " to second
      if ((d1 + d2 + d) <= perimeter) {   // Point inside ellipse?
        canvas->drawPixel(x, y, eye_color565);
      }
    }
  }
}


// ONE-TIME INITIALIZATION --------------

void setup() {
  // Initialize hardware
  Serial.begin(115200);
  if (! glasses.begin(IS3741_ADDR_DEFAULT, i2c)) err("IS3741 not found", 2);

  canvas = glasses.getCanvas();
  if (!canvas) err("Can't allocate canvas", 5);

  i2c->setClock(1000000); // 1 MHz I2C for extra butteriness

  // Configure glasses for reduced brightness, enable output
  glasses.setLEDscaling(0xFF);
  glasses.setGlobalCurrent(20);
  glasses.enable(true);

  // INITIALIZE TABLES & OTHER GLOBALS ----

  // Pre-compute the Y position of 1/2 of the LEDs in a ring, relative
  // to the 3X canvas resolution, so ring & matrix animation can be aligned.
  for (uint8_t i=0; i<13; i++) {
    float angle = (float)i / 24.0 * M_PI * 2.0;
    y_pos[i] = 10.0 - cos(angle) * 12.0;
  }

  // Convert some colors from [R,G,B] (easier to specify) to packed integers
  ring_open_color_packed = gammify(ring_open_color);
  eye_color565 = glasses.color565(eye_color[0], eye_color[1], eye_color[2]);

  start_time = millis(); // For frames-per-second math
}

// MAIN LOOP ----------------------------

void loop() {
  canvas->fillScreen(0);

  // The eye animation logic is a carry-over from like a billion
  // prior eye projects, so this might be comment-light.
  uint32_t now = micros(); // 'Snapshot' the time once per frame

  float upper, lower, ratio;

  // Blink logic
  uint32_t elapsed = now - blink_start_time; // Time since start of blink event
  if (elapsed > blink_duration) {  // All done with event?
    blink_start_time = now;        // A new one starts right now
    elapsed = 0;
    blink_state++;                 // Cycle closing/opening/paused
    if (blink_state == 1) {        // Starting new blink...
      blink_duration = random(60000, 120000);
    } else if (blink_state == 2) { // Switching closing to opening...
      blink_duration *= 2;         // Opens at half the speed
    } else {                       // Switching to pause in blink
      blink_state = 0;
      blink_duration = random(500000, 4000000);
    }
  }
  if (blink_state) {            // If currently in a blink...
    float ratio = (float)elapsed / (float)blink_duration; // 0.0-1.0 as it closes
    if (blink_state == 2) ratio = 1.0 - ratio;            // 1.0-0.0 as it opens
    upper = ratio * 15.0 - 4.0; // Upper eyelid pos. in 3X space
    lower = 23.0 - ratio * 8.0; // Lower eyelid pos. in 3X space
  }

  // Eye movement logic. Two points, 'p1' and 'p2', are the foci of an
  // ellipse. p1 moves from current to next position a little faster
  // than p2, creating a "squash and stretch" effect (frame rate and
  // resolution permitting). When motion is stopped, the two points
  // are at the same position.
  float p1[2], p2[2];
  elapsed = now - move_start_time;             // Time since start of move event
  if (in_motion) {                             // Currently moving?
    if (elapsed > move_duration) {             // If end of motion reached,
      in_motion = false;                       // Stop motion and
      memcpy(&p1, &next_pos, sizeof next_pos); // set everything to new position
      memcpy(&p2, &next_pos, sizeof next_pos);
      memcpy(&cur_pos, &next_pos, sizeof next_pos);
      move_duration = random(500000, 1500000); // Wait this long
    } else { // Still moving
      // Determine p1, p2 position in time
      float delta[2];
      delta[0] = next_pos[0] - cur_pos[0];
      delta[1] = next_pos[1] - cur_pos[1];
      ratio = (float)elapsed / (float)move_duration;
      if (ratio < 0.6) { // First 60% of move time, p1 is in motion
        // Easing function: 3*e^2-2*e^3 0.0 to 1.0
        float e = ratio / 0.6; // 0.0 to 1.0
        e = 3 * e * e - 2 * e * e * e;
        p1[0] = cur_pos[0] + delta[0] * e;
        p1[1] = cur_pos[1] + delta[1] * e;
      } else {                                   // Last 40% of move time
        memcpy(&p1, &next_pos, sizeof next_pos); // p1 has reached end position
      }
      if (ratio > 0.3) { // Last 70% of move time, p2 is in motion
        float e = (ratio - 0.3) / 0.7; // 0.0 to 1.0
        e = 3 * e * e - 2 * e * e * e; // Easing func.
        p2[0] = cur_pos[0] + delta[0] * e;
        p2[1] = cur_pos[1] + delta[1] * e;
      } else {                                 // First 30% of move time
        memcpy(&p2, &cur_pos, sizeof cur_pos); // p2 waits at start position
      }
    }
  } else { // Eye is stopped
    memcpy(&p1, &cur_pos, sizeof cur_pos); // Both foci at current eye position
    memcpy(&p2, &cur_pos, sizeof cur_pos);
    if (elapsed > move_duration) { // Pause time expired?
      in_motion = true;            // Start up new motion!
      move_start_time = now;
      move_duration = random(150000, 250000);
      float angle = (float)random(1000) / 1000.0 * M_PI * 2.0;
      float dist = (float)random(750) / 100.0;
      next_pos[0] = 9.0 + cos(angle) * dist;
      next_pos[1] = 7.5 + sin(angle) * dist * 0.8;
    }
  }

  // Draw the raster part of each eye...
  for (uint8_t e=0; e<2; e++) {
    // Each eye's foci are offset slightly, to fixate toward center
    float p1a[2], p2a[2];
    p1a[0] = p1[0] + x_offset[e];
    p2a[0] = p2[0] + x_offset[e];
    p1a[1] = p2a[1] = p1[1];
    // Compute bounding rectangle (in 3X space) of ellipse
    // (min X, min Y, max X, max Y). Like the ellipse rasterizer,
    // this isn't optimal, but will suffice.
    int bounds[4];
    bounds[0] = max(int(min(p1a[0], p2a[0]) - RADIUS), box_x_min[e]);
    bounds[1] = max(max(int(min(p1a[1], p2a[1]) - RADIUS), 0), (int)upper);
    bounds[2] = min(int(max(p1a[0], p2a[0]) + RADIUS + 1), box_x_max[e]);
    bounds[3] = min(int(max(p1a[1], p2a[1]) + RADIUS + 1), 15);
    rasterize(p1a, p2a, bounds); // Render ellipse into buffer
  }

  // If the eye is currently blinking, and if the top edge of the eyelid
  // overlaps the bitmap, draw lines across the bitmap as if eyelids.
  if (blink_state and upper >= 0.0) {
    int iu = (int)upper;
    canvas->drawLine(box_x_min[0], iu, box_x_max[0] - 1, iu, eye_color565);
    canvas->drawLine(box_x_min[1], iu, box_x_max[1] - 1, iu, eye_color565);
  }

  glasses.scale(); // Smooth filter 3X canvas to LED grid

  // Matrix and rings share a few pixels. To make the rings take
  // precedence, they're drawn later. So blink state is revisited now...
  if (blink_state) { // In mid-blink?
    for (uint8_t i=0; i<13; i++) { // Half an LED ring, top-to-bottom...
      float a = min(max(y_pos[i] - upper + 1.0, 0.0), 3.0);
      float b = min(max(lower - y_pos[i] + 1.0, 0.0), 3.0);
      ratio = a * b / 9.0; // Proximity of LED to eyelid edges
      uint32_t packed = interp(ring_open_color, ring_blink_color, ratio);
      glasses.left_ring.setPixelColor(i, packed);
      glasses.right_ring.setPixelColor(i, packed);
      if ((i > 0) && (i < 12)) {
        uint8_t j = 24 - i; // Mirror half-ring to other side
        glasses.left_ring.setPixelColor(j, packed);
        glasses.right_ring.setPixelColor(j, packed);
      }
    }
  } else {
    glasses.left_ring.fill(ring_open_color_packed);
    glasses.right_ring.fill(ring_open_color_packed);
  }

  glasses.show();

  frames += 1;
  elapsed = millis() - start_time;
  Serial.println(frames * 1000 / elapsed);
}
