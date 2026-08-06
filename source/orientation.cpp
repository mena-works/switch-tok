#include "orientation.hpp"

#include <cmath>

#include <borealis.hpp>
#include <switch.h>

namespace
{

// Only the roll axis matters. Measured on hardware: held normally the console
// sits tilted back, so gravity lands on Z (x~0.0, y~0.0, z~-1.0), and turning it
// on its side moves gravity onto X (x~0.99). Comparing X against Y -- the
// obvious first guess -- gets the turn right but never detects the way back,
// because on the way back neither axis is anywhere near 1.
//
// Two thresholds, not one: rotate at 0.65, return below 0.40. A single
// threshold makes the picture flip back and forth while the console is held
// anywhere near the boundary.
constexpr float kEnterPortrait = 0.65f;
constexpr float kLeavePortrait = 0.40f;

// Frames the new reading must persist for. At 60 Hz this is a quarter second:
// long enough to ignore a wobble, short enough to feel immediate.
constexpr int kDebounceFrames = 15;

constexpr int kLogEvery = 180;

} // namespace

OrientationSensor& OrientationSensor::instance()
{
    static OrientationSensor sensor;
    return sensor;
}

bool OrientationSensor::init()
{
    HidSixAxisSensorHandle sixaxis;

    Result rc = hidGetSixAxisSensorHandles(&sixaxis, 1, HidNpadIdType_Handheld,
                                           HidNpadStyleTag_NpadHandheld);
    if (R_FAILED(rc))
    {
        brls::Logger::warning("six-axis handles unavailable: {:#x}", rc);
        return false;
    }

    rc = hidStartSixAxisSensor(sixaxis);
    if (R_FAILED(rc))
    {
        brls::Logger::warning("six-axis start failed: {:#x}", rc);
        return false;
    }

    handleValue = sixaxis.type_value;
    active      = true;
    brls::Logger::info("orientation sensor ready");
    return true;
}

void OrientationSensor::exit()
{
    if (!active)
        return;

    HidSixAxisSensorHandle sixaxis {};
    sixaxis.type_value = handleValue;

    hidStopSixAxisSensor(sixaxis);
    active = false;
}

Orientation OrientationSensor::update()
{
    if (!active)
        return stable;

    HidSixAxisSensorHandle sixaxis {};
    sixaxis.type_value = handleValue;

    HidSixAxisSensorState state {};
    if (hidGetSixAxisSensorStates(sixaxis, &state, 1) < 1)
        return stable;

    const float ax = state.acceleration.x;
    const float ay = state.acceleration.y;

    // The axis signs are not documented anywhere trustworthy, so log them and
    // read the mapping off a real console rather than guessing twice.
    if (++frames % kLogEvery == 0)
        brls::Logger::debug("accel x={:.2f} y={:.2f} z={:.2f} -> {}", ax, ay,
                            state.acceleration.z, static_cast<int>(stable));

    Orientation reading = stable;

    if (std::fabs(ax) > kEnterPortrait)
        reading = ax > 0.0f ? Orientation::PortraitCW : Orientation::PortraitCCW;
    else if (std::fabs(ax) < kLeavePortrait)
        reading = Orientation::Landscape;
    // Between the two thresholds the reading is ambiguous: hold what we have.

    if (reading == candidate)
    {
        if (++agreed >= kDebounceFrames && stable != candidate)
        {
            stable = candidate;
            brls::Logger::info("orientation -> {}", static_cast<int>(stable));
        }
    }
    else
    {
        candidate = reading;
        agreed    = 0;
    }

    return stable;
}

float OrientationSensor::angle() const
{
    constexpr float kHalfPi = 1.57079632679f;

    switch (stable)
    {
        case Orientation::PortraitCW:
            return -kHalfPi;
        case Orientation::PortraitCCW:
            return kHalfPi;
        default:
            return 0.0f;
    }
}
