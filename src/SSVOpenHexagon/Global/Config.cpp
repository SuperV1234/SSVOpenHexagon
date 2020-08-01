// Copyright (c) 2013-2020 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: http://opensource.org/licenses/AFL-3.0

#include "SSVOpenHexagon/Global/Config.hpp"
#include "SSVOpenHexagon/Global/Assets.hpp"
#include "SSVOpenHexagon/Utils/Utils.hpp"
#include "SSVOpenHexagon/Online/Online.hpp"
#include "SSVOpenHexagon/SSVUtilsJson/SSVUtilsJson.hpp"

#include <SSVStart/Input/Input.hpp>
#include <SSVStart/GameSystem/GameWindow.hpp>

#include <iostream>
#include <fstream>
#include <memory>

using namespace std;
using namespace sf;
using namespace ssvs;
using namespace ssvs::Input;
using namespace ssvu::FileSystem;
using namespace ssvuj;
using namespace ssvu;

#define X_LINKEDVALUES                                                     \
    X(online, bool, "online")                                              \
    X(official, bool, "official")                                          \
    X(noRotation, bool, "no_rotation")                                     \
    X(noBackground, bool, "no_background")                                 \
    X(noSound, bool, "no_sound")                                           \
    X(noMusic, bool, "no_music")                                           \
    X(blackAndWhite, bool, "black_and_white")                              \
    X(pulseEnabled, bool, "pulse_enabled")                                 \
    X(_3DEnabled, bool, "3D_enabled")                                      \
    X(_3DMultiplier, float, "3D_multiplier")                               \
    X(_3DMaxDepth, unsigned int, "3D_max_depth")                           \
    X(invincible, bool, "invincible")                                      \
    X(autoRestart, bool, "auto_restart")                                   \
    X(soundVolume, float, "sound_volume")                                  \
    X(musicVolume, float, "music_volume")                                  \
    X(flashEnabled, bool, "flash_enabled")                                 \
    X(zoomFactor, float, "zoom_factor")                                    \
    X(pixelMultiplier, int, "pixel_multiplier")                            \
    X(playerSpeed, float, "player_speed")                                  \
    X(playerFocusSpeed, float, "player_focus_speed")                       \
    X(playerSize, float, "player_size")                                    \
    X(limitFPS, bool, "limit_fps")                                         \
    X(vsync, bool, "vsync")                                                \
    X(autoZoomFactor, bool, "auto_zoom_factor")                            \
    X(fullscreen, bool, "fullscreen")                                      \
    X(windowedAutoResolution, bool, "windowed_auto_resolution")            \
    X(fullscreenAutoResolution, bool, "fullscreen_auto_resolution")        \
    X(fullscreenWidth, unsigned int, "fullscreen_width")                   \
    X(fullscreenHeight, unsigned int, "fullscreen_height")                 \
    X(windowedWidth, unsigned int, "windowed_width")                       \
    X(windowedHeight, unsigned int, "windowed_height")                     \
    X(showMessages, bool, "show_messages")                                 \
    X(debug, bool, "debug")                                                \
    X(beatPulse, bool, "beatpulse_enabled")                                \
    X(showTrackedVariables, bool, "show_tracked_variables")                \
    X(musicSpeedDMSync, bool, "music_speed_dm_sync")                       \
    X(maxFPS, unsigned int, "max_fps")                                     \
    X(antialiasingLevel, unsigned int, "antialiasing_level")               \
    X(showFPS, bool, "show_fps")                                           \
    X(timerStatic, bool, "timer_static")                                   \
    X(serverLocal, bool, "server_local")                                   \
    X(serverVerbose, bool, "server_verbose")                               \
    X(mouseVisible, bool, "mouse_visible")                                 \
    X(musicSpeedMult, float, "music_speed_mult")                           \
    X(drawTextOutlines, bool, "draw_text_outlines")                        \
    X(darkenUnevenBackgroundChunk, bool, "darken_uneven_background_chunk") \
    X(rotateToStart, bool, "rotate_to_start")                              \
    X(joystickDeadzone, float, "joystick_deadzone")                        \
    X(textPadding, float, "text_padding")                                  \
    X(textScaling, float, "text_scaling")                                  \
    X(timescale, float, "timescale")                                       \
    X(showKeyIcons, bool, "show_key_icons")                                \
    X(keyIconsScale, float, "key_icons_scale")                             \
    X(firstTimePlaying, bool, "first_time_playing")                        \
    X(saveLocalBestReplayToFile, bool, "save_local_best_replay_to_file")   \
    X(triggerRotateCCW, Trigger, "t_rotate_ccw")                           \
    X(triggerRotateCW, Trigger, "t_rotate_cw")                             \
    X(triggerFocus, Trigger, "t_focus")                                    \
    X(triggerExit, Trigger, "t_exit")                                      \
    X(triggerForceRestart, Trigger, "t_force_restart")                     \
    X(triggerRestart, Trigger, "t_restart")                                \
    X(triggerReplay, Trigger, "t_replay")                                  \
    X(triggerScreenshot, Trigger, "t_screenshot")                          \
    X(triggerSwap, Trigger, "t_swap")                                      \
    X(triggerUp, Trigger, "t_up")                                          \
    X(triggerDown, Trigger, "t_down")

namespace hg::Config
{

[[nodiscard]] static ssvuj::Obj& root() noexcept
{
    static ssvuj::Obj res = [] {
        if(ssvufs::Path{"config.json"}.exists<ssvufs::Type::File>())
        {
            ssvu::lo("hg::Config::root()")
                << "User-defined `config.json` file found\n";

            return ssvuj::getFromFile("config.json");
        }
        else if(ssvufs::Path{"default_config.json"}
                    .exists<ssvufs::Type::File>())
        {
            ssvu::lo("hg::Config::root()")
                << "User `config.json` file not found, looking for default\n";

            ssvu::lo("hg::Config::root()")
                << "Default `default_config.json` file found\n";

            return ssvuj::getFromFile("default_config.json");
        }
        else
        {
            ssvu::lo("hg::Config::root()")
                << "FATAL ERROR: No suitable config file found\n";

            std::abort();
        }
    }();

    return res;
}

#define X(name, type, key)                                 \
    [[nodiscard]] static auto& name() noexcept             \
    {                                                      \
        static auto res = ::ssvuj::LinkedValue<type>(key); \
        return res;                                        \
    }
X_LINKEDVALUES
#undef X

static void syncAllFromObj()
{
#define X(name, type, key) name().syncFrom(root());
    X_LINKEDVALUES
#undef X
}

static void syncAllToObj()
{
#define X(name, type, key) name().syncTo(root());
    X_LINKEDVALUES
#undef X
}

float sizeX{1500}, sizeY{1500};
constexpr float spawnDistance{1600};
string uneligibilityReason;

void applyAutoWindowedResolution()
{
    auto d(VideoMode::getDesktopMode());
    windowedWidth() = d.width;
    windowedHeight() = d.height;
}
void applyAutoFullscreenResolution()
{
    auto d(VideoMode::getDesktopMode());
    fullscreenWidth() = d.width;
    fullscreenHeight() = d.height;
}

void loadConfig(const vector<string>& mOverridesIds)
{
    lo("::loadConfig") << "loading config\n";

    for(const auto p :
        getScan<ssvufs::Mode::Single, ssvufs::Type::File, ssvufs::Pick::ByExt>(
            "ConfigOverrides/", ".json"))
    {
        if(contains(mOverridesIds, p.getFileNameNoExtensions()))
        {
            const auto overrideRoot(getFromFile(p));
            for(auto itr(begin(overrideRoot)); itr != end(overrideRoot); ++itr)
            {
                root()[getKey(itr)] = *itr;
            }
        }
    }

    syncAllFromObj();

    if(getWindowedAutoResolution())
    {
        applyAutoWindowedResolution();
    }

    if(getFullscreenAutoResolution())
    {
        applyAutoFullscreenResolution();
    }

    recalculateSizes();
}

void saveConfig()
{
    lo("::saveConfig") << "saving config\n";
    syncAllToObj();
    writeToFile(root(), "config.json");
}

bool isEligibleForScore()
{
    if(!getOfficial())
    {
        uneligibilityReason = "official mode off";
        return false;
    }

    if(getDebug())
    {
        uneligibilityReason = "debug mode on";
        return false;
    }

    if(!getAutoZoomFactor())
    {
        uneligibilityReason = "modified zoom factor";
        return false;
    }

    if(getPlayerSpeed() != 9.45f)
    {
        uneligibilityReason = "player speed modified";
        return false;
    }

    if(getPlayerFocusSpeed() != 4.625f)
    {
        uneligibilityReason = "player focus speed modified";
        return false;
    }

    if(getPlayerSize() != 7.3f)
    {
        uneligibilityReason = "player size modified";
        return false;
    }

    if(getInvincible())
    {
        uneligibilityReason = "invincibility on";
        return false;
    }

    if(getNoRotation())
    {
        uneligibilityReason = "rotation off";
        return false;
    }

    return true;
}

void recalculateSizes()
{
    sizeX = sizeY = max(getWidth(), getHeight()) * 1.3f;
    if(!getAutoZoomFactor())
    {
        return;
    }

    const float factorX(1024.f / ssvu::toFloat(getWidth()));
    const float factorY(768.f / ssvu::toFloat(getHeight()));
    zoomFactor() = max(factorX, factorY);
}
void setFullscreen(GameWindow& mWindow, bool mFullscreen)
{
    fullscreen() = mFullscreen;

    mWindow.setSize(getWidth(), getHeight());
    mWindow.setFullscreen(getFullscreen());

    recalculateSizes();
}

void refreshWindowSize(unsigned int mWidth, unsigned int mHeight)
{
    windowedWidth() = mWidth;
    windowedHeight() = mHeight;
}

void setCurrentResolution(
    GameWindow& mWindow, unsigned int mWidth, unsigned int mHeight)
{
    if(fullscreen())
    {
        fullscreenAutoResolution() = false;
        fullscreenWidth() = mWidth;
        fullscreenHeight() = mHeight;
    }
    else
    {
        windowedAutoResolution() = false;
        windowedWidth() = mWidth;
        windowedHeight() = mHeight;
    }

    mWindow.setSize(getWidth(), getHeight());
    mWindow.setFullscreen(getFullscreen());

    recalculateSizes();
}
void setCurrentResolutionAuto(GameWindow& mWindow)
{
    if(fullscreen())
    {
        fullscreenAutoResolution() = true;
        applyAutoFullscreenResolution();
    }
    else
    {
        windowedAutoResolution() = true;
        applyAutoWindowedResolution();
    }

    mWindow.setSize(getWidth(), getHeight());
    mWindow.setFullscreen(getFullscreen());
    recalculateSizes();
}
void setVsync(ssvs::GameWindow& mWindow, bool mValue)
{
    vsync() = mValue;
    mWindow.setVsync(vsync());
}
void setLimitFPS(ssvs::GameWindow& mWindow, bool mValue)
{
    limitFPS() = mValue;
    mWindow.setFPSLimited(mValue);
}
void setMaxFPS(ssvs::GameWindow& mWindow, unsigned int mValue)
{
    maxFPS() = mValue;
    mWindow.setMaxFPS(mValue);
}
void setTimerStatic(ssvs::GameWindow& mWindow, bool mValue)
{
    timerStatic() = mValue;

    if(timerStatic())
    {
        mWindow.setTimer<ssvs::TimerStatic>(0.5f, 0.5f);
    }
    else
    {
        mWindow.setTimer<ssvs::TimerDynamic>();
        setLimitFPS(mWindow, true);
        setMaxFPS(mWindow, 200);
    }
}

void setAntialiasingLevel(ssvs::GameWindow& mWindow, unsigned int mValue)
{
    antialiasingLevel() = mValue;
    mWindow.setAntialiasingLevel(mValue);
}

void setOnline(bool mOnline)
{
    online() = mOnline;
}

void setOfficial(bool mOfficial)
{
    official() = mOfficial;
}

void setDebug(bool mDebug)
{
    debug() = mDebug;
}

void setNoRotation(bool mNoRotation)
{
    noRotation() = mNoRotation;
}

void setNoBackground(bool mNoBackground)
{
    noBackground() = mNoBackground;
}

void setBlackAndWhite(bool mBlackAndWhite)
{
    blackAndWhite() = mBlackAndWhite;
}

void setNoSound(bool mNoSound)
{
    noSound() = mNoSound;
}

void setNoMusic(bool mNoMusic)
{
    noMusic() = mNoMusic;
}

void setPulse(bool mPulse)
{
    pulseEnabled() = mPulse;
}

void set3D(bool m3D)
{
    _3DEnabled() = m3D;
}

void setInvincible(bool mInvincible)
{
    invincible() = mInvincible;
}

void setAutoRestart(bool mAutoRestart)
{
    autoRestart() = mAutoRestart;
}

void setSoundVolume(float mVolume)
{
    soundVolume() = mVolume;
}

void setMusicVolume(float mVolume)
{
    musicVolume() = mVolume;
}

void setFlash(bool mFlash)
{
    flashEnabled() = mFlash;
}

void setMusicSpeedDMSync(bool mValue)
{
    musicSpeedDMSync() = mValue;
}

void setShowFPS(bool mValue)
{
    showFPS() = mValue;
}

void setServerLocal(bool mValue)
{
    serverLocal() = mValue;
}

void setServerVerbose(bool mValue)
{
    serverVerbose() = mValue;
}

void setMouseVisible(bool mValue)
{
    mouseVisible() = mValue;
}

void setMusicSpeedMult(float mValue)
{
    musicSpeedMult() = mValue;
}

void setDrawTextOutlines(bool mX)
{
    drawTextOutlines() = mX;
}

void setDarkenUnevenBackgroundChunk(bool mX)
{
    darkenUnevenBackgroundChunk() = mX;
}

void setRotateToStart(bool mX)
{
    rotateToStart() = mX;
}

void setJoystickDeadzone(float mX)
{
    joystickDeadzone() = mX;
}

void setTextPadding(float mX)
{
    textPadding() = mX;
}

void setTextScaling(float mX)
{
    textScaling() = mX;
}

void setTimescale(float mX)
{
    timescale() = mX;
}

void setShowKeyIcons(bool mX)
{
    showKeyIcons() = mX;
}

void setKeyIconsScale(float mX)
{
    keyIconsScale() = mX;
}

void setFirstTimePlaying(bool mX)
{
    firstTimePlaying() = mX;
}

void setSaveLocalBestReplayToFile(bool mX)
{
    saveLocalBestReplayToFile() = mX;
}

[[nodiscard]] bool getOnline()
{
    return online();
}

[[nodiscard]] bool getOfficial()
{
    return official();
}

[[nodiscard]] string getUneligibilityReason()
{
    return uneligibilityReason;
}

[[nodiscard]] float getSizeX()
{
    return sizeX;
}

[[nodiscard]] float getSizeY()
{
    return sizeY;
}

[[nodiscard]] float getSpawnDistance()
{
    return spawnDistance;
}

[[nodiscard]] float getZoomFactor()
{
    return zoomFactor();
}

[[nodiscard]] int getPixelMultiplier()
{
    return pixelMultiplier();
}

[[nodiscard]] float getPlayerSpeed()
{
    return getOfficial() ? 9.45f : playerSpeed();
}

[[nodiscard]] float getPlayerFocusSpeed()
{
    return getOfficial() ? 4.625f : playerFocusSpeed();
}

[[nodiscard]] float getPlayerSize()
{
    return getOfficial() ? 7.3f : playerSize();
}

[[nodiscard]] bool getNoRotation()
{
    return getOfficial() ? false : noRotation();
}

[[nodiscard]] bool getNoBackground()
{
    return getOfficial() ? false : noBackground();
}

[[nodiscard]] bool getBlackAndWhite()
{
    return getOfficial() ? false : blackAndWhite();
}

[[nodiscard]] bool getNoSound()
{
    return noSound();
}

[[nodiscard]] bool getNoMusic()
{
    return noMusic();
}

[[nodiscard]] float getSoundVolume()
{
    return soundVolume();
}

[[nodiscard]] float getMusicVolume()
{
    return musicVolume();
}

[[nodiscard]] bool getLimitFPS()
{
    return limitFPS();
}

[[nodiscard]] bool getVsync()
{
    return vsync();
}

[[nodiscard]] bool getAutoZoomFactor()
{
    return getOfficial() ? true : autoZoomFactor();
}

[[nodiscard]] bool getFullscreen()
{
    return fullscreen();
}

[[nodiscard, gnu::const]] float getVersion()
{
    return 2.03f;
}

[[nodiscard, gnu::const]] const char* getVersionString()
{
    return "2.03";
}

[[nodiscard]] bool getWindowedAutoResolution()
{
    return windowedAutoResolution();
}

[[nodiscard]] bool getFullscreenAutoResolution()
{
    return fullscreenAutoResolution();
}

[[nodiscard]] unsigned int getFullscreenWidth()
{
    return fullscreenWidth();
}

[[nodiscard]] unsigned int getFullscreenHeight()
{
    return fullscreenHeight();
}

[[nodiscard]] unsigned int getWindowedWidth()
{
    return windowedWidth();
}

[[nodiscard]] unsigned int getWindowedHeight()
{
    return windowedHeight();
}

[[nodiscard]] unsigned int getWidth()
{
    return getFullscreen() ? getFullscreenWidth() : getWindowedWidth();
}

[[nodiscard]] unsigned int getHeight()
{
    return getFullscreen() ? getFullscreenHeight() : getWindowedHeight();
}

[[nodiscard]] bool getShowMessages()
{
    return showMessages();
}

[[nodiscard]] bool getDebug()
{
    return getOfficial() ? false : debug();
}

[[nodiscard]] bool getPulse()
{
    return getOfficial() ? true : pulseEnabled();
}

[[nodiscard]] bool getBeatPulse()
{
    return getOfficial() ? true : beatPulse();
}

[[nodiscard]] bool getInvincible()
{
    return getOfficial() ? false : invincible();
}

[[nodiscard]] bool get3D()
{
    return _3DEnabled();
}

[[nodiscard]] float get3DMultiplier()
{
    return _3DMultiplier();
}

[[nodiscard]] unsigned int get3DMaxDepth()
{
    return _3DMaxDepth();
}

[[nodiscard]] bool getAutoRestart()
{
    return autoRestart();
}

[[nodiscard]] bool getFlash()
{
    return flashEnabled();
}

[[nodiscard]] bool getShowTrackedVariables()
{
    return showTrackedVariables();
}

[[nodiscard]] bool getMusicSpeedDMSync()
{
    return musicSpeedDMSync();
}

[[nodiscard]] unsigned int getMaxFPS()
{
    return maxFPS();
}

[[nodiscard]] unsigned int getAntialiasingLevel()
{
    return antialiasingLevel();
}

[[nodiscard]] bool getShowFPS()
{
    return showFPS();
}

[[nodiscard]] bool getTimerStatic()
{
    return timerStatic();
}

[[nodiscard]] bool getServerLocal()
{
    return serverLocal();
}

[[nodiscard]] bool getServerVerbose()
{
    return serverVerbose();
}

[[nodiscard]] bool getMouseVisible()
{
    return mouseVisible();
}

[[nodiscard]] float getMusicSpeedMult()
{
    return musicSpeedMult();
}

[[nodiscard]] bool getDrawTextOutlines()
{
    return drawTextOutlines();
}

[[nodiscard]] bool getDarkenUnevenBackgroundChunk()
{
    return darkenUnevenBackgroundChunk();
}

[[nodiscard]] bool getRotateToStart()
{
    return rotateToStart();
}

[[nodiscard]] float getJoystickDeadzone()
{
    return joystickDeadzone();
}

[[nodiscard]] float getTextPadding()
{
    return textPadding();
}

[[nodiscard]] float getTextScaling()
{
    return textScaling();
}

[[nodiscard]] float getTimescale()
{
    return getOfficial() ? 1.f : timescale();
}

[[nodiscard]] bool getShowKeyIcons()
{
    return showKeyIcons();
}

[[nodiscard]] float getKeyIconsScale()
{
    return keyIconsScale();
}

[[nodiscard]] bool getFirstTimePlaying()
{
    return firstTimePlaying();
}

[[nodiscard]] bool getSaveLocalBestReplayToFile()
{
    return saveLocalBestReplayToFile();
}

[[nodiscard]] Trigger getTriggerRotateCCW()
{
    return triggerRotateCCW();
}

[[nodiscard]] Trigger getTriggerRotateCW()
{
    return triggerRotateCW();
}

[[nodiscard]] Trigger getTriggerFocus()
{
    return triggerFocus();
}

[[nodiscard]] Trigger getTriggerExit()
{
    return triggerExit();
}

[[nodiscard]] Trigger getTriggerForceRestart()
{
    return triggerForceRestart();
}

[[nodiscard]] Trigger getTriggerRestart()
{
    return triggerRestart();
}

[[nodiscard]] Trigger getTriggerReplay()
{
    return triggerReplay();
}

[[nodiscard]] Trigger getTriggerScreenshot()
{
    return triggerScreenshot();
}

[[nodiscard]] Trigger getTriggerSwap()
{
    return triggerSwap();
}

[[nodiscard]] Trigger getTriggerUp()
{
    return triggerUp();
}

[[nodiscard]] Trigger getTriggerDown()
{
    return triggerDown();
}

} // namespace hg::Config

#undef X_LINKEDVALUES
