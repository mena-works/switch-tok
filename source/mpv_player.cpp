#include "mpv_player.hpp"

#include <atomic>

#include <borealis.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>

#include "clip_cache.hpp"

// nanovg can adopt an existing GL texture instead of owning the pixels. The
// symbol is backend-specific and nanovg_gl.h cannot be included here without
// pulling in a second copy of the implementation, so declare it directly.
// Flip to the GLES3 variant if borealis was built with -DUSE_GLES.
extern "C"
{
    int nvglCreateImageFromHandleGL3(NVGcontext* ctx, unsigned int texture, int w, int h, int flags);
    int nvglCreateImageFromHandleGLES3(NVGcontext* ctx, unsigned int texture, int w, int h, int flags);
}

#ifdef USE_GLES
#define nvglCreateImageFromHandle nvglCreateImageFromHandleGLES3
#else
#define nvglCreateImageFromHandle nvglCreateImageFromHandleGL3
#endif

namespace
{

// nanovg must not free a texture it did not allocate.
constexpr int kNvgImageNoDelete = 1 << 16;

// Set from mpv's render thread, consumed on the UI thread.
std::atomic<bool> gFramePending { false };

void* getProcAddress(void*, const char* name)
{
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

void onRenderUpdate(void*)
{
    gFramePending.store(true, std::memory_order_release);
}

void checkError(int status, const char* what)
{
    if (status < 0)
        brls::Logger::error("mpv: {} failed: {}", what, mpv_error_string(status));
}

} // namespace

MpvPlayer& MpvPlayer::instance()
{
    static MpvPlayer player;
    return player;
}

bool MpvPlayer::init()
{
    mpv = mpv_create();
    if (!mpv)
    {
        brls::Logger::error("mpv_create failed");
        return false;
    }

    // The Switch has no terminal and no config dir worth reading.
    mpv_set_option_string(mpv, "terminal", "no");
    mpv_set_option_string(mpv, "config", "no");
    mpv_set_option_string(mpv, "idle", "yes");
    mpv_set_option_string(mpv, "vo", "libmpv");

    // Tegra X1: homebrew gets cores 0-2, and NVDEC is not wired into this
    // build, so everything is software decoded. Keep the server-side transcode
    // modest (see server/README) rather than fighting this here.
    mpv_set_option_string(mpv, "hwdec", "no");
    mpv_set_option_string(mpv, "vd-lavc-threads", "4");
    mpv_set_option_string(mpv, "vd-lavc-skiploopfilter", "nonkey");

    // Taken from wiliwili, which runs this same borealis + libmpv pairing on
    // Switch. Direct rendering off keeps the decoder out of buffers we hand to
    // the render API, and the explicit glFinish is their documented fix for
    // random crashes in the GL interop -- cause unknown, but this build is the
    // one it was found on.
    mpv_set_option_string(mpv, "vd-lavc-dr", "no");
    mpv_set_option_string(mpv, "opengl-glfinish", "yes");

    // Feed behaviour: every clip loops until the user swipes away.
    mpv_set_option_string(mpv, "loop-file", "inf");
    mpv_set_option_string(mpv, "keep-open", "yes");

    mpv_set_option_string(mpv, "cache", "yes");
    mpv_set_option_string(mpv, "demuxer-max-bytes", "16MiB");
    mpv_set_option_string(mpv, "network-timeout", "10");
    mpv_set_option_string(mpv, "audio-channels", "stereo");

    // Known cosmetic issue: every loop boundary logs
    //   ao/hos: audio end or underrun -> starting AO -> Error writing audio
    // once per lap. Audible playback is unaffected.
    //
    // audio-stream-silence=yes was tried as a fix and did not help; mpv also
    // warns that it "will break certain player behavior", so it is not worth
    // carrying for a benefit it did not deliver. Left alone deliberately.

    const int status = mpv_initialize(mpv);
    if (status < 0)
    {
        brls::Logger::error("mpv_initialize failed: {}", mpv_error_string(status));
        mpv_destroy(mpv);
        mpv = nullptr;
        return false;
    }

    mpv_request_log_messages(mpv, "warn");

    ClipCache::instance().attach(mpv);
    mpv_observe_property(mpv, 0, "dwidth", MPV_FORMAT_INT64);
    mpv_observe_property(mpv, 0, "dheight", MPV_FORMAT_INT64);
    mpv_observe_property(mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv, 0, "core-idle", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv, 0, "paused-for-cache", MPV_FORMAT_FLAG);

    mpv_opengl_init_params glParams { getProcAddress, nullptr };
    int advancedControl = 0;

    mpv_render_param params[] = {
        { MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL) },
        { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glParams },
        { MPV_RENDER_PARAM_ADVANCED_CONTROL, &advancedControl },
        { MPV_RENDER_PARAM_INVALID, nullptr },
    };

    const int renderStatus = mpv_render_context_create(&renderCtx, mpv, params);
    if (renderStatus < 0)
    {
        brls::Logger::error("mpv_render_context_create failed: {}", mpv_error_string(renderStatus));
        mpv_destroy(mpv);
        mpv = nullptr;
        return false;
    }

    mpv_render_context_set_update_callback(renderCtx, onRenderUpdate, nullptr);

    brls::Logger::info("mpv {} ready", mpv_client_api_version());
    return true;
}

void MpvPlayer::exit()
{
    if (renderCtx)
    {
        mpv_render_context_set_update_callback(renderCtx, nullptr, nullptr);
        mpv_render_context_free(renderCtx);
        renderCtx = nullptr;
    }

    if (mpv)
    {
        mpv_terminate_destroy(mpv);
        mpv = nullptr;
    }

    // The GL objects belong to the window that is being torn down anyway.
    fbo = texture = 0;
    nvgImage = -1;
}

float MpvPlayer::progress() const
{
    if (duration <= 0.0)
        return 0.0f;

    const float value = static_cast<float>(timePos / duration);
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

void MpvPlayer::play(const std::string& url)
{
    if (!mpv)
        return;

    // Clear the old clip's state now rather than waiting for mpv to report the
    // new one, so the bar does not briefly show the previous video's progress.
    //
    // Deliberately NOT clearing videoWidth/videoHeight: mpv reports dwidth and
    // dheight only when they change, so a clip the same size as the last one
    // produces no event. Zeroing them here left the size unknown, and since the
    // render target is sized from it, those clips played audio over a black
    // screen. Stale dimensions for a frame or two are the cheaper wrong answer.
    timePos = duration = 0.0;
    buffering          = true;

    const char* cmd[] = { "loadfile", url.c_str(), "replace", nullptr };
    checkError(mpv_command(mpv, cmd), "loadfile");
}

void MpvPlayer::stop()
{
    if (!mpv)
        return;

    const char* cmd[] = { "stop", nullptr };
    mpv_command(mpv, cmd);
    videoWidth = videoHeight = 0;
}

void MpvPlayer::setPaused(bool paused)
{
    if (!mpv)
        return;

    int flag = paused ? 1 : 0;
    mpv_set_property(mpv, "pause", MPV_FORMAT_FLAG, &flag);
}

void MpvPlayer::setVolume(int volume)
{
    if (!mpv)
        return;

    volumeLevel = volume < 0 ? 0 : (volume > 100 ? 100 : volume);

    double value = volumeLevel;
    mpv_set_property(mpv, "volume", MPV_FORMAT_DOUBLE, &value);
}

void MpvPlayer::adjustVolume(int delta)
{
    setVolume(volumeLevel + delta);

    // Nudging the volume up from silence should be audible, not silently
    // ignored because mute is still latched.
    if (delta > 0 && isMuted)
        toggleMute();
}

void MpvPlayer::toggleMute()
{
    if (!mpv)
        return;

    isMuted   = !isMuted;
    int flag  = isMuted ? 1 : 0;
    mpv_set_property(mpv, "mute", MPV_FORMAT_FLAG, &flag);
}

void MpvPlayer::seekToFraction(float fraction)
{
    if (!mpv)
        return;

    const float clamped = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);

    const std::string percent = std::to_string(clamped * 100.0f);
    const char*       cmd[]   = { "seek", percent.c_str(), "absolute-percent", nullptr };
    mpv_command(mpv, cmd);
}

void MpvPlayer::seek(double seconds)
{
    if (!mpv)
        return;

    const std::string amount = std::to_string(seconds);
    const char*       cmd[]  = { "seek", amount.c_str(), "relative", nullptr };
    mpv_command(mpv, cmd);
}

void MpvPlayer::update()
{
    if (!mpv)
        return;

    while (true)
    {
        mpv_event* event = mpv_wait_event(mpv, 0.0);
        if (!event || event->event_id == MPV_EVENT_NONE)
            break;

        switch (event->event_id)
        {
            case MPV_EVENT_PROPERTY_CHANGE:
            {
                auto* prop = static_cast<mpv_event_property*>(event->data);
                if (!prop->data)
                    break;

                const std::string name = prop->name;

                if (prop->format == MPV_FORMAT_INT64)
                {
                    const int value = static_cast<int>(*static_cast<int64_t*>(prop->data));
                    if (name == "dwidth")
                        videoWidth = value;
                    else if (name == "dheight")
                        videoHeight = value;
                }
                else if (prop->format == MPV_FORMAT_DOUBLE)
                {
                    const double value = *static_cast<double*>(prop->data);
                    if (name == "time-pos")
                        timePos = value;
                    else if (name == "duration")
                        duration = value;
                }
                else if (prop->format == MPV_FORMAT_FLAG)
                {
                    const bool value = *static_cast<int*>(prop->data) != 0;
                    if (name == "core-idle")
                        coreIdle = value;
                    else if (name == "paused-for-cache")
                        cacheWait = value;

                    // core-idle is also true while deliberately paused, so a
                    // frame on screen means we are not waiting on anything.
                    buffering = cacheWait || (coreIdle && videoWidth <= 0);
                }
                break;
            }

            case MPV_EVENT_LOG_MESSAGE:
            {
                auto* msg = static_cast<mpv_event_log_message*>(event->data);

                std::string text = msg->text;
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                    text.pop_back();

                if (text.empty())
                    break;

                // Carry mpv's own severity across instead of flattening the
                // verbose audio tracing into a wall of warnings.
                switch (msg->log_level)
                {
                    case MPV_LOG_LEVEL_FATAL:
                    case MPV_LOG_LEVEL_ERROR:
                        brls::Logger::error("mpv/{}: {}", msg->prefix, text);
                        break;
                    case MPV_LOG_LEVEL_WARN:
                        brls::Logger::warning("mpv/{}: {}", msg->prefix, text);
                        break;
                    default:
                        brls::Logger::debug("mpv/{}: {}", msg->prefix, text);
                        break;
                }
                break;
            }

            case MPV_EVENT_FILE_LOADED:
            {
                // Belt and braces for the same problem: ask outright rather
                // than waiting for a change notification that may never come.
                int64_t value = 0;
                if (mpv_get_property(mpv, "dwidth", MPV_FORMAT_INT64, &value) >= 0 && value > 0)
                    videoWidth = static_cast<int>(value);
                if (mpv_get_property(mpv, "dheight", MPV_FORMAT_INT64, &value) >= 0 && value > 0)
                    videoHeight = static_cast<int>(value);
                break;
            }

            case MPV_EVENT_END_FILE:
            {
                auto* end = static_cast<mpv_event_end_file*>(event->data);
                if (end->reason == MPV_END_FILE_REASON_ERROR)
                    brls::Logger::error("mpv playback error: {}", mpv_error_string(end->error));
                break;
            }

            default:
                break;
        }
    }
}

bool MpvPlayer::ensureTarget(NVGcontext* vg, int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    if (fbo != 0 && width == targetWidth && height == targetHeight)
        return true;

    releaseTarget(vg);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

    const GLenum complete = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));

    if (complete != GL_FRAMEBUFFER_COMPLETE)
    {
        brls::Logger::error("video FBO incomplete: 0x{:x}", complete);
        releaseTarget(vg);
        return false;
    }

    nvgImage = nvglCreateImageFromHandle(vg, texture, width, height, kNvgImageNoDelete);
    targetWidth  = width;
    targetHeight = height;
    return nvgImage >= 0;
}

void MpvPlayer::releaseTarget(NVGcontext* vg)
{
    if (nvgImage >= 0)
    {
        nvgDeleteImage(vg, nvgImage);
        nvgImage = -1;
    }
    if (fbo)
    {
        glDeleteFramebuffers(1, &fbo);
        fbo = 0;
    }
    if (texture)
    {
        glDeleteTextures(1, &texture);
        texture = 0;
    }
    targetWidth = targetHeight = 0;
}

int MpvPlayer::frameImage(NVGcontext* vg)
{
    if (!renderCtx || videoWidth <= 0 || videoHeight <= 0)
        return -1;

    const int width  = videoWidth;
    const int height = videoHeight;

    if (!ensureTarget(vg, width, height))
        return -1;

    if (!gFramePending.exchange(false, std::memory_order_acq_rel))
        return nvgImage;  // nothing new; the previous frame is still on the texture

    // mpv issues raw GL and does not restore our bindings, so bracket the call.
    GLint prevFbo = 0;
    GLint prevViewport[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    mpv_opengl_fbo target { static_cast<int>(fbo), width, height, 0 };

    // No flip: nanovg's sampling of the adopted texture already matches what
    // mpv writes. Asking mpv to flip on top of that renders the frame upside
    // down.
    int flipY = 0;

    mpv_render_param params[] = {
        { MPV_RENDER_PARAM_OPENGL_FBO, &target },
        { MPV_RENDER_PARAM_FLIP_Y, &flipY },
        { MPV_RENDER_PARAM_INVALID, nullptr },
    };

    mpv_render_context_render(renderCtx, params);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    // nanovg caches GL state of its own; force it to rebind on the next draw.
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    return nvgImage;
}
