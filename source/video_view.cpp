#include "video_view.hpp"

#include <algorithm>
#include <cmath>

#include "clip_cache.hpp"
#include "mpv_player.hpp"
#include "orientation.hpp"

VideoView::VideoView()
{
    // Captions sit at the bottom-left, like the source material.
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_END);
    this->setAlignItems(brls::AlignItems::FLEX_START);
    this->setPadding(0.0f, 40.0f, 48.0f, 40.0f);
    this->setBackgroundColor(nvgRGB(0, 0, 0));

    statusLabel = new brls::Label();
    statusLabel->setText("loading feed...");
    statusLabel->setFontSize(20.0f);
    statusLabel->setTextColor(nvgRGBA(255, 255, 255, 160));
    this->addView(statusLabel);

    counterLabel = new brls::Label();
    counterLabel->setFontSize(18.0f);
    counterLabel->setTextColor(nvgRGBA(255, 255, 255, 140));
    this->addView(counterLabel);

    authorLabel = new brls::Label();
    authorLabel->setFontSize(28.0f);
    authorLabel->setTextColor(nvgRGB(255, 255, 255));
    this->addView(authorLabel);

    descLabel = new brls::Label();
    descLabel->setFontSize(21.0f);
    descLabel->setTextColor(nvgRGBA(255, 255, 255, 220));
    descLabel->setWidthPercentage(70.0f);
    this->addView(descLabel);

    // One recognizer on ANY axis rather than one per direction: two would
    // compete for the same touch, and whichever won first would lock out the
    // other. The first meaningful movement decides which gesture this is.
    this->addGestureRecognizer(new brls::PanGestureRecognizer(
        [this](brls::PanGestureStatus status, brls::Sound*) { onPan(status); },
        brls::PanAxis::ANY));

    this->addGestureRecognizer(new brls::TapGestureRecognizer(this, [this]() {
        if (onTogglePause)
            onTogglePause();
    }));
}

void VideoView::onPan(const brls::PanGestureStatus& status)
{
    // Undo the picture's rotation so the gesture is read in the frame the
    // viewer is actually looking at. Turned sideways, a swipe "up" arrives as
    // horizontal screen movement, and without this it reads as a scrub.
    const float cosA = std::cos(-lastRotation);
    const float sinA = std::sin(-lastRotation);

    const float rawDx = status.position.x - status.startPosition.x;
    const float rawDy = status.position.y - status.startPosition.y;

    const float dx = rawDx * cosA - rawDy * sinA;
    const float dy = rawDx * sinA + rawDy * cosA;

    if (status.state == brls::GestureState::START)
    {
        panMode   = Pan::Undecided;
        scrubbing = false;
        return;
    }

    if (status.state == brls::GestureState::STAY)
    {
        // Wait for enough travel to tell the directions apart, otherwise a
        // slightly crooked swipe registers as a scrub and jumps the video.
        if (panMode == Pan::Undecided)
        {
            constexpr float kDecide = 24.0f;
            if (std::fabs(dx) < kDecide && std::fabs(dy) < kDecide)
                return;

            panMode = std::fabs(dy) > std::fabs(dx) ? Pan::Swipe : Pan::Scrub;
        }

        if (panMode == Pan::Scrub)
        {
            if (lastFitWidth > 0.0f)
            {
                // Position mapped into the picture's frame as well, so the
                // handle follows the finger along the bar however it is turned.
                const float relX = status.position.x - lastCenterX;
                const float relY = status.position.y - lastCenterY;
                const float alongBar = relX * cosA - relY * sinA;

                scrubbing   = true;
                scrubTarget = 0.5f + alongBar / lastFitWidth;
                scrubTarget = std::max(0.0f, std::min(1.0f, scrubTarget));

                // Throttled: seeking on every frame of a drag makes the
                // decoder thrash and the picture never settles.
                if (--scrubCooldown <= 0)
                {
                    MpvPlayer::instance().seekToFraction(scrubTarget);
                    scrubCooldown = 6;
                }
            }
        }
        return;
    }

    if (status.state == brls::GestureState::END)
    {
        if (panMode == Pan::Scrub && scrubbing)
        {
            MpvPlayer::instance().seekToFraction(scrubTarget);
        }
        else if (panMode == Pan::Swipe)
        {
            // Up means forward, matching the source material.
            constexpr float kCommit = 70.0f;
            if (dy < -kCommit && onNavigate)
                onNavigate(1);
            else if (dy > kCommit && onNavigate)
                onNavigate(-1);
        }

        panMode   = Pan::Undecided;
        scrubbing = false;
    }
}

void VideoView::setPosition(int index, int total)
{
    positionIndex = index;
    positionTotal = total;
    refreshCounter();
}

void VideoView::setContext(const std::string& text)
{
    contextText = text;
    refreshCounter();
}

void VideoView::refreshCounter()
{
    if (positionTotal <= 0)
    {
        counterLabel->setText(contextText);
        return;
    }

    std::string text = std::to_string(positionIndex + 1) + " / " + std::to_string(positionTotal);
    if (!contextText.empty())
        text += "   " + contextText;

    counterLabel->setText(text);
}

void VideoView::setItem(const FeedItem& item)
{
    currentItem      = item;
    stalledFrames    = 0;
    retriedFromCache = false;

    authorLabel->setText("@" + item.author);

    // Captions run long and the box grows to fit, pushing the text up over the
    // picture. Clamp instead of letting the layout decide.
    constexpr size_t kMaxCaption = 110;
    descLabel->setText(item.description.size() > kMaxCaption
                           ? item.description.substr(0, kMaxCaption) + "..."
                           : item.description);

    statusLabel->setText("");

    // Plays straight out of RAM when the clip was prefetched, and falls back to
    // the CDN when the swipe outran the download.
    MpvPlayer::instance().play(ClipCache::instance().uriFor(item.id, item.videoUrl));
}

void VideoView::setStatus(const std::string& text)
{
    statusLabel->setText(text);
    statusTicks = 0;  // errors stay up until something replaces them
}

void VideoView::flashStatus(const std::string& text)
{
    statusLabel->setText(text);
    statusTicks = 120;  // ~2 s at 60 Hz
}

void VideoView::draw(NVGcontext* vg, float x, float y, float width, float height,
                     brls::Style style, brls::FrameContext* ctx)
{
    MpvPlayer& player = MpvPlayer::instance();
    player.update();

    if (statusTicks > 0 && --statusTicks == 0)
        statusLabel->setText("");

    // Downloading while mpv is still opening a stream makes them fight over the
    // console's socket pool, and the player loses: its own TLS handshake gets
    // closed by the peer. So prefetch only once playback is settled.
    //
    // But "settled" alone deadlocks when playback cannot start at all: no video
    // means no prefetch means nothing cached means still no video. After a few
    // seconds of nothing, assume mpv is not using the network and let the
    // downloads through -- they are the way out.
    constexpr int kStallFrames = 180;  // ~3 s

    if (player.isBuffering())
        ++stalledFrames;
    else
        stalledFrames = 0;

    const bool settled = (player.hasVideo() && !player.isBuffering()) || stalledFrames > kStallFrames;
    if (settled != prefetchAllowed)
    {
        prefetchAllowed = settled;
        ClipCache::instance().setAllowed(settled);
    }

    // Once the clip we are stuck on has been downloaded, play it from memory.
    // mpv resolves CDN hostnames itself and cannot always do so; the cache went
    // through our own resolver, so it has bytes mpv can read without a network.
    if (!retriedFromCache && stalledFrames > kStallFrames && !currentItem.id.empty())
    {
        ClipCache& cache = ClipCache::instance();
        if (cache.lookup(currentItem.id))
        {
            retriedFromCache = true;
            brls::Logger::info("playback stalled; retrying {} from cache", currentItem.id);
            player.play(cache.uriFor(currentItem.id, currentItem.videoUrl));
        }
        else
        {
            cache.prefetch(currentItem.id, currentItem.videoUrl);
        }
    }

    OrientationSensor& sensor = OrientationSensor::instance();
    sensor.update();

    const bool  rotated  = sensor.current() != Orientation::Landscape;
    const float rotation = sensor.angle();

    const int image = player.frameImage(vg);

    if (image >= 0 && player.hasVideo())
    {
        // Turning the console turns the usable box with it, which is the whole
        // point: a 9:16 clip fills a sideways 720x1280 screen exactly.
        const float boxWidth  = rotated ? height : width;
        const float boxHeight = rotated ? width : height;

        // The texture is the video at its own resolution, so this is the only
        // place the frame gets scaled. Contain rather than cover, so nothing is
        // cropped whichever way the console is held.
        const float videoAspect = static_cast<float>(player.sourceWidth()) / player.sourceHeight();
        const float boxAspect   = boxWidth / boxHeight;

        float fitWidth  = boxWidth;
        float fitHeight = boxHeight;

        if (videoAspect > boxAspect)
            fitHeight = boxWidth / videoAspect;
        else
            fitWidth = boxHeight * videoAspect;

        const float halfW = fitWidth * 0.5f;
        const float halfH = fitHeight * 0.5f;

        // Remembered for gesture handling, which has no other way to know how
        // the picture is currently placed.
        lastRotation = rotated ? rotation : 0.0f;
        lastFitWidth = fitWidth;
        lastCenterX  = x + width * 0.5f;
        lastCenterY  = y + height * 0.5f;

        nvgSave(vg);
        nvgTranslate(vg, x + width * 0.5f, y + height * 0.5f);
        if (rotated)
            nvgRotate(vg, rotation);

        NVGpaint video = nvgImagePattern(vg, -halfW, -halfH, fitWidth, fitHeight, 0.0f, image, 1.0f);
        nvgBeginPath(vg);
        nvgRect(vg, -halfW, -halfH, fitWidth, fitHeight);
        nvgFillPaint(vg, video);
        nvgFill(vg);

        // Drawn inside the transform so it hugs the bottom edge of the picture
        // whichever way the console is held. It grows while scrubbing so the
        // finger has something that visibly responds.
        const float barHeight = scrubbing ? 10.0f : 4.0f;
        const float shown     = scrubbing ? scrubTarget : player.progress();

        nvgBeginPath(vg);
        nvgRect(vg, -halfW, halfH - barHeight, fitWidth, barHeight);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 55));
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgRect(vg, -halfW, halfH - barHeight, fitWidth * shown, barHeight);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 235));
        nvgFill(vg);

        if (scrubbing)
        {
            nvgBeginPath(vg);
            nvgCircle(vg, -halfW + fitWidth * shown, halfH - barHeight * 0.5f, 9.0f);
            nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
            nvgFill(vg);
        }

        nvgRestore(vg);
    }

    // Without this a clip that is still opening looks like the app has frozen
    // on the previous frame.
    if (player.isBuffering())
    {
        spinnerPhase += 0.11f;

        nvgBeginPath(vg);
        nvgArc(vg, x + width * 0.5f, y + height * 0.5f, 20.0f, spinnerPhase, spinnerPhase + 2.1f,
               NVG_CW);
        nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 210));
        nvgStrokeWidth(vg, 3.0f);
        nvgStroke(vg);
    }

    // Captions are laid out for a landscape box, so drawing them while the
    // console is turned would put sideways text across the picture. Held
    // upright the video already fills the screen, which is the moment you least
    // want furniture on top of it.
    if (rotated)
        return;

    // Scrim so white captions stay readable over bright frames.
    NVGpaint scrim = nvgLinearGradient(vg, x, y + height * 0.55f, x, y + height,
                                       nvgRGBA(0, 0, 0, 0), nvgRGBA(0, 0, 0, 190));
    nvgBeginPath(vg);
    nvgRect(vg, x, y + height * 0.55f, width, height * 0.45f);
    nvgFillPaint(vg, scrim);
    nvgFill(vg);

    brls::Box::draw(vg, x, y, width, height, style, ctx);
}
