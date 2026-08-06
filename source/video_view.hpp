#pragma once

#include <functional>

#include <borealis.hpp>

#include "feed_item.hpp"

// Fullscreen video surface with the caption overlay on top.
//
// Owns no decoding state of its own: playback lives in MpvPlayer, this only
// blits the current frame and lays out the text.
class VideoView : public brls::Box
{
  public:
    VideoView();

    void setItem(const FeedItem& item);
    void setStatus(const std::string& text);
    void setPosition(int index, int total);

    // Region code, or the active search term. Shown next to the counter rather
    // than only flashed on change, so "which feed am I in" is always answerable.
    void setContext(const std::string& text);

    // Transient feedback (volume, mute) that clears itself, so a one-off
    // message never becomes permanent furniture.
    void flashStatus(const std::string& text);

    // delta is -1 or +1. The view knows the gesture, the activity owns the feed.
    void setOnNavigate(std::function<void(int)> callback) { onNavigate = std::move(callback); }
    void setOnTogglePause(std::function<void()> callback) { onTogglePause = std::move(callback); }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

  private:
    void onPan(const brls::PanGestureStatus& status);
    void refreshCounter();

    brls::Label* authorLabel  = nullptr;
    brls::Label* descLabel    = nullptr;
    brls::Label* statusLabel  = nullptr;
    brls::Label* counterLabel = nullptr;

    std::string contextText;
    int         positionIndex = 0;
    int         positionTotal = 0;

    float spinnerPhase    = 0.0f;
    int   statusTicks     = 0;
    bool  prefetchAllowed = false;

    std::function<void(int)> onNavigate;
    std::function<void()>    onTogglePause;

    FeedItem currentItem;
    int      stalledFrames  = 0;
    bool     retriedFromCache = false;

    // A single pan recognizer serves both gestures: the first significant
    // movement decides which one it is. Two recognizers would fight over the
    // same touch.
    enum class Pan
    {
        Undecided,
        Swipe,
        Scrub,
    };

    Pan   panMode      = Pan::Undecided;
    float scrubTarget  = 0.0f;
    bool  scrubbing    = false;
    int   scrubCooldown = 0;

    // Geometry of the picture as last drawn. Gestures are interpreted in the
    // picture's frame, not the screen's: when the console is turned, "up" to
    // the viewer is sideways to the panel, and reading raw screen coordinates
    // turns every swipe into a scrub.
    float lastRotation  = 0.0f;
    float lastFitWidth  = 0.0f;
    float lastCenterX   = 0.0f;
    float lastCenterY   = 0.0f;
};
