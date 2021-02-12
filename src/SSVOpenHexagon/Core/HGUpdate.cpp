// Copyright (c) 2013-2020 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: https://opensource.org/licenses/AFL-3.0

#include "SSVOpenHexagon/Utils/Utils.hpp"
#include "SSVOpenHexagon/Core/HexagonGame.hpp"
#include "SSVOpenHexagon/Core/Joystick.hpp"

#include <SSVStart/Utils/Vector2.hpp>

#include <SSVUtils/Core/Common/Frametime.hpp>

using namespace std;
using namespace sf;
using namespace ssvs;
using namespace ssvu;
using namespace hg::Utils;

namespace hg
{

void HexagonGame::update(ssvu::FT mFT)
{
    mFT *= Config::getTimescale();

    std::string nameStr = levelData->name;
    nameFormat(nameStr);
    const std::string diffStr = diffFormat(difficultyMult);
    const std::string timeStr = timeFormat(status.getTimeSeconds());

    constexpr float DELAY_TO_UPDATE = 5.f; // X seconds
    timeUntilRichPresenceUpdate -= ssvu::getFTToSeconds(mFT);

    if(timeUntilRichPresenceUpdate <= 0.f)
    {
        steamManager.set_rich_presence_in_game(nameStr, diffStr, timeStr);

        timeUntilRichPresenceUpdate = DELAY_TO_UPDATE;
    }

    updateRichPresenceCallbacks();
    updateText();
    updateFlash(mFT);
    effectTimelineManager.update(mFT);

    if(!mustReplayInput())
    {
        updateInput();
    }
    else
    {
        assert(activeReplay.has_value());

        if(!status.started)
        {
            start();
        }

        const input_bitset ib =
            activeReplay->replayPlayer.get_current_and_move_forward();

        if(ib[static_cast<unsigned int>(input_bit::left)])
        {
            inputMovement = -1;
        }
        else if(ib[static_cast<unsigned int>(input_bit::right)])
        {
            inputMovement = 1;
        }
        else
        {
            inputMovement = 0;
        }

        inputSwap = ib[static_cast<unsigned int>(input_bit::swap)];
        inputFocused = ib[static_cast<unsigned int>(input_bit::focus)];
    }

    updateKeyIcons();

    if(status.started)
    {
        player.update(*this, mFT);

        if(!status.hasDied)
        {
            const std::optional<bool> preventPlayerInput =
                runLuaFunctionIfExists<bool, float, int, bool, bool>("onInput",
                    mFT, getInputMovement(), getInputFocused(), getInputSwap());

            if(!preventPlayerInput.has_value() || !(*preventPlayerInput))
            {
                player.updateInput(*this, mFT);
            }

            player.updatePosition(*this, mFT);

            status.accumulateFrametime(mFT);
            if(levelStatus.scoreOverridden)
            {
                status.updateCustomScore(
                    lua.readVariable<float>(levelStatus.scoreOverride));
            }
            updateWalls(mFT);

            ssvu::eraseRemoveIf(
                walls, [](const CWall& w) { return w.isDead(); });

            cwManager.cleanup();

            updateEvents(mFT);
            updateIncrement();

            if(mustChangeSides && walls.empty())
            {
                sideChange(
                    rng.get_int(levelStatus.sidesMin, levelStatus.sidesMax));
            }

            updateLevel(mFT);
            updateCustomWalls(mFT);

            if(Config::getBeatPulse())
            {
                updateBeatPulse(mFT);
            }

            if(Config::getPulse())
            {
                updatePulse(mFT);
            }

            if(!Config::getBlackAndWhite())
            {
                styleData.update(mFT, pow(difficultyMult, 0.8f));
            }
        }
        else
        {
            levelStatus.rotationSpeed *= 0.99f;
        }

        if(Config::get3D())
        {
            update3D(mFT);
        }
        if(!Config::getNoRotation())
        {
            updateRotation(mFT);
        }
    }

    overlayCamera.update(mFT);
    backgroundCamera.update(mFT);

    if(status.started)
    {
        if(status.mustStateChange != StateChange::None)
        {
            fpsWatcher.disable();

            const bool executeLastReplay =
                status.mustStateChange == StateChange::MustReplay;

            if(!executeLastReplay && !assets.anyLocalProfileActive())
            {
                // If playing a replay from file, there is no local profile
                // active, so just go to the menu when attempting to restart the
                // level.

                goToMenu();
                return;
            }

            newGame(getPackId(), restartId, restartFirstTime, difficultyMult,
                executeLastReplay);
        }

        if(!status.scoreInvalid && Config::getOfficial() &&
            fpsWatcher.isLimitReached())
        {
            invalidateScore("PERFORMANCE ISSUES");
        }
        else if(!status.scoreInvalid && !Config::get3D() &&
                levelStatus._3DRequired)
        {
            invalidateScore("3D REQUIRED");
        }

        fpsWatcher.update();
    }
}

void HexagonGame::updateWalls(ssvu::FT mFT)
{
    cwManager.forCustomWalls([&](const CCustomWall& customWall) {
        // After *only* the player has moved, push in case of overlap.
        if(!customWall.getCanCollide() ||
            !customWall.isOverlapping(player.getPosition()))
        {
            return;
        }

        if(player.getJustSwapped())
        {
            player.kill(*this);
            steamManager.unlock_achievement("a22_swapdeath");
        }
        else if(player.push(*this, customWall, mFT))
        {
            player.kill(*this);
        }
    });

    // First round of collision check, player gets a chance to
    // escape if certain conditions are met.
    unsigned int i;
    const sf::Vector2f& pPosition{player.getPosition()};
    const unsigned int wSize{static_cast<unsigned int>(walls.size())};

    for(i = 0; i < wSize; ++i)
    {
        walls[i].update(*this, centerPos, mFT);

        // If there is no collision skip to the next wall.
        if(!walls[i].isOverlapping(pPosition))
        {
            continue;
        }

        // Kill after a swap or if player could not be pushed out to safety.
        if(player.getJustSwapped())
        {
            player.kill(*this);
            steamManager.unlock_achievement("a22_swapdeath");
        }
        else if(player.push(*this, walls[i], centerPos, mFT))
        {
            player.kill(*this);
        }

        break;
    }

    // If i == wSize it means there was no collision,
    // so we can stop here.
    if(i == wSize)
    {
        return;
    }

    // Recheck collision with walls that came before.
    const auto executeDeath = [this] {
        if(player.getJustSwapped())
        {
            steamManager.unlock_achievement("a22_swapdeath");
        }
        player.kill(*this);
    };

    for(unsigned int j{0}; j < i; ++j)
    {
        if(walls[j].isOverlapping(pPosition))
        {
            executeDeath();
        }
    }

    // Update and check collision on the remaining walls.
    ++i;
    while(i < wSize)
    {
        walls[i].update(*this, centerPos, mFT);

        if(walls[i].isOverlapping(pPosition))
        {
            executeDeath();
        }

        ++i;
    }
}

void HexagonGame::updateCustomWalls(ssvu::FT mFT)
{
    (void)mFT;

    const bool customWallCollision =
        cwManager.anyCustomWall([&](const CCustomWall& customWall) {
            return customWall.getCanCollide() &&
                   customWall.isOverlapping(player.getPosition());
        });

    if(customWallCollision)
    {
        player.kill(*this);
    }
}

void HexagonGame::start()
{
    status.start();
    messageText.setString("");
    assets.playSound("go.ogg");

    if(!mustReplayInput())
    {
        std::string nameStr = levelData->name;
        std::string packStr = getPackName();
        nameFormat(nameStr);
        nameFormat(packStr);
        const std::string diffStr = diffFormat(difficultyMult);

        discordManager.set_rich_presence_in_game(
            nameStr + " [x" + diffStr + "]", packStr);
    }
    else
    {
        discordManager.set_rich_presence_on_replay();
    }

    if(!Config::getNoMusic())
    {
        assets.musicPlayer.resume();
    }

    if(Config::getOfficial())
    {
        fpsWatcher.enable();
    }

    runLuaFunction<void>("onLoad");
}

void HexagonGame::updateInput()
{
    // Joystick support
    hg::Joystick::update();

    const bool jCW = hg::Joystick::rightPressed();
    const bool jCCW = hg::Joystick::leftPressed();

    if(!status.started && (!Config::getRotateToStart() || inputImplCCW ||
                              inputImplCW || inputImplBothCWCCW || jCW || jCCW))
    {
        start();
    }

    // Naive touch controls
    for(const auto& p : window.getFingerDownPositions())
    {
        if(p.x < window.getWidth() / 2.f)
        {
            inputImplCCW = 1;
        }
        else
        {
            inputImplCW = 1;
        }
    }

    if(inputImplCW && !inputImplCCW)
    {
        inputMovement = 1;
    }
    else if(!inputImplCW && inputImplCCW)
    {
        inputMovement = -1;
    }
    else if(inputImplCW && inputImplCCW)
    {
        if(!inputImplBothCWCCW)
        {
            if(inputMovement == 1 && inputImplLastMovement == 1)
            {
                inputMovement = -1;
            }
            else if(inputMovement == -1 && inputImplLastMovement == -1)
            {
                inputMovement = 1;
            }
        }
    }
    else
    {
        inputMovement = 0;

        // Joystick support
        {
            if(jCW && !jCCW)
            {
                inputMovement = 1;
            }
            else if(!jCW && jCCW)
            {
                inputMovement = -1;
            }
            else if(jCW && jCCW)
            {
                if(!inputImplBothCWCCW)
                {
                    if(inputMovement == 1 && inputImplLastMovement == 1)
                    {
                        inputMovement = -1;
                    }
                    else if(inputMovement == -1 && inputImplLastMovement == -1)
                    {
                        inputMovement = 1;
                    }
                }
            }
            else
            {
                inputMovement = 0;
            }
        }
    }

    // Replay support
    if(status.started && !status.hasDied)
    {
        const bool left = getInputMovement() == -1;
        const bool right = getInputMovement() == 1;
        const bool swap = getInputSwap();
        const bool focus = getInputFocused();

        lastReplayData.record_input(left, right, swap, focus);
    }

    // Joystick support
    if(hg::Joystick::exitRisingEdge())
    {
        goToMenu();
    }
    else if(hg::Joystick::forceRestartRisingEdge() ||
            (status.hasDied && hg::Joystick::restartRisingEdge()))
    {
        status.mustStateChange = StateChange::MustRestart;
    }
    else if(status.hasDied && hg::Joystick::replayRisingEdge())
    {
        status.mustStateChange = StateChange::MustReplay;
    }
}

void HexagonGame::updateEvents(ssvu::FT)
{
    if(const auto o =
            eventTimelineRunner.update(eventTimeline, status.getTimeTP());
        o == hg::Utils::timeline2_runner::outcome::finished)
    {
        eventTimeline.clear();
        eventTimelineRunner = {};
    }

    if(const auto o = messageTimelineRunner.update(
           messageTimeline, status.getCurrentTP());
        o == hg::Utils::timeline2_runner::outcome::finished)
    {
        messageTimeline.clear();
        messageTimelineRunner = {};
    }
}

void HexagonGame::updateIncrement()
{
    if(!levelStatus.incEnabled)
    {
        return;
    }

    if(status.getIncrementTimeSeconds() < levelStatus.incTime)
    {
        return;
    }

    ++levelStatus.currentIncrements;
    incrementDifficulty();
    status.resetIncrementTime();
    mustChangeSides = true;
}

void HexagonGame::updateLevel(ssvu::FT mFT)
{
    if(status.isTimePaused())
    {
        return;
    }

    runLuaFunction<float>("onUpdate", mFT);

    const auto o = timelineRunner.update(timeline, status.getTimeTP());

    if(o == hg::Utils::timeline2_runner::outcome::finished && !mustChangeSides)
    {
        timeline.clear();
        runLuaFunction<void>("onStep");
        timelineRunner = {};
    }
}
void HexagonGame::updatePulse(ssvu::FT mFT)
{
    if(status.pulseDelay <= 0 && status.pulseDelayHalf <= 0)
    {
        float pulseAdd{status.pulseDirection > 0 ? levelStatus.pulseSpeed
                                                 : -levelStatus.pulseSpeedR};
        float pulseLimit{status.pulseDirection > 0 ? levelStatus.pulseMax
                                                   : levelStatus.pulseMin};

        status.pulse += pulseAdd * mFT * getMusicDMSyncFactor();
        if((status.pulseDirection > 0 && status.pulse >= pulseLimit) ||
            (status.pulseDirection < 0 && status.pulse <= pulseLimit))
        {
            status.pulse = pulseLimit;
            status.pulseDirection *= -1;
            status.pulseDelayHalf = levelStatus.pulseDelayHalfMax;
            if(status.pulseDirection < 0)
            {
                status.pulseDelay = levelStatus.pulseDelayMax;
            }
        }
    }

    status.pulseDelay -= mFT;
    status.pulseDelayHalf -= mFT;

    float p{status.pulse / levelStatus.pulseMin};
    float rotation{backgroundCamera.getRotation()};
    backgroundCamera.setView({ssvs::zeroVec2f,
        {(Config::getWidth() * Config::getZoomFactor()) * p,
            (Config::getHeight() * Config::getZoomFactor()) * p}});
    backgroundCamera.setRotation(rotation);
}
void HexagonGame::updateBeatPulse(ssvu::FT mFT)
{
    if(status.beatPulseDelay <= 0)
    {
        status.beatPulse = levelStatus.beatPulseMax;
        status.beatPulseDelay = levelStatus.beatPulseDelayMax;
    }
    else
    {
        status.beatPulseDelay -= 1 * mFT * getMusicDMSyncFactor();
    }

    if(status.beatPulse > 0)
    {
        status.beatPulse -= (2.f * mFT * getMusicDMSyncFactor()) *
                            levelStatus.beatPulseSpeedMult;
    }

    float radiusMin{Config::getBeatPulse() ? levelStatus.radiusMin : 75};
    status.radius =
        radiusMin * (status.pulse / levelStatus.pulseMin) + status.beatPulse;
}
void HexagonGame::updateRotation(ssvu::FT mFT)
{
    auto nextRotation(getRotationSpeed() * 10.f);
    if(status.fastSpin > 0)
    {
        nextRotation +=
            abs((getSmootherStep(0, levelStatus.fastSpin, status.fastSpin) /
                    3.5f) *
                17.f) *
            getSign(nextRotation);
        status.fastSpin -= mFT;
    }

    backgroundCamera.turn(nextRotation);
}
void HexagonGame::updateFlash(ssvu::FT mFT)
{
    if(status.flashEffect > 0)
    {
        status.flashEffect -= 3 * mFT;
    }
    status.flashEffect = getClamped(status.flashEffect, 0.f, 255.f);
    for(auto i(0u); i < 4; ++i)
    {
        flashPolygon[i].color.a = status.flashEffect;
    }
}
void HexagonGame::update3D(ssvu::FT mFT)
{
    status.pulse3D += styleData._3dPulseSpeed * status.pulse3DDirection * mFT;
    if(status.pulse3D > styleData._3dPulseMax)
    {
        status.pulse3DDirection = -1;
    }
    else if(status.pulse3D < styleData._3dPulseMin)
    {
        status.pulse3DDirection = 1;
    }
}

} // namespace hg
