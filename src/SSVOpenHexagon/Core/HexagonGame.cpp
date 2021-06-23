// Copyright (c) 2013-2020 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: https://opensource.org/licenses/AFL-3.0

#include "SSVOpenHexagon/Core/HexagonGame.hpp"

#include "SSVOpenHexagon/Components/CWall.hpp"
#include "SSVOpenHexagon/Global/Assert.hpp"
#include "SSVOpenHexagon/Global/Assets.hpp"
#include "SSVOpenHexagon/Global/Audio.hpp"
#include "SSVOpenHexagon/Global/Config.hpp"
#include "SSVOpenHexagon/Core/HexagonClient.hpp"
#include "SSVOpenHexagon/Core/Joystick.hpp"
#include "SSVOpenHexagon/Core/Steam.hpp"
#include "SSVOpenHexagon/Core/Discord.hpp"
#include "SSVOpenHexagon/Core/Discord.hpp"
#include "SSVOpenHexagon/Utils/Utils.hpp"
#include "SSVOpenHexagon/Utils/Concat.hpp"
#include "SSVOpenHexagon/Utils/LevelValidator.hpp"
#include "SSVOpenHexagon/Utils/LuaWrapper.hpp"
#include "SSVOpenHexagon/Utils/String.hpp"

#include <imgui.h>
#include <imgui-SFML.h>

#include <SSVStart/Utils/Input.hpp>
#include <SSVStart/Utils/Vector2.hpp>
#include <SSVStart/Utils/SFML.hpp>
#include <SSVStart/Input/Trigger.hpp>

#include <SSVUtils/Core/Common/Frametime.hpp>
#include <SSVUtils/Core/Log/Log.hpp>

#include <SFML/Graphics.hpp>

#include <cmath>
#include <chrono>

namespace hg {

namespace {

[[nodiscard]] double getReplayScore(const HexagonGameStatus& status)
{
    return status.getCustomScore() != 0.f
               ? status.getCustomScore()
               : status.getPlayedAccumulatedFrametime();
}

[[nodiscard]] random_number_generator initializeRng()
{
    const random_number_generator::seed_type seed = ssvu::getRndEngine()();
    return random_number_generator{seed};
}

} // namespace

HexagonGame::ActiveReplay::ActiveReplay(const replay_file& mReplayFile)
    : replayFile{mReplayFile}, replayPlayer{replayFile._data}
{}

void HexagonGame::createWall(int mSide, float mThickness,
    const SpeedData& mSpeed, const SpeedData& mCurve, float mHueMod)
{
    walls.emplace_back(getSides(), getWallAngleLeft(), getWallAngleRight(),
        getWallSkewLeft(), getWallSkewRight(), centerPos, mSide, mThickness,
        levelStatus.wallSpawnDistance, mSpeed, mCurve, mHueMod);
}

void HexagonGame::initKeyIcons()
{
    if(window == nullptr)
    {
        return;
    }

    for(const auto& t :
        {"keyArrow.png", "keyFocus.png", "keySwap.png", "replayIcon.png"})
    {
        assets.getTextureOrNullTexture(t).setSmooth(true);
    }

    keyIconLeft.setTexture(assets.getTextureOrNullTexture("keyArrow.png"));
    keyIconRight.setTexture(assets.getTextureOrNullTexture("keyArrow.png"));
    keyIconFocus.setTexture(assets.getTextureOrNullTexture("keyFocus.png"));
    keyIconSwap.setTexture(assets.getTextureOrNullTexture("keySwap.png"));
    replayIcon.setTexture(assets.getTextureOrNullTexture("replayIcon.png"));

    updateKeyIcons();
}

void HexagonGame::updateKeyIcons()
{
    if(window == nullptr)
    {
        return;
    }

    constexpr float halfSize = 32.f;
    constexpr float size = halfSize * 2.f;

    keyIconLeft.setOrigin({halfSize, halfSize});
    keyIconRight.setOrigin({halfSize, halfSize});
    keyIconFocus.setOrigin({halfSize, halfSize});
    keyIconSwap.setOrigin({halfSize, halfSize});

    keyIconLeft.setRotation(180);

    const float scaling = Config::getKeyIconsScale() / Config::getZoomFactor();

    keyIconLeft.setScale(scaling, scaling);
    keyIconRight.setScale(scaling, scaling);
    keyIconFocus.setScale(scaling, scaling);
    keyIconSwap.setScale(scaling, scaling);

    const float scaledHalfSize = halfSize * scaling;
    const float scaledSize = size * scaling;
    const float padding = 8.f * scaling;
    const float finalPadding = scaledSize + padding;
    const sf::Vector2f finalPaddingX{finalPadding, 0.f};

    const sf::Vector2f bottomRight{
        Config::getWidth() - padding - scaledHalfSize,
        Config::getHeight() - padding - scaledHalfSize};

    keyIconSwap.setPosition(bottomRight);
    keyIconFocus.setPosition(keyIconSwap.getPosition() - finalPaddingX);
    keyIconRight.setPosition(keyIconFocus.getPosition() - finalPaddingX);
    keyIconLeft.setPosition(keyIconRight.getPosition() - finalPaddingX);

    // ------------------------------------------------------------------------

    replayIcon.setOrigin({size, size});
    replayIcon.setScale(scaling / 2.f, scaling / 2.f);

    const sf::Vector2f topRight{Config::getWidth() - padding - scaledHalfSize,
        padding + scaledHalfSize};

    replayIcon.setPosition(topRight);
}

void HexagonGame::updateLevelInfo()
{
    if(window == nullptr)
    {
        return;
    }

    const float levelInfoScaling = 1.f;
    const float scaling = levelInfoScaling / Config::getZoomFactor();
    const float padding = 8.f * scaling;

    const sf::Vector2f size{325.f, 75.f};
    const sf::Vector2f halfSize{size / 2.f};
    const sf::Vector2f scaledHalfSize{halfSize * scaling};

    levelInfoRectangle.setSize(size);
    levelInfoRectangle.setScale(scaling, scaling);

    const sf::Color offsetColor{
        Config::getBlackAndWhite() || styleData.getColors().empty()
            ? sf::Color::Black
            : styleData.getColor(0)};

    levelInfoRectangle.setFillColor(offsetColor);
    levelInfoRectangle.setOutlineColor(styleData.getMainColor());
    levelInfoRectangle.setOrigin(halfSize);
    levelInfoRectangle.setOutlineThickness(3.f);

    const sf::Vector2f bottomLeft{padding + scaledHalfSize.x,
        Config::getHeight() - padding - scaledHalfSize.y};

    levelInfoRectangle.setPosition(bottomLeft);

    const float tPadding = padding;

    const auto trim = [](std::string s)
    {
        if(s.size() > 26)
        {
            return s.substr(0, 26);
        }

        return s;
    };

    levelInfoTextLevel.setFillColor(styleData.getMainColor());
    levelInfoTextLevel.setCharacterSize(20.f / Config::getZoomFactor());
    levelInfoTextLevel.setString(trim(Utils::toUppercase(levelData->name)));
    levelInfoTextLevel.setOrigin(ssvs::getLocalNW(levelInfoTextLevel));
    levelInfoTextLevel.setPosition(ssvs::getGlobalNW(levelInfoRectangle) +
                                   sf::Vector2f{tPadding, tPadding});

    const auto prepareText = [&](sf::Text& text, const float characterSize,
                                 const std::string& string)
    {
        text.setFillColor(styleData.getTextColor());
        text.setCharacterSize(characterSize / Config::getZoomFactor());
        text.setString(string);
    };

    prepareText(
        levelInfoTextPack, 14.f, trim(Utils::toUppercase(getPackName())));
    levelInfoTextPack.setOrigin(ssvs::getLocalNW(levelInfoTextPack));
    levelInfoTextPack.setPosition(
        ssvs::getGlobalSW(levelInfoTextLevel) + sf::Vector2f{0.f, tPadding});

    prepareText(
        levelInfoTextAuthor, 20.f, trim(Utils::toUppercase(getPackAuthor())));
    levelInfoTextAuthor.setOrigin(ssvs::getLocalSE(levelInfoTextAuthor));
    levelInfoTextAuthor.setPosition(ssvs::getGlobalSE(levelInfoRectangle) -
                                    sf::Vector2f{tPadding, tPadding});

    prepareText(levelInfoTextBy, 12.f, "BY");
    levelInfoTextBy.setOrigin(ssvs::getLocalSE(levelInfoTextBy));
    levelInfoTextBy.setPosition(
        ssvs::getGlobalSW(levelInfoTextAuthor) - sf::Vector2f{tPadding, 0.f});

    if(levelData->difficultyMults.size() > 1)
    {
        prepareText(levelInfoTextDM, 14.f, diffFormat(difficultyMult) + "x");
        levelInfoTextDM.setOrigin(ssvs::getLocalSW(levelInfoTextDM));
        levelInfoTextDM.setPosition(ssvs::getGlobalSW(levelInfoRectangle) +
                                    sf::Vector2f{tPadding, -tPadding});
    }
    else
    {
        levelInfoTextDM.setString("");
    }
}

void HexagonGame::nameFormat(std::string& name)
{
    name[0] = std::toupper(name[0]);
}

[[nodiscard]] std::string HexagonGame::diffFormat(float diff)
{
    char buf[255];
    std::snprintf(buf, sizeof(buf), "%g", diff);
    return buf;
}

[[nodiscard]] std::string HexagonGame::timeFormat(float time)
{
    char buf[255];
    std::snprintf(buf, sizeof(buf), "%.3f", time);
    return buf;
}

[[nodiscard]] bool HexagonGame::imguiLuaConsoleHasInput()
{
    return ilcShowConsole && (ImGui::GetIO().WantCaptureKeyboard ||
                                 ImGui::GetIO().WantCaptureMouse);
}

[[nodiscard]] static sf::Text initText(
    const char* text, const sf::Font& font, const float characterSize)
{
    return sf::Text{text, font,
        ssvu::toNum<unsigned int>(characterSize / Config::getZoomFactor())};
}

HexagonGame::HexagonGame(Steam::steam_manager* mSteamManager,
    Discord::discord_manager* mDiscordManager, HGAssets& mAssets, Audio* mAudio,
    ssvs::GameWindow* mGameWindow, HexagonClient* mHexagonClient)
    : steamManager(mSteamManager),
      discordManager(mDiscordManager),
      assets(mAssets),
      font{assets.getFontOrNullFont("OpenSquare-Regular.ttf")},
      fontBold{assets.getFontOrNullFont("OpenSquare-Bold.ttf")},
      audio(mAudio),
      window(mGameWindow),
      hexagonClient{mHexagonClient},
      player{ssvs::zeroVec2f, getSwapCooldown(), Config::getPlayerSize(),
          Config::getPlayerSpeed(), Config::getPlayerFocusSpeed()},
      levelStatus{Config::getMusicSpeedDMSync(), Config::getSpawnDistance()},
      messageText{initText("", font, 38.f)},
      pbText{initText("", fontBold, 65.f)},
      levelInfoTextLevel{"", font},
      levelInfoTextPack{"", font},
      levelInfoTextAuthor{"", font},
      levelInfoTextBy{"", font},
      levelInfoTextDM{"", font},
      rng{initializeRng()},
      fpsText{initText("0", font, 25.f)},
      timeText{initText("0", fontBold, 70.f)},
      text{initText("", font, 25.f)},
      replayText{initText("", font, 20.f)}
{
    if(window != nullptr)
    {
        const float width = Config::getWidth();
        const float height = Config::getHeight();
        const float zoomFactor = Config::getZoomFactor();

        backgroundCamera.emplace(*window,
            sf::View{ssvs::zeroVec2f,
                sf::Vector2f{width * zoomFactor, height * zoomFactor}});

        overlayCamera.emplace(
            *window, sf::View{sf::Vector2f{width / 2.f, height / 2.f},
                         sf::Vector2f{width, height}});
    }


    game.onUpdate +=
        [this](ssvu::FT mFT) { update(mFT, Config::getTimescale()); };

    game.onPostUpdate += [this] { postUpdate(); };

    game.onDraw += [this] { draw(); };

    game.onAnyEvent +=
        [](const sf::Event& event) { ImGui::SFML::ProcessEvent(event); };

    if(window != nullptr)
    {
        window->onRecreation += [this]
        {
            initFlashEffect();
            initKeyIcons();
        };
    }

    // ------------------------------------------------------------------------
    // Keyboard binds

    Config::keyboardBindsSanityCheck();

    using Tid = Config::Tid;

    const auto addTidInput =
        [&](const Tid tid, const ssvs::Input::Type type, auto action)
    {
        game.addInput(
            Config::getTrigger(tid), action, type, static_cast<int>(tid));
    };

    const auto addTid2StateInput = [&](const Tid tid, bool& value)
    {
        add2StateInput(
            game, Config::getTrigger(tid), value, static_cast<int>(tid));
    };

    addTid2StateInput(Tid::RotateCCW, inputImplCCW);
    addTid2StateInput(Tid::RotateCW, inputImplCW);
    addTid2StateInput(Tid::Focus, inputFocused);
    addTid2StateInput(Tid::Swap, inputSwap);

    const auto notInConsole = [this](auto&& f)
    {
        return [this, f](ssvu::FT /*unused*/)
        {
            if(!imguiLuaConsoleHasInput())
            {
                f();
            }
        };
    };

    game.addInput({{sf::Keyboard::Key::Escape}},
        notInConsole([this] { goToMenu(); }), // hardcoded
        ssvs::Input::Type::Always);

    addTidInput(Tid::Exit, ssvs::Input::Type::Always,
        notInConsole([this] { goToMenu(); }));

    addTidInput(Tid::ForceRestart, ssvs::Input::Type::Once,
        notInConsole(
            [this] { status.mustStateChange = StateChange::MustRestart; }));

    addTidInput(Tid::Restart, ssvs::Input::Type::Once,
        notInConsole(
            [this]
            {
                if(deathInputIgnore <= 0.f && status.hasDied)
                {
                    status.mustStateChange = StateChange::MustRestart;
                }
            }));

    addTidInput(Tid::Replay, ssvs::Input::Type::Once,
        notInConsole(
            [this]
            {
                if(deathInputIgnore <= 0.f && status.hasDied)
                {
                    status.mustStateChange = StateChange::MustReplay;
                }
            }));

    addTidInput(Tid::Screenshot, ssvs::Input::Type::Once,
        notInConsole([this] { mustTakeScreenshot = true; }));

    addTidInput(Tid::LuaConsole, ssvs::Input::Type::Once,
        [this](ssvu::FT /*unused*/)
        {
            if(Config::getDebug())
            {
                ilcShowConsoleNext = true;
            }
        });

    addTidInput(Tid::Pause, ssvs::Input::Type::Once,
        [this](ssvu::FT /*unused*/)
        {
            if(Config::getDebug())
            {
                debugPause = !debugPause;

                if(debugPause)
                {
                    if(audio != nullptr)
                    {
                        audio->pauseMusic();
                    }
                }
                else if(!status.hasDied)
                {
                    if(audio != nullptr)
                    {
                        audio->resumeMusic();
                    }
                }
            }
        });

    // ------------------------------------------------------------------------
    // Joystick binds

    Config::loadAllJoystickBinds();

    // ------------------------------------------------------------------------
    // key icons
    initKeyIcons();
}

HexagonGame::~HexagonGame()
{
    ssvu::lo("HexagonGame::~HexagonGame") << "Cleaning up game resources...\n";
}

void HexagonGame::refreshTrigger(
    const ssvs::Input::Trigger& trigger, const int bindID)
{
    game.refreshTrigger(trigger, bindID);
}

void HexagonGame::setLastReplay(const replay_file& mReplayFile)
{
    lastSeed = mReplayFile._seed;
    lastReplayData = mReplayFile._data;
    lastFirstPlay = mReplayFile._first_play;
    lastPlayedScore = mReplayFile._played_score;

    activeReplay.emplace(mReplayFile);
}

void HexagonGame::updateRichPresenceCallbacks()
{
    // Update Steam Rich Presence
    if(steamManager != nullptr && !steamHung)
    {
        if(!steamManager->run_callbacks())
        {
            steamAttempt += 1;
            if(steamAttempt > 20)
            {
                steamHung = true;
                ssvu::lo("Steam") << "Too many failed callbacks. Stopping "
                                     "Steam callbacks.\n";
            }
        }
    }

    // Update Discord Rich Presence
    if(discordManager != nullptr && !discordHung)
    {
        if(!discordManager->run_callbacks())
        {
            discordAttempt += 1;
            if(discordAttempt > 20)
            {
                discordHung = true;
                ssvu::lo("Discord") << "Too many failed callbacks. Stopping "
                                       "Discord callbacks.\n";
            }
        }
    }
}

[[nodiscard]] bool HexagonGame::shouldPlaySounds() const
{
    return window != nullptr && audio != nullptr && !Config::getNoSound();
}

[[nodiscard]] bool HexagonGame::shouldPlayMusic() const
{
    return window != nullptr && audio != nullptr && !Config::getNoMusic();
}

void HexagonGame::playSoundOverride(const std::string& mId)
{
    if(shouldPlaySounds())
    {
        audio->playSoundOverride(mId);
    }
}

void HexagonGame::playSoundAbort(const std::string& mId)
{
    if(shouldPlaySounds())
    {
        audio->playSoundAbort(mId);
    }
}

void HexagonGame::playPackSoundOverride(
    const std::string& mPackId, const std::string& mId)
{
    if(shouldPlaySounds())
    {
        audio->playPackSoundOverride(mPackId, mId);
    }
}

void HexagonGame::newGame(const std::string& mPackId, const std::string& mId,
    bool mFirstPlay, float mDifficultyMult, bool executeLastReplay)
{
    SSVOH_ASSERT(assets.isValidPackId(mPackId));
    SSVOH_ASSERT(assets.isValidLevelId(mId));

    initFlashEffect();

    packId = mPackId;
    levelId = mId;

    if(executeLastReplay && activeReplay.has_value())
    {
        firstPlay = activeReplay->replayFile._first_play;
    }
    else
    {
        firstPlay = mFirstPlay;
    }

    SSVOH_ASSERT(assets.isValidLevelId(mId));
    setLevelData(assets.getLevelData(mId), mFirstPlay);

    difficultyMult = mDifficultyMult;

    const double tempReplayScore = getReplayScore(status);
    status = HexagonGameStatus{};

    if(!executeLastReplay)
    {
        // TODO (P2): this can be used to restore normal speed
        // window.setTimer<ssvs::TimerStatic>(0.5f, 0.5f);

        rng = initializeRng();

        // Save data for immediate replay.
        lastSeed = rng.seed();
        lastReplayData = replay_data{};
        lastFirstPlay = mFirstPlay;

        // Clear any existing active replay.
        activeReplay.reset();
    }
    else
    {
        if(!activeReplay.has_value())
        {
            lastPlayedScore = tempReplayScore;

            activeReplay.emplace(replay_file{
                ._version{0},

                // TODO (P1): should this stay local?
                ._player_name{assets.getCurrentLocalProfile().getName()},

                ._seed{lastSeed},
                ._data{lastReplayData},
                ._pack_id{mPackId},
                ._level_id{mId},
                ._music_start_time{segment.time},
                ._first_play{lastFirstPlay},
                ._difficulty_mult{mDifficultyMult},
                ._played_score{lastPlayedScore},
            });
        }

        activeReplay->replayPlayer.reset();

        SSVOH_ASSERT(assets.isValidPackId(mPackId));

        activeReplay->replayPackName =
            Utils::toUppercase(assets.getPackData(mPackId).name);

        activeReplay->replayLevelName = Utils::toUppercase(levelData->name);

        // TODO (P2): this can be used to speed up the replay
        // window.setTimer<ssvs::TimerStatic>(0.5f, 0.1f);

        rng = random_number_generator{activeReplay->replayFile._seed};
        firstPlay = activeReplay->replayFile._first_play;
        
    }

    // Audio cleanup
    if(window != nullptr && audio != nullptr)
    {
        audio->stopSounds();
        stopLevelMusic();

        if(!Config::getNoMusic())
        {
            if(!executeLastReplay)
            {
                playLevelMusic();
            } 
            else 
            {
                playLevelMusicAtTime(activeReplay->replayFile._music_start_time);
            }
            audio->pauseMusic();
            refreshMusicPitch();
        }
        else
        {
            audio->stopMusic();
        }
    }

    debugPause = false;

    // Events cleanup
    messageText.setString("");
    pbText.setString("");

    // Event timeline cleanup
    eventTimeline.clear();
    eventTimelineRunner = {};

    // Music timeline cleanup
    musicTimeline.clear();
    musicTimelineRunner = {};

    // Message timeline cleanup
    messageTimeline.clear();
    messageTimelineRunner = {};

    // Manager cleanup
    walls.clear();
    cwManager.clear();
    player =
        CPlayer{ssvs::zeroVec2f, getSwapCooldown(), Config::getPlayerSize(),
            Config::getPlayerSpeed(), Config::getPlayerFocusSpeed()};

    // Timeline cleanup
    timeline.clear();
    timelineRunner = {};

    effectTimelineManager.clear();
    mustChangeSides = false;
    mustStart = false;

    // Particles cleanup
    pbTextGrowth = 0.f;
    mustSpawnPBParticles = false;
    nextPBParticleSpawn = 0.f;
    particles.clear();

    if(window != nullptr)
    {
        SSVOH_ASSERT(overlayCamera.has_value());
        SSVOH_ASSERT(backgroundCamera.has_value());

        // Reset zoom
        overlayCamera->setView(
            {{Config::getWidth() / 2.f, Config::getHeight() / 2.f},
                sf::Vector2f(Config::getWidth(), Config::getHeight())});
        backgroundCamera->setView({ssvs::zeroVec2f,
            {Config::getWidth() * Config::getZoomFactor(),
                Config::getHeight() * Config::getZoomFactor()}});
        backgroundCamera->setRotation(0);

        // Reset skew
        overlayCamera->setSkew(sf::Vector2f{1.f, 1.f});
        backgroundCamera->setSkew(sf::Vector2f{1.f, 1.f});
    }

    // Lua context and game status cleanup
    inputImplCCW = inputImplCW = false;

    lua = Lua::LuaContext{};
    calledDeprecatedFunctions.clear();
    initLua();
    runLuaFile(levelData->luaScriptPath);

    if(!firstPlay)
    {
        runLuaFunctionIfExists<void>("onUnload");
        playSoundOverride("restart.ogg");
    }
    else
    {
        playSoundOverride("select.ogg");
    }

    runLuaFunctionIfExists<void>("onInit");

    restartId = mId;
    restartFirstTime = false;
    setSides(levelStatus.sides);

    // Set initial values for some status fields from Lua
    status.pulseDelay += levelStatus.pulseInitialDelay;
    status.beatPulseDelay += levelStatus.beatPulseInitialDelay;
    timeUntilRichPresenceUpdate = -1.f; // immediate update

    // Store the keys/buttons to be pressed to replay and restart after you
    // die.
    using Tid = Config::Tid;
    status.restartInput = Config::getKeyboardBindNames(Tid::Restart);
    status.replayInput = Config::getKeyboardBindNames(Tid::Replay);

    // Format strings to only show the first key to avoid extremely long
    // messages
    int commaPos = status.restartInput.find(',');
    if(commaPos > 0)
    {
        status.restartInput.erase(commaPos);
    }
    commaPos = status.replayInput.find(',');
    if(commaPos > 0)
    {
        status.replayInput.erase(commaPos);
    }

    // Add joystick buttons if any and finalize message
    std::string joystickButton =
        Config::getJoystickBindNames(Joystick::Jid::Restart);
    if(!status.restartInput.empty())
    {
        if(!joystickButton.empty())
        {
            status.restartInput += " OR JOYSTICK " + joystickButton;
        }
        status.restartInput = "PRESS " + status.restartInput + " TO RESTART\n";
    }
    else if(!joystickButton.empty())
    {
        status.restartInput =
            "PRESS JOYSTICK " + joystickButton + " TO RESTART\n";
    }
    else
    {
        status.restartInput = "NO RESTART BUTTON SET\n";
    }
    joystickButton = Config::getJoystickBindNames(Joystick::Jid::Replay);
    if(!status.replayInput.empty())
    {
        if(!joystickButton.empty())
        {
            status.replayInput += " OR JOYSTICK " + joystickButton;
        }
        status.replayInput = "PRESS " + status.replayInput + " TO REPLAY\n";
    }
    else if(!joystickButton.empty())
    {
        status.replayInput =
            "PRESS JOYSTICK " + joystickButton + " TO REPLAY\n";
    }
    else
    {
        status.replayInput = "NO REPLAY BUTTON SET\n";
    }
}

void HexagonGame::death_shakeCamera()
{
    if(window == nullptr)
    {
        return;
    }

    SSVOH_ASSERT(overlayCamera.has_value());
    SSVOH_ASSERT(backgroundCamera.has_value());

    overlayCamera->setView(
        {{Config::getWidth() / 2.f, Config::getHeight() / 2.f},
            sf::Vector2f(Config::getWidth(), Config::getHeight())});

    backgroundCamera->setCenter(ssvs::zeroVec2f);

    Utils::shakeCamera(effectTimelineManager, *overlayCamera);
    Utils::shakeCamera(effectTimelineManager, *backgroundCamera);
}

void HexagonGame::death_updateRichPresence()
{
    if(window == nullptr)
    {
        return;
    }

    if(inReplay())
    {
        // Do not update rich presence if watching a replay.
        return;
    }

// TODO (P2): ??? meant for rich presence?
#if 0
    // Gather player's Personal Best
    std::string pbStr = "(";
    if(isPersonalBest)
    {
        pbStr += "New PB!)";
    }
    else
    {
        pbStr += "PB: " +
                 timeFormat(assets.getLocalScore(levelData->getValidatorWithoutPackId(difficultyMult))) +
                 "s)";
    }
#endif

    std::string nameStr = levelData->name;
    nameFormat(nameStr);

    const std::string diffStr = diffFormat(difficultyMult);
    const std::string timeStr = timeFormat(status.getTimeSeconds());

    if(discordManager != nullptr)
    {
        discordManager->set_rich_presence_in_game(
            nameStr + " [x" + diffStr + "]", "Survived " + timeStr + "s", true);
    }
}

[[nodiscard]] HexagonGame::SaveScoreIfNeededResult
HexagonGame::death_saveScoreIfNeeded()
{
    if(window == nullptr)
    {
        return SaveScoreIfNeededResult::NoWindow;
    }

    if(!shouldSaveScore())
    {
        return SaveScoreIfNeededResult::ShouldNotSave;
    }

    const std::string validatorWithoutPackid =
        levelData->getValidatorWithoutPackId(difficultyMult);

    const double score = status.getTimeSeconds();

    const bool isPersonalBest =
        score > assets.getLocalScore(validatorWithoutPackid);

    if(!isPersonalBest)
    {
        return SaveScoreIfNeededResult::NotPersonalBest;
    }

    assets.setLocalScore(validatorWithoutPackid, score);
    return SaveScoreIfNeededResult::PersonalBest;
}

void HexagonGame::death_saveScoreIfNeededAndShowPBEffects()
{
    const SaveScoreIfNeededResult r = death_saveScoreIfNeeded();

    if(r == SaveScoreIfNeededResult::NoWindow)
    {
        return;
    }

    if(r == SaveScoreIfNeededResult::ShouldNotSave ||
        r == SaveScoreIfNeededResult::NotPersonalBest)
    {
        playSoundAbort("gameOver.ogg");
        return;
    }

    SSVOH_ASSERT(r == SaveScoreIfNeededResult::PersonalBest);

    pbText.setString("NEW PERSONAL BEST!");
    mustSpawnPBParticles = true;

    playSoundAbort("personalBest.ogg");
}

void HexagonGame::death(bool mForce)
{
    if(status.hasDied)
    {
        return;
    }

    deathInputIgnore = 10.f;

    playSoundAbort(levelStatus.deathSound);

    runLuaFunctionIfExists<void>("onPreDeath");

    if(!mForce && (Config::getInvincible() || levelStatus.tutorialMode))
    {
        return;
    }

    runLuaFunctionIfExists<void>("onDeath");
    death_shakeCamera();
    death_updateRichPresence();
    stopLevelMusic();

    status.flashEffect = 255;
    status.hasDied = true;

    const replay_file rf = death_createReplayFile();

    // TODO (P2): for testing
    if(onDeathReplayCreated)
    {
        onDeathReplayCreated(rf);
    }

    ssvu::lo("Replay") << "Attempting to send and save replay...\n";
    death_sendAndSaveReplay(rf);

    death_saveScoreIfNeededAndShowPBEffects(); // Saves local best

    if(window != nullptr && Config::getAutoRestart())
    {
        status.mustStateChange = StateChange::MustRestart;
    }
}

[[nodiscard]] replay_file HexagonGame::death_createReplayFile()
{
    // TODO (P2): for testing
    const std::string rfName = assets.anyLocalProfileActive()
                                   ? assets.getCurrentLocalProfile().getName()
                                   : "no_profile";

    return replay_file{
        ._version{0},
        ._player_name{rfName},
        ._seed{lastSeed},
        ._data{lastReplayData},
        ._pack_id{packId},
        ._level_id{levelId},
        ._first_play{firstPlay},
        ._difficulty_mult{difficultyMult},
        ._played_score{getReplayScore(status)},
    };
}

void HexagonGame::death_sendAndSaveReplay(const replay_file& rf)
{
    const std::optional<compressed_replay_file> crfOpt =
        compress_replay_file(rf);

    if(!crfOpt.has_value())
    {
        ssvu::lo("Replay") << "Failed to compress replay, will not save to "
                              "file or send to server\n";

        return;
    }

    const compressed_replay_file& crf = crfOpt.value();

    // ------------------------------------------------------------------------
    // Send compressed replay to server.
    if(const std::string levelValidator =
            Utils::getLevelValidator(rf._level_id, rf._difficulty_mult);
        !death_sendReplay(levelValidator, crf))
    {
        ssvu::lo("Replay") << "Failure sending replay\n";
    }

    // ------------------------------------------------------------------------
    // Save compressed replay locally.
    if(const std::string filename = Utils::concat(rf.create_filename(), ".gz");
        !death_saveReplay(filename, crf))
    {
        ssvu::lo("Replay") << "Failure saving replay\n";
    }
}

[[nodiscard]] bool HexagonGame::death_sendReplay(
    const std::string& levelValidator, const compressed_replay_file& crf)
{
    if(hexagonClient == nullptr ||
        hexagonClient->getState() != HexagonClient::State::LoggedIn_Ready ||
        !Config::getOfficial())
    {
        return false;
    }

    ssvu::lo("Replay") << "Sending compressed replay to server...\n";

    if(!hexagonClient->trySendCompressedReplay(levelValidator, crf))
    {
        ssvu::lo("Replay") << "Could not send compressed replay to server\n";
        return false;
    }

    steamManager->unlock_achievement("a24_onlinescore");
    return true;
}

[[nodiscard]] bool HexagonGame::death_saveReplay(
    const std::string& filename, const compressed_replay_file& crf)
{
    std::filesystem::create_directory("Replays/");

    std::filesystem::path p;
    p /= "Replays/";
    p /= filename;

    if(!crf.serialize_to_file(p))
    {
        ssvu::lo("Replay") << "Failed to save new compressed replay file '" << p
                           << "'\n";

        return false;
    }

    ssvu::lo("Replay") << "Successfully saved new compressed replay file '" << p
                       << "'\n";

    return true;
}

[[nodiscard]] std::optional<HexagonGame::GameExecutionResult>
HexagonGame::executeGameUntilDeath(
    const int maxProcessingSeconds, const float timescale)
{
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;

    const TimePoint tpBegin = Clock::now();

    const auto exceededProcessingTime = [&]
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   Clock::now() - tpBegin)
                   .count() > maxProcessingSeconds;
    };

    while(!status.hasDied)
    {
        update(Config::TIME_STEP, timescale);
        postUpdate();

        if(exceededProcessingTime())
        {
            return std::nullopt;
        }
    }

    return GameExecutionResult{
        .playedTimeSeconds = status.getPlayedAccumulatedFrametimeInSeconds(), //
        .pausedTimeSeconds = status.getPausedAccumulatedFrametimeInSeconds(), //
        .totalTimeSeconds = status.getTotalAccumulatedFrametimeInSeconds(),   //
        .customScore = status.getCustomScore()                                //
    };
}

[[nodiscard]] std::optional<HexagonGame::GameExecutionResult>
HexagonGame::runReplayUntilDeathAndGetScore(const replay_file& mReplayFile,
    const int maxProcessingSeconds, const float timescale)
{
    SSVOH_ASSERT(assets.isValidPackId(mReplayFile._pack_id));
    SSVOH_ASSERT(assets.isValidLevelId(mReplayFile._level_id));

    setLastReplay(mReplayFile);

    newGame(mReplayFile._pack_id, mReplayFile._level_id,
        mReplayFile._first_play, mReplayFile._difficulty_mult,
        /* mExecuteLastReplay */ true);

    return executeGameUntilDeath(maxProcessingSeconds, timescale);
}

void HexagonGame::incrementDifficulty()
{
    playSoundOverride("levelUp.ogg");

    const float signMult = (levelStatus.rotationSpeed > 0.f) ? 1.f : -1.f;

    levelStatus.rotationSpeed += levelStatus.rotationSpeedInc * signMult;

    const auto& rotationSpeedMax(levelStatus.rotationSpeedMax);
    if(std::abs(levelStatus.rotationSpeed) > rotationSpeedMax)
    {
        levelStatus.rotationSpeed = rotationSpeedMax * signMult;
    }

    levelStatus.rotationSpeed *= -1.f;
    status.fastSpin = levelStatus.fastSpin;
}

void HexagonGame::sideChange(unsigned int mSideNumber)
{
    levelStatus.speedMult += levelStatus.speedInc;
    levelStatus.delayMult += levelStatus.delayInc;

    if(levelStatus.rndSideChangesEnabled)
    {
        setSides(mSideNumber);
    }

    mustChangeSides = false;

    playSoundOverride(levelStatus.levelUpSound);
    runLuaFunctionIfExists<void>("onIncrement");
}

[[nodiscard]] bool HexagonGame::shouldSaveScore()
{
    if(!assets.anyLocalProfileActive())
    {
        ssvu::lo("hg::HexagonGame::shouldSaveScore()")
            << "No local profile active, rejecting\n";

        return false;
    }

    if(!Config::isEligibleForScore())
    {
        ssvu::lo("hg::HexagonGame::shouldSaveScore()")
            << "Not saving score - not eligible - "
            << Config::getUneligibilityReason() << '\n';

        return false;
    }

    if(status.scoreInvalid)
    {
        ssvu::lo("hg::HexagonGame::shouldSaveScore()")
            << "Not saving score - score invalidated\n";

        return false;
    }

    if(levelStatus.tutorialMode)
    {
        ssvu::lo("hg::HexagonGame::shouldSaveScore()")
            << "Not saving score - in tutorial mode\n";

        return false;
    }

    if(levelData->unscored)
    {
        ssvu::lo("hg::HexagonGame::shouldSaveScore()")
            << "Not saving score - unscored level\n";

        return false;
    }

    if(inReplay())
    {
        ssvu::lo("hg::HexagonGame::shouldSaveScore()")
            << "Not saving score - currently in replay\n";

        return false;
    }

    return true;
}

#if 0
// TODO (P0): use?
const double score =
    levelStatus.scoreOverridden
        ? lua.readVariable<double>(levelStatus.scoreOverride)
        : status.getTimeSeconds();
#endif

void HexagonGame::goToMenu(bool mSendScores, bool mError)
{
    if(window == nullptr)
    {
        ssvu::lo("hg::HexagonGame::goToMenu")
            << "Attempted to go back to menu without a game window\n";

        return;
    }

    if(audio != nullptr)
    {
        audio->stopSounds();
    }

    ilcLuaTracked.clear();
    ilcLuaTrackedNames.clear();
    ilcLuaTrackedResults.clear();

    if(!mError)
    {
        playSoundOverride("beep.ogg");
    }

    calledDeprecatedFunctions.clear();

    if(mSendScores && !mError && shouldSaveScore())
    {
        (void)death_saveScoreIfNeeded(); // Saves local best

        const replay_file rf = death_createReplayFile();

        ssvu::lo("Replay") << "Attempting to send and save replay...\n";
        death_sendAndSaveReplay(rf);
    }

    // Stop infinite feedback from occurring if the error is happening on
    // onUnload.
    if(!mError)
    {
        runLuaFunctionIfExists<void>("onUnload");
    }

    if(fnGoToMenu)
    {
        fnGoToMenu(mError);
    }
}

void HexagonGame::raiseWarning(
    const std::string& mFunctionName, const std::string& mAdditionalInfo)
{
    // Only raise the warning once to avoid redundancy
    if(calledDeprecatedFunctions.contains(mFunctionName))
    {
        return;
    }

    calledDeprecatedFunctions.emplace(mFunctionName);

    // Raise warning to the console
    const std::string errorMsg = Utils::concat("[Lua] WARNING: The function \"",
        mFunctionName, "\" (used in level \"", levelData->name,
        "\") is deprecated. ", mAdditionalInfo);

    std::cout << errorMsg << std::endl;
    ilcCmdLog.emplace_back(Utils::concat("[warning]: ", errorMsg, '\n'));
}

void HexagonGame::addMessage(
    std::string mMessage, double mDuration, bool mSoundToggle)
{
    if(!Config::getShowMessages())
    {
        return;
    }

    Utils::uppercasify(mMessage);

    messageTimeline.append_do(
        [this, mSoundToggle, mMessage]
        {
            if(mSoundToggle)
            {
                playSoundOverride(levelStatus.beepSound);
            }

            messageText.setString(mMessage);
        });

    messageTimeline.append_wait_for_sixths(mDuration);
    messageTimeline.append_do([this] { messageText.setString(""); });
}

void HexagonGame::clearMessages()
{
    messageTimeline.clear();
}

void HexagonGame::setLevelData(
    const LevelData& mLevelData, bool mMusicFirstPlay)
{
    levelData = &mLevelData;

    levelStatus =
        LevelStatus{Config::getMusicSpeedDMSync(), Config::getSpawnDistance()};

    styleData = assets.getStyleData(levelData->packId, levelData->styleId);
    styleData.computeColors();

    musicData = assets.getMusicData(levelData->packId, levelData->musicId);
    musicData.firstPlay = mMusicFirstPlay;
}

[[nodiscard]] const std::string& HexagonGame::getPackId() const noexcept
{
    return levelData->packId;
}

[[nodiscard]] const PackData& HexagonGame::getPackData() const noexcept
{
    return assets.getPackData(getPackId());
}

[[nodiscard]] const std::string&
HexagonGame::getPackDisambiguator() const noexcept
{
    return getPackData().disambiguator;
}

[[nodiscard]] const std::string& HexagonGame::getPackAuthor() const noexcept
{
    return getPackData().author;
}

[[nodiscard]] const std::string& HexagonGame::getPackName() const noexcept
{
    return getPackData().name;
}

[[nodiscard]] int HexagonGame::getPackVersion() const noexcept
{
    return getPackData().version;
}

void HexagonGame::playLevelMusic()
{
    if(shouldPlayMusic())
    {
        segment = musicData.playRandomSegment(getPackId(), *audio);
        musicTimelineRunner.clearLastTp();
        // TODO (P1): problems with addHash in headless mode:
        status.beatPulseDelay += segment.beatPulseDelayOffset;
    }
}

void HexagonGame::playLevelMusicAtTime(float mSeconds)
{
    if(shouldPlayMusic())
    {
        musicData.playSeconds(getPackId(), *audio, mSeconds);
        musicTimelineRunner.clearLastTp();
    }
}

void HexagonGame::stopLevelMusic()
{
    if(shouldPlayMusic())
    {
        audio->stopMusic();
    }
}

void HexagonGame::invalidateScore(const std::string& mReason)
{
    if(status.scoreInvalid)
    {
        return;
    }

    status.scoreInvalid = true;
    status.invalidReason = mReason;

    ssvu::lo("HexagonGame::invalidateScore")
        << "Invalidating official game (" << mReason << ")\n";
}

auto HexagonGame::getColorMain() const -> sf::Color
{
    if(Config::getBlackAndWhite())
    {
        return sf::Color(255, 255, 255, styleData.getMainColor().a);
    }

    return styleData.getMainColor();
}

auto HexagonGame::getColorPlayer() const -> sf::Color
{
    if(Config::getBlackAndWhite())
    {
        return sf::Color(255, 255, 255, styleData.getPlayerColor().a);
    }

    return styleData.getPlayerColor();
}

auto HexagonGame::getColorText() const -> sf::Color
{
    if(Config::getBlackAndWhite())
    {
        return sf::Color(255, 255, 255, styleData.getTextColor().a);
    }

    return styleData.getTextColor();
}

auto HexagonGame::getColorCap() const -> sf::Color
{
    if (Config::getBlackAndWhite()) 
    {
        return sf::Color::Black;
    }

    return styleData.getCapColorResult();
}

auto HexagonGame::getColorWall() const -> sf::Color
{
    if (Config::getBlackAndWhite()) 
    {
        return sf::Color(255, 255, 255, styleData.getWallColor().a);
    }

    return styleData.getWallColor();
}

[[nodiscard]] float HexagonGame::getMusicDMSyncFactor() const
{
    return std::pow(difficultyMult, 0.12f);
}

[[nodiscard]] float HexagonGame::getOptionalMusicDMSyncFactor() const
{
    return levelStatus.syncMusicToDM ? getMusicDMSyncFactor() : 1.f;
}

void HexagonGame::refreshMusicPitch()
{
    if(audio != nullptr)
    {
        audio->setCurrentMusicPitch(getOptionalMusicDMSyncFactor() *
                                    Config::getMusicSpeedMult() *
                                    levelStatus.musicPitch);
    }
}

void HexagonGame::setSides(unsigned int mSides)
{
    playSoundOverride(levelStatus.beepSound);

    if(mSides < 3)
    {
        mSides = 3;
    }

    levelStatus.sides = mSides;
}

[[nodiscard]] ssvs::GameState& HexagonGame::getGame() noexcept
{
    return game;
}

[[nodiscard]] float HexagonGame::getRadius() const noexcept
{
    return status.radius;
}

[[nodiscard]] const sf::Color& HexagonGame::getColor(int mIdx) const noexcept
{
    return styleData.getColor(mIdx);
}

[[nodiscard]] float HexagonGame::getSpeedMultDM() const noexcept
{
    const auto res = levelStatus.speedMult * (std::pow(difficultyMult, 0.65f));

    if(!levelStatus.hasSpeedMaxLimit())
    {
        return res;
    }

    return (res < levelStatus.speedMax) ? res : levelStatus.speedMax;
}

[[nodiscard]] float HexagonGame::getDelayMultDM() const noexcept
{
    const auto res = levelStatus.delayMult / (std::pow(difficultyMult, 0.10f));

    if(!levelStatus.hasDelayMaxLimit())
    {
        return res;
    }

    return (res < levelStatus.delayMax) ? res : levelStatus.delayMax;
}

[[nodiscard]] float HexagonGame::getRotationSpeed() const noexcept
{
    return levelStatus.rotationSpeed;
}

[[nodiscard]] unsigned int HexagonGame::getSides() const noexcept
{
    return levelStatus.sides;
}

[[nodiscard]] float HexagonGame::getWallSkewLeft() const noexcept
{
    return levelStatus.wallSkewLeft;
}

[[nodiscard]] float HexagonGame::getWallSkewRight() const noexcept
{
    return levelStatus.wallSkewRight;
}

[[nodiscard]] float HexagonGame::getWallAngleLeft() const noexcept
{
    return levelStatus.wallAngleLeft;
}

[[nodiscard]] float HexagonGame::getWallAngleRight() const noexcept
{
    return levelStatus.wallAngleRight;
}

[[nodiscard]] float HexagonGame::get3DEffectMult() const noexcept
{
    return levelStatus._3dEffectMultiplier;
}

[[nodiscard]] HexagonGameStatus& HexagonGame::getStatus() noexcept
{
    return status;
}

[[nodiscard]] const HexagonGameStatus& HexagonGame::getStatus() const noexcept
{
    return status;
}

[[nodiscard]] LevelStatus& HexagonGame::getLevelStatus()
{
    return levelStatus;
}

[[nodiscard]] HGAssets& HexagonGame::getAssets()
{
    return assets;
}

[[nodiscard]] bool HexagonGame::getInputFocused() const
{
    return inputFocused;
}

[[nodiscard]] float HexagonGame::getPlayerSpeedMult() const
{
    return levelStatus.playerSpeedMult;
}

[[nodiscard]] bool HexagonGame::getInputSwap() const
{
    return inputSwap;
}

[[nodiscard]] int HexagonGame::getInputMovement() const
{
    return inputMovement;
}

[[nodiscard]] bool HexagonGame::inReplay() const noexcept
{
    return activeReplay.has_value();
}

[[nodiscard]] bool HexagonGame::mustReplayInput() const noexcept
{
    return inReplay() && !activeReplay->replayPlayer.done();
}

[[nodiscard]] bool HexagonGame::mustShowReplayUI() const noexcept
{
    return inReplay();
}

[[nodiscard]] float HexagonGame::getSwapCooldown() const noexcept
{
    return std::max(36.f * levelStatus.swapCooldownMult, 8.f);
}

void HexagonGame::performPlayerSwap(const bool mPlaySound)
{
    player.playerSwap();
    runLuaFunctionIfExists<void>("onCursorSwap");

    if(mPlaySound)
    {
        playSoundOverride(getLevelStatus().swapSound);
    }
}

void HexagonGame::performPlayerKill()
{
    const bool fatal =
        !Config::getInvincible() && !getLevelStatus().tutorialMode;

    player.kill(fatal);
    death();
}

} // namespace hg
