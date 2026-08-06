#pragma once

#include <string>
#include <vector>

struct NVGcontext;
struct mpv_handle;
struct mpv_render_context;

// Thin wrapper around libmpv in render-API mode.
//
// One instance only: mpv owns the audio device and there is no reason to decode
// two feed items at once. Every method must be called from the UI thread, since
// mpv_render_context_render() issues GL calls against borealis' context.
class MpvPlayer
{
  public:
    static MpvPlayer& instance();

    bool init();
    void exit();

    // mpv fetches the URL itself, so nothing is downloaded ahead of time here.
    void play(const std::string& url);
    void stop();

    void setPaused(bool paused);

    void setVolume(int volume);  // 0-100
    void adjustVolume(int delta);
    int  volume() const { return volumeLevel; }

    void toggleMute();
    bool muted() const { return isMuted; }

    // Relative, in seconds. Negative rewinds.
    void seek(double seconds);

    // Absolute, 0..1 of the clip. Used by touch scrubbing.
    void seekToFraction(float fraction);

    // Drains mpv's event queue. Call once per frame.
    void update();

    // Renders the pending frame into an offscreen texture and returns a nanovg
    // image handle for it, or -1 if nothing is ready. Safe to call every frame:
    // it re-renders only when mpv reports a new frame.
    //
    // The target is always the video's own resolution, so mpv draws 1:1 and the
    // caller is free to scale once, however it likes. Sizing it to the view
    // instead makes mpv fit the frame first and the caller fit it again, and two
    // fits in a row is how you get a stretched picture.
    int frameImage(NVGcontext* vg);

    bool hasVideo() const { return videoWidth > 0 && videoHeight > 0; }
    int  sourceWidth() const { return videoWidth; }
    int  sourceHeight() const { return videoHeight; }

    // 0..1, or 0 when the duration is not known yet.
    float progress() const;

    // True while mpv has nothing to show: still opening the file, or stalled
    // refilling its cache mid-clip.
    bool isBuffering() const { return buffering; }

  private:
    MpvPlayer() = default;

    bool ensureTarget(NVGcontext* vg, int width, int height);
    void releaseTarget(NVGcontext* vg);

    mpv_handle*         mpv       = nullptr;
    mpv_render_context* renderCtx = nullptr;

    unsigned int fbo     = 0;
    unsigned int texture = 0;
    int          nvgImage = -1;
    int          targetWidth  = 0;
    int          targetHeight = 0;

    int videoWidth  = 0;
    int videoHeight = 0;

    int  volumeLevel = 70;
    bool isMuted     = false;

    double timePos   = 0.0;
    double duration  = 0.0;
    bool   buffering = true;
    bool   coreIdle  = true;
    bool   cacheWait = false;
};
