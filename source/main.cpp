#include <cstdio>

#include <borealis.hpp>
#include <switch.h>
#include <unistd.h>

#include "config.hpp"
#include "feed_activity.hpp"
#include "mpv_player.hpp"
#include "net.hpp"
#include "orientation.hpp"
#include "settings.hpp"

int main(int argc, char* argv[])
{
    // Sockets, romfs and the system services are already up: borealis'
    // switch_wrapper.c defines userAppInit(), which libnx runs from crt0 before
    // main. Do not re-initialise them here.
    //
    // That file only wires up nxlink stdio under DEBUG, so in a release build
    // this is the call that makes startup failures visible instead of showing a
    // black screen and bouncing straight back to hbmenu.
    const int nxlinkFd = nxlinkStdio();
    setvbuf(stdout, nullptr, _IOLBF, 0);

    // Launched from the SD card or inside an emulator there is no nxlink host,
    // and stdout goes nowhere -- which is how a startup failure becomes a black
    // screen with no explanation. Fall back to a file on the SD card, which an
    // emulator exposes on the host filesystem and a console keeps for later.
    if (nxlinkFd < 0)
    {
        if (FILE* logFile = fopen("sdmc:/switch-tok.log", "w"))
        {
            setvbuf(logFile, nullptr, _IOLBF, 0);
            brls::Logger::setLogOutput(logFile);
        }
    }

    brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
    brls::Logger::info("switch-tok starting");

    net::globalInit();
    brls::Logger::info("curl ready");

    settings::load();

    if (!brls::Application::init())
    {
        brls::Logger::error("borealis init failed");
        return EXIT_FAILURE;
    }
    brls::Logger::info("borealis init ok");

    brls::Application::createWindow("switch-tok");
    brls::Application::setGlobalQuit(true);
    brls::Logger::info("window created");

    // Must come after createWindow: the render context needs a live GL context.
    // A failure here is not fatal on purpose -- the UI still comes up and says
    // so, which beats exiting to hbmenu with no explanation.
    const bool mpvReady = MpvPlayer::instance().init();
    if (mpvReady)
    {
        MpvPlayer::instance().setVolume(settings::get().volume);
        if (settings::get().muted)
            MpvPlayer::instance().toggleMute();
        brls::Logger::info("mpv init ok");
    }
    else
    {
        brls::Logger::error("mpv init failed - continuing without playback");
    }

    // Needs borealis' HID setup, so it goes after the window. Failing here just
    // means the picture never rotates.
    OrientationSensor::instance().init();

    brls::Application::pushActivity(new FeedActivity());
    brls::Logger::info("entering main loop");

    while (brls::Application::mainLoop())
        ;

    brls::Logger::info("main loop exited");

    OrientationSensor::instance().exit();
    MpvPlayer::instance().exit();
    net::globalExit();  // sockets and romfs are torn down by userAppExit()

    return EXIT_SUCCESS;
}
