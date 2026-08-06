#pragma once

enum class Orientation
{
    Landscape,   // console held normally
    PortraitCW,  // rotated clockwise
    PortraitCCW, // rotated counter-clockwise
};

// Reads the handheld six-axis sensor to tell which way the console is being
// held. Joy-Cons are attached in handheld mode, so their accelerometer is the
// console's accelerometer; docked there is no such thing as screen orientation
// and this reports Landscape forever.
class OrientationSensor
{
  public:
    static OrientationSensor& instance();

    bool init();
    void exit();

    // Call once per frame.
    Orientation update();

    Orientation current() const { return stable; }

    // Radians to rotate the picture by so it stands upright for the viewer.
    float angle() const;

  private:
    OrientationSensor() = default;

    // HidSixAxisSensorHandle is a union over a u32, kept as the raw value here
    // so this header does not have to pull in all of switch.h.
    unsigned int handleValue = 0;
    bool         active      = false;

    Orientation stable    = Orientation::Landscape;
    Orientation candidate = Orientation::Landscape;
    int         agreed    = 0;
    int         frames    = 0;
};
