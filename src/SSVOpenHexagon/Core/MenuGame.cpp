// Copyright (c) 2013-2020 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: https://opensource.org/licenses/AFL-3.0

#include "SSVOpenHexagon/Core/MenuGame.hpp"

#include "SSVOpenHexagon/Components/CCustomWallManager.hpp"

#include "SSVOpenHexagon/Core/BindControl.hpp"
#include "SSVOpenHexagon/Core/Discord.hpp"
#include "SSVOpenHexagon/Core/HexagonClient.hpp"
#include "SSVOpenHexagon/Core/HGStatus.hpp"
#include "SSVOpenHexagon/Core/Joystick.hpp"
#include "SSVOpenHexagon/Core/LeaderboardCache.hpp"
#include "SSVOpenHexagon/Core/LuaScripting.hpp"
#include "SSVOpenHexagon/Core/RandomNumberGenerator.hpp"
#include "SSVOpenHexagon/Core/Steam.hpp"

#include "SSVOpenHexagon/Data/MusicData.hpp"
#include "SSVOpenHexagon/Data/LevelData.hpp"
#include "SSVOpenHexagon/Data/LoadInfo.hpp"
#include "SSVOpenHexagon/Data/PackInfo.hpp"
#include "SSVOpenHexagon/Data/PackData.hpp"
#include "SSVOpenHexagon/Data/ProfileData.hpp"

#include "SSVOpenHexagon/Global/Assert.hpp"
#include "SSVOpenHexagon/Global/Assets.hpp"
#include "SSVOpenHexagon/Global/Audio.hpp"
#include "SSVOpenHexagon/Global/Config.hpp"
#include "SSVOpenHexagon/Global/Version.hpp"

#include "SSVOpenHexagon/Online/Database.hpp"

#include "SSVOpenHexagon/SSVUtilsJson/SSVUtilsJson.hpp"

#include "SSVOpenHexagon/Utils/Casts.hpp"
#include "SSVOpenHexagon/Utils/Concat.hpp"
#include "SSVOpenHexagon/Utils/FontHeight.hpp"
#include "SSVOpenHexagon/Utils/Geometry.hpp"
#include "SSVOpenHexagon/Utils/LevelValidator.hpp"
#include "SSVOpenHexagon/Utils/LuaWrapper.hpp"
#include "SSVOpenHexagon/Utils/Match.hpp"
#include "SSVOpenHexagon/Utils/ScopeGuard.hpp"
#include "SSVOpenHexagon/Utils/String.hpp"
#include "SSVOpenHexagon/Utils/UniquePtr.hpp"
#include "SSVOpenHexagon/Utils/Utils.hpp"

#include <SSVStart/Input/Input.hpp>
#include <SSVStart/Utils/SFML.hpp>
#include <SSVStart/Utils/Input.hpp>
#include <SSVStart/Utils/Vector2.hpp>
#include <SSVStart/GameSystem/GameSystem.hpp>

#include <SSVMenuSystem/SSVMenuSystem.hpp>

#include <SSVUtils/Core/Common/Frametime.hpp>

#include <utility>
#include <array>
#include <tuple>
#include <string_view>

namespace hg {

[[nodiscard]] static bool anyItemEnabled(const ssvms::Menu& menu)
{
    for(const auto& i : menu.getItems())
    {
        if(i->isEnabled())
        {
            return true;
        }
    }

    return false;
}

[[nodiscard]] static bool scrollToEnabledMenuItem(ssvms::Menu* menu)
{
    if(menu == nullptr)
    {
        return false;
    }

    // Scroll to a menu item that is enabled
    menu->update();

    if(!anyItemEnabled(*menu))
    {
        return false;
    }

    while(!menu->getItem().isEnabled())
    {
        menu->next();
    }

    return true;
}

void MenuGame::MenuFont::updateHeight()
{
    height = hg::Utils::getFontHeight(font);
}

[[nodiscard]] bool MenuGame::isEnteringText() const noexcept
{
    return state <= States::ETLPNewBoot || state == States::ETLPNew;
}


[[nodiscard]] ssvms::Menu* MenuGame::getCurrentMenu() noexcept
{
    switch(state)
    {
        case States::SMain: return &mainMenu;
        case States::MOpts: return &optionsMenu;
        case States::MOnline: return &onlineMenu;
        case States::SLPSelectBoot:
        case States::SLPSelect: return &profileSelectionMenu;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
        default: return nullptr;
#pragma GCC diagnostic pop
    }
}

[[nodiscard]] bool MenuGame::isInMenu() noexcept
{
    return getCurrentMenu() != nullptr;
}

void MenuGame::initOnlineIcons()
{
    assets.getTexture("onlineIcon.png").setSmooth(true);
    assets.getTexture("onlineIconFail.png").setSmooth(true);
}

//*****************************************************
//
// INITIALIZATION
//
//*****************************************************

inline constexpr float maxOffset{100.f};

MenuGame::MenuGame(Steam::steam_manager& mSteamManager,
    Discord::discord_manager& mDiscordManager, HGAssets& mAssets, Audio& mAudio,
    ssvs::GameWindow& mGameWindow, HexagonClient& mHexagonClient)
    : steamManager(mSteamManager),
      discordManager(mDiscordManager),
      assets(mAssets),
      openSquare(mAssets.getFont("OpenSquare-Regular.ttf")),
      openSquareBold(mAssets.getFont("OpenSquare-Bold.ttf")),
      audio(mAudio),
      window(mGameWindow),
      hexagonClient{mHexagonClient},
      dialogBox(openSquare, mGameWindow),
      leaderboardCache{Utils::makeUnique<LeaderboardCache>()},
      lua{},
      execScriptPackPathContext{},
      currentPack{nullptr},
      titleBar{assets.getTexture("titleBar.png")},
      creditsBar1{assets.getTexture("creditsBar1.png")},
      creditsBar2{assets.getTexture("creditsBar2.png")},
      epilepsyWarning{assets.getTexture("epilepsyWarning.png")},
      sOnline{assets.getTexture("onlineIconFail.png")},
      rsOnlineStatus{sf::Vector2f{128.f, 32.f}},
      txtOnlineStatus{"", openSquare, 24},
      enteredChars{},
      backgroundCamera{window,
          {ssvs::zeroVec2f, {Config::getSizeX() * Config::getZoomFactor(),
                                Config::getSizeY() * Config::getZoomFactor()}}},
      overlayCamera{
          window, {{Config::getWidth() / 2.f,
                       Config::getHeight() * Config::getZoomFactor() / 2.f},
                      {Config::getWidth() * Config::getZoomFactor(),
                          Config::getHeight() * Config::getZoomFactor()}}},
      mustRefresh{false},
      wasFocusHeld{false},
      focusHeld{false},
      wheelProgress{0.f},
      touchDelay{0.f},
      state{States::LoadingScreen},
      packChangeDirection{0},
      levelStatus{Config::getMusicSpeedDMSync(), Config::getSpawnDistance()},
      ignoreInputs{0},
      w{0.f},
      h{0.f},
      scrollbarOffset{0},
      fourByThree{false},
      welcomeMenu{},
      mainMenu{},
      optionsMenu{},
      onlineMenu{},
      profileSelectionMenu{},
      levelData{},
      styleData{},
      txtVersion{.font{"", openSquare, 40}},
      txtProf{.font{"", openSquare, 18}},
      // For the loading screen
      txtLoadBig{.font{"", openSquare, 72}},
      txtLoadSmall{.font{"", openSquareBold}},
      txtRandomTip{.font{"", openSquare, 32}},
      // For the Main Menu
      txtMenuBig{.font{"", openSquare, 96}},
      txtMenuSmall{.font{"", openSquare}},
      txtProfile{.font{"", openSquare, 32}},
      txtInstructionsBig{.font{"", openSquare, 46}},
      txtInstructionsMedium{.font{"", openSquare}},
      txtInstructionsSmall{.font{"", openSquare, 20}},
      // Manual Input
      txtEnteringText{.font{"", openSquare, 54}},
      // For the Level Selection Screen
      txtSelectionBig{.font{"", openSquareBold, 28}},
      txtSelectionMedium{.font{"", openSquareBold, 20}},
      txtSelectionSmall{.font{"", openSquare, 12}},
      txtSelectionScore{.font{"", openSquare, 28}},
      txtSelectionRanked{.font{"", openSquare, 10}},
      menuTextColor{},
      menuQuadColor{},
      menuSelectionColor{},
      dialogBoxTextColor{},
      menuBackgroundTris{},
      menuQuads{},
      loadInfo(mAssets.getLoadResults()),
      randomTip{},
      hexagonRotation{0.f},
      menuHalfHeight{0.f},
      enteringTextOffset{0.f},
      isLevelFavorite{false},
      favoriteLevelDataIds{},
      lvlSlct{},
      favSlct{.levelDataIds = &favoriteLevelDataIds, .isFavorites = true},
      lvlDrawer{&lvlSlct}
{
    // Set cursor visible by default, will be disabled when using keyboard and
    // re-enabled when moving the mouse.
    setMouseCursorVisible(true);

    if(Config::getFirstTimePlaying())
    {
        showFirstTimeTips = true;
        Config::setFirstTimePlaying(false);
    }

    initAssets();
    initOnlineIcons();
    refreshCamera();

    game.onUpdate += [this](ssvu::FT mFT) { update(mFT); };

    game.onDraw += [this] { draw(); };

    game.onEvent(sf::Event::EventType::Resized) +=
        [this](const sf::Event& event)
    { changeResolutionTo(event.size.width, event.size.height); };

    game.onEvent(sf::Event::EventType::TextEntered) +=
        [this](const sf::Event& mEvent)
    {
        if(mEvent.text.unicode < 128)
        {
            enteredChars.emplace_back(ssvu::toNum<char>(mEvent.text.unicode));
        }
    };

    game.onEvent(sf::Event::EventType::KeyPressed) += [this](const sf::Event&)
    {
        if(window.hasFocus())
        {
            setMouseCursorVisible(false);
        }
    };

    game.onEvent(sf::Event::EventType::MouseMoved) += [this](const sf::Event& e)
    {
        const sf::Vector2i mouseMoveVec = {e.mouseMove.x, e.mouseMove.y};
        const sf::Vector2i mouseMoveDelta =
            lastMouseMovedPosition - mouseMoveVec;

        lastMouseMovedPosition = mouseMoveVec;

        const bool actuallyMoved =
            (mouseMoveDelta.x != 0) || (mouseMoveDelta.y != 0);

        if(window.hasFocus() && actuallyMoved)
        {
            setMouseCursorVisible(true);
        }
    };

    game.onEvent(sf::Event::EventType::MouseWheelMoved) +=
        [this](const sf::Event& mEvent)
    {
        if(window.hasFocus())
        {
            setMouseCursorVisible(true);
        }

        // Disable scroll while assigning a bind
        if(state == States::MOpts)
        {
            const auto* const bc{
                dynamic_cast<BindControlBase*>(&getCurrentMenu()->getItem())};
            if(bc != nullptr && bc->isWaitingForBind())
            {
                return;
            }
        }

        if(state == States::LevelSelection)
        {
            if(focusHeld)
            {
                changePackQuick(mEvent.mouseWheel.delta > 0 ? -1 : 1);
            }
            else if(lvlDrawer != nullptr)
            {
                lvlDrawer->YScrollTo += mEvent.mouseWheel.delta * 48.f;

                if(lvlDrawer->YScrollTo > 0)
                {
                    lvlDrawer->YScrollTo = 0;
                }
                else if(lvlDrawer->YScrollTo < -4000)
                {
                    // Why...
                    steamManager.unlock_achievement("a35_eagerformore");
                    playSoundOverride("error.ogg");
                    lvlDrawer->YScrollTo = 0;
                }
            }

            return;
        }

        wheelProgress += mEvent.mouseWheel.delta;
        if(wheelProgress > 1.f)
        {
            wheelProgress = 0.f;
            upAction();
        }
        else if(wheelProgress < -1.f)
        {
            wheelProgress = 0.f;
            downAction();
        }
    };

    // To close the load results with any key
    setIgnoreAllInputs(1);

    const auto checkCloseBootScreens = [this]
    {
        if((--ignoreInputs) == 0)
        {
            if(state == States::LoadingScreen)
            {
                changeStateTo(States::EpilepsyWarning);
                setIgnoreAllInputs(1);
                scrollbarOffset = 0;
            }
            else
            {
                mainMenu.getItems()[0]->getOffset() = maxOffset;
                mainMenu.getCategory().getOffset() =
                    fourByThree ? 280.f : 400.f;

                playLocally();
                setIgnoreAllInputs(0);
            }

            playSoundOverride("select.ogg");
        }
    };

    const auto checkCloseDialogBox = [this]
    {
        const auto closeBox = [this]
        {
            playSoundOverride("select.ogg");
            dialogBox.clearDialogBox();
            setIgnoreAllInputs(0);
        };

        const auto transitionInputSequence =
            [this, closeBox](const DialogInputState newState)
        {
            dialogInputState = newState;
            HG_SCOPE_GUARD({ closeBox(); });
            return dialogBox.getInput();
        };

        const auto endInputSequence = [this, closeBox]
        {
            dialogInputState = DialogInputState::Nothing;
            HG_SCOPE_GUARD({ closeBox(); });
            return dialogBox.getInput();
        };

        if(ignoreInputs != 0)
        {
            return;
        }

        if(dialogInputState == DialogInputState::Nothing)
        {
            closeBox();
            return;
        }

        if(dialogInputState == DialogInputState::Registration_EnteringUsername)
        {
            registrationUsername = transitionInputSequence(
                DialogInputState::Registration_EnteringPassword);

            showInputDialogBoxNice("REGISTRATION", "PASSWORD");
            dialogBox.setInputBoxPassword(true);
            setIgnoreAllInputs(1);
            return;
        }

        if(dialogInputState == DialogInputState::Registration_EnteringPassword)
        {
            registrationPassword = transitionInputSequence(
                DialogInputState::Registration_EnteringPasswordConfirm);

            showInputDialogBoxNice("REGISTRATION", "CONFIRM PASSWORD");
            dialogBox.setInputBoxPassword(true);
            setIgnoreAllInputs(1);
            return;
        }

        if(dialogInputState ==
            DialogInputState::Registration_EnteringPasswordConfirm)
        {
            registrationPasswordConfirm = endInputSequence();

            if(registrationPassword == registrationPasswordConfirm)
            {
                hexagonClient.tryRegister(
                    registrationUsername, registrationPassword);
            }
            else
            {
                playSoundOverride("error.ogg");

                showDialogBox(
                    "PASSWORD MISMATCH\n\n"
                    "PRESS ANY KEY OR BUTTON TO CLOSE THIS MESSAGE\n");

                setIgnoreAllInputs(1);
            }

            return;
        }

        if(dialogInputState == DialogInputState::Login_EnteringUsername)
        {
            loginUsername = transitionInputSequence(
                DialogInputState::Login_EnteringPassword);

            showInputDialogBoxNice("LOGIN", "PASSWORD");
            dialogBox.setInputBoxPassword(true);
            setIgnoreAllInputs(1);
            return;
        }

        if(dialogInputState == DialogInputState::Login_EnteringPassword)
        {
            loginPassword = endInputSequence();
            hexagonClient.tryLogin(loginUsername, loginPassword);
            return;
        }

        if(dialogInputState == DialogInputState::DeleteAccount_EnteringPassword)
        {
            deleteAccountPassword = endInputSequence();
            hexagonClient.tryDeleteAccount(deleteAccountPassword);
            return;
        }
    };

    game.onEvent(sf::Event::EventType::TextEntered) +=
        [this](const sf::Event& mEvent)
    {
        if(dialogBox.empty() || !dialogBox.isInputBox())
        {
            return;
        }

        std::string& input = dialogBox.getInput();

        if(mEvent.text.unicode >= 32 && mEvent.text.unicode < 127)
        {
            if(input.size() < 32)
            {
                playSoundOverride("beep.ogg");
                input.push_back(static_cast<char>(mEvent.text.unicode));
            }
        }
        else if(mEvent.text.unicode == 8) // backspace
        {
            if(!input.empty())
            {
                playSoundOverride("beep.ogg");
                input.pop_back();
            }
        }
    };

    game.onEvent(sf::Event::EventType::KeyReleased) +=
        [this, checkCloseBootScreens, checkCloseDialogBox](
            const sf::Event& mEvent)
    {
        // don't do anything if inputs are being processed as usual
        if(ignoreInputs == 0)
        {
            return;
        }

        // Scenario one: epilepsy warning is being drawn and user
        // must close it with any key press
        if(state == States::EpilepsyWarning || state == States::LoadingScreen)
        {
            checkCloseBootScreens();
            return;
        }

        // Scenario two: actions are blocked cause a dialog box is open
        if(dialogBoxDelay > 0.f)
        {
            return;
        }

        const ssvs::KKey key{mEvent.key.code};
        if(!dialogBox.empty())
        {
            if(dialogBox.getKeyToClose() == ssvs::KKey::Unknown ||
                key == dialogBox.getKeyToClose())
            {
                --ignoreInputs;
            }

            if(dialogBox.isInputBox() && key == ssvs::KKey::Escape)
            {
                setIgnoreAllInputs(0);
                dialogInputState = DialogInputState::Nothing;

                playSoundOverride("select.ogg");
                dialogBox.clearDialogBox();

                return;
            }

            checkCloseDialogBox();
            return;
        }

        // Scenario three: actions are blocked cause we are using a
        // BindControl menu item
        if(getCurrentMenu() != nullptr && key == ssvs::KKey::Escape)
        {
            getCurrentMenu()->getItem().exec(); // turn off bind inputting
            setIgnoreAllInputs(0);
            playSoundOverride("beep.ogg");
            return;
        }

        if((--ignoreInputs) == 0)
        {
            if(getCurrentMenu() == nullptr || state != States::MOpts)
            {
                setIgnoreAllInputs(0);
                return;
            }

            auto* const bc{dynamic_cast<KeyboardBindControl*>(
                &getCurrentMenu()->getItem())};

            // don't try assigning a keyboard key to a controller bind
            if(bc == nullptr)
            {
                playSoundOverride("error.ogg");
                ignoreInputs = 1;
                return;
            }

            // If user tries to bind a key that is already hardcoded ignore
            // the input and notify it of what has happened.
            if(!bc->newKeyboardBind(key))
            {
                playSoundOverride("error.ogg");
                setIgnoreAllInputs(1);
                showDialogBox(
                    "THE KEY YOU ARE TRYING TO ASSIGN TO THIS ACTION\n"
                    "IS ALREADY BOUND TO IT BY DEFAULT,\n"
                    "YOUR LAST INPUT HAS BEEN IGNORED\n\n"
                    "PRESS ANY KEY OR BUTTON TO CLOSE THIS MESSAGE\n");
                return;
            }

            playSoundOverride("select.ogg");
            setIgnoreAllInputs(0);
            touchDelay = 10.f;
        }
    };

    game.onEvent(sf::Event::EventType::MouseButtonReleased) +=
        [this, checkCloseBootScreens, checkCloseDialogBox](
            const sf::Event& mEvent)
    {
        if(ignoreInputs == 0)
        {
            return;
        }

        if(state == States::EpilepsyWarning || state == States::LoadingScreen)
        {
            checkCloseBootScreens();
            return;
        }

        if(dialogBoxDelay > 0.f)
        {
            return;
        }

        if(!dialogBox.empty())
        {
            if(dialogBox.getKeyToClose() != ssvs::KKey::Unknown)
            {
                return;
            }
            --ignoreInputs;
            checkCloseDialogBox();
            return;
        }

        if((--ignoreInputs) == 0)
        {
            if(getCurrentMenu() == nullptr || state != States::MOpts)
            {
                setIgnoreAllInputs(0);
                return;
            }

            auto* const bc{dynamic_cast<KeyboardBindControl*>(
                &getCurrentMenu()->getItem())};

            // don't try assigning a keyboard key to a controller bind
            if(bc == nullptr)
            {
                playSoundOverride("error.ogg");
                ignoreInputs = 1;
                return;
            }

            bc->newKeyboardBind(mEvent.mouseButton.button);
            playSoundOverride("select.ogg");
            setIgnoreAllInputs(0);
            touchDelay = 10.f;
        }
    };

    game.onEvent(sf::Event::EventType::JoystickButtonReleased) +=
        [this, checkCloseBootScreens, checkCloseDialogBox](
            const sf::Event& mEvent)
    {
        if(ignoreInputs == 0)
        {
            return;
        }

        if(state == States::EpilepsyWarning || state == States::LoadingScreen)
        {
            checkCloseBootScreens();
            return;
        }

        if(dialogBoxDelay > 0.f)
        {
            return;
        }

        if(!dialogBox.empty())
        {
            if(dialogBox.getKeyToClose() != ssvs::KKey::Unknown)
            {
                return;
            }
            --ignoreInputs;
            checkCloseDialogBox();
            return;
        }

        if((--ignoreInputs) == 0)
        {
            if(getCurrentMenu() == nullptr || state != States::MOpts)
            {
                setIgnoreAllInputs(0);
                return;
            }

            auto* const bc{dynamic_cast<JoystickBindControl*>(
                &getCurrentMenu()->getItem())};

            // don't try assigning a controller button to a keyboard bind
            if(bc == nullptr)
            {
                playSoundOverride("error.ogg");
                ignoreInputs = 1;
                return;
            }

            bc->newJoystickBind(mEvent.joystickButton.button);
            setIgnoreAllInputs(0);
            playSoundOverride("select.ogg");
            touchDelay = 10.f;
        }
    };

    window.onRecreation += [this]
    {
        refreshCamera();
        adjustLevelsOffset();
        adjustMenuOffset(true);
        resetNamesScrolls();
        mustRefresh = true;
    };

    initMenus();
    initInput();
    initLua();

    //--------------------------------
    // Main menu background

    {
        const auto [randomPack, randomLevel] =
            pickRandomMainMenuBackgroundStyle();

        const std::string& randomPackId =
            getNthSelectablePackInfo(randomPack).id;

        lvlSlct.levelDataIds = &assets.getLevelIdsByPack(randomPackId);
        setIndex(randomLevel);
    }

    // Setup for the loading menu
    static constexpr std::array<std::array<std::string_view, 2>, 4> tips{
        {{"HOLDING FOCUS WHILE CHANGING PACK", "SKIPS THE SWITCH ANIMATION"},
            {"REMEMBER TO TAKE BREAKS", "OPEN HEXAGON IS AN INTENSE GAME"},
            {"EXPERIMENT USING SWAP", "IT MAY SAVE YOUR LIFE"},
            {"IF A LEVEL IS TOO CHALLENGING",
                "PRACTICE IT AT A LOWER DIFFICULTY"}}};
    randomTip = tips[ssvu::getRndI(0, tips.size())];

    // Set size of the level offsets vector to the minimum required
    unsigned int maxSize{0}, packSize;
    for(std::size_t i{0}; i < getSelectablePackInfosSize(); ++i)
    {
        const std::string& packId = getNthSelectablePackInfo(i).id;

        if(!assets.packHasLevels(packId))
        {
            continue;
        }

        packSize = assets.getLevelIdsByPack(packId).size();
        if(packSize > maxSize)
        {
            maxSize = packSize;
        }
    }
    lvlSlct.lvlOffsets.resize(maxSize);
}

MenuGame::~MenuGame()
{
    ssvu::lo("MenuGame::~MenuGame") << "Cleaning up menu resources...\n";
}

void MenuGame::init(bool error)
{
    steamManager.set_rich_presence_in_menu();
    steamManager.update_hardcoded_achievements();

    discordManager.set_rich_presence_in_menu();

    audio.stopMusic();
    audio.stopSounds();

    if(error)
    {
        playSoundOverride("error.ogg");
    }
    else
    {
        playSoundOverride("select.ogg");
    }

    // Online::setForceLeaderboardRefresh(true);
}

void MenuGame::init(
    bool error, const std::string& pack, const std::string& level)
{
    init(error);
    loadCommandLineLevel(pack, level);
}

void MenuGame::initAssets()
{
    for(const auto& t : {"titleBar.png", "creditsBar1.png", "creditsBar2.png",
            "creditsBar2b.png", "creditsBar2c.png", "creditsBar2d.png",
            "bottomBar.png", "epilepsyWarning.png"})
    {
        assets.getTexture(t).setSmooth(true);
    }
}

void MenuGame::changeStateTo(const States mState)
{
    const States prevState = state;
    state = mState;

    if(prevState == state)
    {
        // Not a state transition.
        return;
    }

    if(state == States::LevelSelection)
    {
        firstLevelSelection = false;
    }

    if(!showFirstTimeTips)
    {
        // Not the first time playing.
        return;
    }

    const auto mustShowTip = [&](const States s, bool& flag)
    { return state == s && std::exchange(flag, false); };

    const auto showTip = [&](const char* str)
    {
        playSoundOverride("select.ogg");

        showDialogBox(str);
        setIgnoreAllInputs(1);

        // Prevent dialog box from being closed immediately:
        dialogBoxDelay = 64.f;
    };

    if(mustShowTip(States::SMain, mustShowFTTMainMenu))
    {
        showTip(
            "WELCOME TO OPEN HEXAGON!\n\n"
            "YOU CAN NAVIGATE THE MAIN MENU WITH THE UP/DOWN ARROW KEYS\n"
            "OR THE DPAD/THUMBSTICK ON YOUR CONTROLLER\n\n"
            "REMEMBER TO CHECK OUT THE OPTIONS MENU\n\n"
            "PRESS ANY KEY OR BUTTON TO CLOSE THIS MESSAGE\n");
    }
    else if(mustShowTip(States::LevelSelection, mustShowFTTLevelSelect))
    {
        showTip(
            "THIS IS WHERE YOU CAN PICK A LEVEL TO PLAY\n\n"
            "LEVELS ARE ORGANIZED IN 'LEVEL PACKS'\n"
            "TO BROWSE LEVELS AND LEVEL PACKS, GO UP/DOWN\n"
            "HOLD SHIFT (FOCUS) TO QUICKLY JUMP BETWEEN PACKS\n"
            "TO CHANGE THE DIFFICULTY OF A LEVEL, GO LEFT/RIGHT\n\n"
            "AS A FIRST TIMER, PLAY THE 'CUBE' LEVELS IN ORDER\n"
            "THEN YOU CAN GET NEW LEVELS ON THE STEAM WORKSHOP\n\n"
            "HAVE FUN!\n\n"
            "PRESS ANY KEY OR BUTTON TO CLOSE THIS MESSAGE\n");
    }
    else if(mustShowTip(States::LevelSelection, mustShowFTTDeathTips))
    {
        showTip(
            "IF YOU FEEL LIKE THE GAME IS TOO HARD, TRY THESE TIPS\n\n"
            "- DECREMENT THE DIFFICULTY BY PRESSING LEFT\n"
            "- PLAY AN EASIER LEVEL TO BUILD UP MUSCLE MEMORY\n"
            "- TURN ON 'INVINCIBILITY' IN 'GAMEPLAY' OPTIONS TO PRACTICE\n\n"
            "REMEMBER THAT PRACTICE IS THE SECRET TO SUCCESS!\n\n"
            "PRESS ANY KEY OR BUTTON TO CLOSE THIS MESSAGE\n");
    }
}

void MenuGame::initInput()
{
    using k = ssvs::KKey;
    using t = ssvs::Input::Type;
    using Tid = Config::Tid;

    const auto addTidInput = [&](const Tid tid, const t type, auto action)
    {
        game.addInput(
            Config::getTrigger(tid), action, type, static_cast<int>(tid));
    };

    addTidInput(Tid::RotateCCW, t::Once,
        [this](ssvu::FT)
        {
            if(!mouseHovering)
            {
                leftAction();
            }
        });

    addTidInput(Tid::RotateCW, t::Once,
        [this](ssvu::FT)
        {
            if(!mouseHovering)
            {
                rightAction();
            }
        });

    game.addInput( // hardcoded
        {{k::Up}}, [this](ssvu::FT) { upAction(); }, t::Once);

    addTidInput(Tid::Up, t::Once, [this](ssvu::FT) { upAction(); });

    game.addInput( // hardcoded
        {{k::Down}}, [this](ssvu::FT) { downAction(); }, t::Once);

    addTidInput(Tid::Down, t::Once, [this](ssvu::FT) { downAction(); });

    addTidInput(
        Tid::NextPack, t::Once, [this](ssvu::FT) { changePackAction(1); });

    addTidInput(
        Tid::PreviousPack, t::Once, [this](ssvu::FT) { changePackAction(-1); });

    add2StateInput(game, Config::getTrigger(Tid::Focus), focusHeld,
        static_cast<int>(Tid::Focus));

    game.addInput( // hardcoded
        {{k::Return}}, [this](ssvu::FT /*unused*/) { okAction(); }, t::Once);

    game.addInput( // hardcoded
        {{k::Escape}},
        [this](ssvs::FT mFT)
        {
            if(state != States::MOpts)
            {
                exitTimer += mFT;
            }
        },
        [this](ssvu::FT /*unused*/) { exitTimer = 0; }, t::Always);

    game.addInput( // hardcoded
        {{k::Escape}}, [this](ssvu::FT /*unused*/) { exitAction(); }, t::Once);

    addTidInput(Tid::Exit, t::Once,
        [this](ssvu::FT /*unused*/)
        {
            if(isEnteringText())
            {
                return;
            }
            exitAction();
        }); // editable

    addTidInput(Tid::Screenshot, t::Once,
        [this](ssvu::FT /*unused*/) { mustTakeScreenshot = true; });

    game.addInput(
            {{k::LAlt, k::Return}},
            [this](ssvu::FT /*unused*/)
            {
                Config::setFullscreen(window, !window.getFullscreen());
                game.ignoreNextInputs();
            },
            t::Once)
        .setPriorityUser(-1000);

    game.addInput( // hardcoded
        {{k::BackSpace}}, [this](ssvu::FT /*unused*/) { eraseAction(); },
        t::Once);

    game.addInput( // hardcoded
        {{k::F1}}, [this](ssvu::FT /*unused*/) { addRemoveFavoriteLevel(); },
        t::Once);

    game.addInput( // hardcoded
        {{k::F2}},
        [this](ssvu::FT /*unused*/) { switchToFromFavoriteLevels(); }, t::Once);

    game.addInput( // hardcoded
        {{k::F3}}, [this](ssvu::FT /*unused*/) { reloadAssets(false); },
        t::Once);

    game.addInput( // hardcoded
        {{k::F4}}, [this](ssvu::FT /*unused*/) { reloadAssets(true); },
        t::Once);
}

void MenuGame::runLuaFile(const std::string& mFileName)
try
{
    Utils::runLuaFile(lua, mFileName);
}
catch(...)
{
    playSoundOverride("error.ogg");
    ssvu::lo("hg::MenuGame::initLua") << "Fatal error in menu for Lua file '"
                                      << mFileName << '\'' << std::endl;
}

void MenuGame::changeResolutionTo(unsigned int mWidth, unsigned int mHeight)
{
    if(Config::getWidth() == mWidth && Config::getHeight() == mHeight)
    {
        return;
    }

    Config::setCurrentResolution(mWidth, mHeight);
    window.getRenderWindow().setSize(sf::Vector2u{mWidth, mHeight});

    refreshCamera();
    adjustLevelsOffset();
    adjustMenuOffset(true);
    resetNamesScrolls();
}

void MenuGame::playSoundOverride(const std::string& assetId)
{
    if(!Config::getNoSound())
    {
        audio.playSoundOverride(assetId);
    }
}

void MenuGame::initLua()
{
    static CCustomWallManager cwManager;
    static random_number_generator rng{0};
    static HexagonGameStatus hexagonGameStatus;

    LuaScripting::init(lua, rng, true /* inMenu */, cwManager, levelStatus,
        hexagonGameStatus, styleData);

    lua.writeVariable("u_log",
        [](const std::string& mLog) { ssvu::lo("lua-menu") << mLog << '\n'; });

    lua.writeVariable("u_execScript",
        [this](const std::string& mScriptName)
        {
            runLuaFile(Utils::getDependentScriptFilename(
                execScriptPackPathContext, levelData->packPath, mScriptName));
        });

    lua.writeVariable("u_execDependencyScript", //
        [this](const std::string& mPackDisambiguator,
            const std::string& mPackName, const std::string& mPackAuthor,
            const std::string& mScriptName)
        {
            SSVOH_ASSERT(currentPack != nullptr);

            Utils::withDependencyScriptFilename(
                [this](const std::string& filename) { runLuaFile(filename); },
                execScriptPackPathContext, assets, *currentPack,
                mPackDisambiguator, mPackName, mPackAuthor, mScriptName);
        });

    lua.writeVariable("u_getDifficultyMult", [] { return 1; });

    lua.writeVariable("u_getSpeedMultDM", [] { return 1; });

    lua.writeVariable("u_getDelayMultDM", [] { return 1; });

    lua.writeVariable("u_getPlayerAngle", [] { return 0; });

    // Unused functions
    for(const auto& un : {"u_isKeyPressed", "u_isMouseButtonPressed",
            "u_isFastSpinning", "u_setPlayerAngle", "u_forceIncrement",
            "u_haltTime", "u_timelineWait", "u_clearWalls", "u_setFlashEffect",

            "a_getMusicSeconds", "a_getMusicMilliseconds", "a_evalTo",
            "a_evalToMs", "a_clearTimepoint", "a_clearTimepointMs", "a_clear",
            "a_setMusic", "a_setMusicSegment", "a_setMusicSeconds",
            "a_playSound", "a_playPackSound", "a_syncMusicToDM",
            "a_setMusicPitch", "a_overrideBeepSound",
            "a_overrideIncrementSound", "a_overrideSwapSound",
            "a_overrideDeathSound",

            "t_eval", "t_kill", "t_clear", "t_wait", "t_waitS", "t_waitUntilS",

            "e_eval", "e_kill", "e_stopTime", "e_stopTimeS", "e_wait",
            "e_waitS", "e_waitUntilS", "e_messageAdd", "e_messageAddImportant",
            "e_messageAddImportantSilent", "e_clearMessages",

            "l_overrideScore", "l_setRotation", "l_getRotation",
            "l_getOfficial",

            "s_setStyle",

            "w_wall", "w_wallAdj", "w_wallAcc", "w_wallHModSpeedData",
            "w_wallHModCurveData",

            "steam_unlockAchievement",

            "u_kill", "u_eventKill", "u_playSound", "u_playPackSound",
            "e_eventStopTime", "e_eventStopTimeS", "e_eventWait",
            "e_eventWaitS", "e_eventWaitUntilS", "m_messageAdd",
            "m_messageAddImportant", "m_messageAddImportantSilent",
            "m_clearMessages"})
    {
        lua.writeVariable(un, [] {});
    }
}

void MenuGame::initMenus()
{
    namespace i = ssvms::Items;

    auto whenNotOfficial = [] { return !Config::getOfficial(); };
    auto whenSoundEnabled = [] { return !Config::getNoSound(); };
    auto whenMusicEnabled = [] { return !Config::getNoMusic(); };

    // Welcome menu
    auto& wlcm(welcomeMenu.createCategory("welcome"));
    wlcm.create<i::Single>("play locally", [this] { playLocally(); });
    wlcm.create<i::Single>("exit game", [this] { window.stop(); });

    //--------------------------------
    // OPTIONS MENU
    //--------------------------------

    auto& options(optionsMenu.createCategory("options"));
    auto& play(optionsMenu.createCategory("gameplay"));
    auto& controls(optionsMenu.createCategory("controls"));
    auto& keyboard(optionsMenu.createCategory("keyboard"));
    auto& joystick(optionsMenu.createCategory("joystick"));
    auto& resolution(optionsMenu.createCategory("resolution"));
    auto& gfx(optionsMenu.createCategory("graphics"));
    auto& sfx(optionsMenu.createCategory("audio"));

    options.create<i::Goto>("GAMEPLAY", play);
    options.create<i::Goto>("CONTROLS", controls);
    options.create<i::Goto>("RESOLUTION", resolution);
    options.create<i::Goto>("GRAPHICS", gfx);
    options.create<i::Goto>("AUDIO", sfx);
    options.create<i::Single>("RESET CONFIG",
        [this]
        {
            Config::resetConfigToDefaults();
            refreshBinds();
        });

    //--------------------------------
    // Gameplay

    play.create<i::Toggle>(
        "autorestart", &Config::getAutoRestart, &Config::setAutoRestart);
    play.create<i::Toggle>("rotate to start", &Config::getRotateToStart,
        &Config::setRotateToStart);
    play.create<i::Toggle>(
        "OFFICIAL MODE", &Config::getOfficial, &Config::setOfficial);
    play.create<i::Toggle>("debug mode", &Config::getDebug, &Config::setDebug) |
        whenNotOfficial;
    play.create<i::Toggle>(
        "invincible", &Config::getInvincible, &Config::setInvincible) |
        whenNotOfficial;
    play.create<i::Slider>("timescale", &Config::getTimescale,
        &Config::setTimescale, 0.1f, 2.f, 0.05f) |
        whenNotOfficial;
    play.create<i::GoBack>("back");

    //--------------------------------
    // Controls

    controls.create<i::Goto>("keyboard", keyboard);
    controls.create<i::Goto>("joystick", joystick);
    controls.create<i::Slider>("joystick deadzone",
        &Config::getJoystickDeadzone, &Config::setJoystickDeadzone, 0.f, 100.f,
        1.f);
    controls.create<i::Single>("reset binds",
        [this]
        {
            Config::resetBindsToDefaults();
            refreshBinds();
        });
    controls.create<i::Single>("hardcoded keys reference",
        [this]
        {
            showDialogBox(
                "UP ARROW - UP\n"
                "DOWN ARROW - DOWN\n"
                "RETURN - ENTER\n"
                "BACKSPACE - REMOVE BIND\n"
                "F1 - ADD LEVEL TO FAVORITES\n"
                "F2 - SWITCH TO/FROM FAVORITE LEVELS\n"
                "F3 - RELOAD LEVEL ASSETS (DEBUG MODE ONLY)\n"
                "F4 - RELOAD PACK ASSETS (DEBUG MODE ONLY)\n\n"
                "PRESS ANY KEY OR BUTTON TO CLOSE THIS MESSAGE\n");
            setIgnoreAllInputs(2);
        });
    controls.create<i::GoBack>("back");

    // Keyboard binds
    const auto callBack =
        [this](const ssvs::Input::Trigger& trig, const int bindID)
    {
        game.refreshTrigger(trig, bindID);

        if(fnHGTriggerRefresh)
        {
            fnHGTriggerRefresh(trig, bindID);
        }
    };

    using Tid = Config::Tid;

    const auto mkAddBindFn = [](ssvs::Input::Trigger& trig)
    {
        return [&trig](const int key, const int btn, const int index)
        { Config::rebindTrigger(trig, key, btn, index); };
    };

    const auto mkClearBindFn = [](ssvs::Input::Trigger& trig) {
        return [&trig](const int index)
        { Config::clearTriggerBind(trig, index); };
    };

    const auto createKeyboardBindControl =
        [&](const char* name, const Tid tid,
            const ssvs::KKey hardcodedKey = ssvs::KKey::Unknown)
    {
        const auto trigGetter = Config::triggerGetters[toSizeT(tid)];

        ssvs::Input::Trigger& trig = trigGetter();

        keyboard.create<KeyboardBindControl>(name, trigGetter,
            mkAddBindFn(trig), mkClearBindFn(trig), callBack,
            static_cast<int>(tid), hardcodedKey);
    };

    createKeyboardBindControl("rotate ccw", Tid::RotateCCW);
    createKeyboardBindControl("rotate cw", Tid::RotateCW);
    createKeyboardBindControl("focus", Tid::Focus);
    createKeyboardBindControl("exit", Tid::Exit, ssvs::KKey::Escape);
    createKeyboardBindControl("force restart", Tid::ForceRestart);
    createKeyboardBindControl("restart", Tid::Restart);
    createKeyboardBindControl("replay", Tid::Replay);
    createKeyboardBindControl("screenshot", Tid::Screenshot);
    createKeyboardBindControl("swap", Tid::Swap);
    createKeyboardBindControl("up", Tid::Up, ssvs::KKey::Up);
    createKeyboardBindControl("down", Tid::Down, ssvs::KKey::Down);
    createKeyboardBindControl("next pack", Tid::NextPack);
    createKeyboardBindControl("previous pack", Tid::PreviousPack);
    createKeyboardBindControl("lua console (debug only)", Tid::LuaConsole);
    createKeyboardBindControl("pause game (debug only)", Tid::Pause);
    keyboard.create<i::GoBack>("back");

    // Joystick binds
    using Jid = Joystick::Jid;

    const auto joystickCallBack =
        [](const unsigned int button, const int buttonID)
    { Joystick::setJoystickBind(button, buttonID); };

    const auto createJoystickBindControl = [&](const char* name, const Jid jid)
    {
        const auto btnGetter = Config::joystickTriggerGetters[toSizeT(jid)];

        const auto btnSetter = Config::joystickTriggerSetters[toSizeT(jid)];

        joystick.create<JoystickBindControl>(name, btnGetter, btnSetter,
            joystickCallBack, static_cast<int>(jid));
    };

    createJoystickBindControl("select", Jid::Select);
    createJoystickBindControl("exit", Jid::Exit);
    createJoystickBindControl("focus", Jid::Focus);
    createJoystickBindControl("swap", Jid::Swap);
    createJoystickBindControl("force restart", Jid::ForceRestart);
    createJoystickBindControl("restart", Jid::Restart);
    createJoystickBindControl("replay", Jid::Replay);
    createJoystickBindControl("screenshot", Jid::Screenshot);
    createJoystickBindControl("next pack", Jid::NextPack);
    createJoystickBindControl("previous pack", Jid::PreviousPack);
    createJoystickBindControl("(un)favorite level", Jid::AddToFavorites);
    createJoystickBindControl("favorites menu", Jid::FavoritesMenu);
    joystick.create<i::GoBack>("back");

    //--------------------------------
    // Resolution

    resolution.create<i::Single>("automatically set resolution",
        [this] { Config::setCurrentResolutionAuto(window); });

    auto& sixByNine(optionsMenu.createCategory("16x9 resolutions"));
    auto& fourByThree(optionsMenu.createCategory("4x3 resolutions"));
    auto& sixByTen(optionsMenu.createCategory("16x10 resolutions"));

    resolution.create<i::Goto>("16x9 resolutions", sixByNine);
    resolution.create<i::Goto>("4x3 resolutions", fourByThree);
    resolution.create<i::Goto>("16x10 - uncommon resolutions", sixByTen);

    // Organize available resolutions based on their aspect ratio.
    int ratio;
    for(const auto& vm : sf::VideoMode::getFullscreenModes())
    {
        if(vm.bitsPerPixel == 32)
        {
            ratio = 10.f * vm.width / vm.height;

            switch(ratio)
            {
                case 17: // 16:9
                    sixByNine.create<i::Single>(
                        ssvu::toStr(vm.width) + "x" + ssvu::toStr(vm.height),
                        [this, &vm]
                        { changeResolutionTo(vm.width, vm.height); });
                    break;

                case 13: // 4:3
                    fourByThree.create<i::Single>(
                        ssvu::toStr(vm.width) + "x" + ssvu::toStr(vm.height),
                        [this, &vm]
                        { changeResolutionTo(vm.width, vm.height); });
                    break;

                default: // 16:10 and uncommon
                    sixByTen.create<i::Single>(
                        ssvu::toStr(vm.width) + "x" + ssvu::toStr(vm.height),
                        [this, &vm]
                        { changeResolutionTo(vm.width, vm.height); });
                    break;
            }
        }
    }

    sixByNine.create<i::GoBack>("back");
    fourByThree.create<i::GoBack>("back");
    sixByTen.create<i::GoBack>("back");

    resolution.create<i::Single>(
        "go windowed", [this] { Config::setFullscreen(window, false); });
    resolution.create<i::Single>(
        "go fullscreen", [this] { Config::setFullscreen(window, true); });
    resolution.create<i::GoBack>("back");

    //--------------------------------
    // Graphics

    auto& visfx(optionsMenu.createCategory("visual fxs"));
    gfx.create<i::Goto>("visual fxs", visfx);
    visfx.create<i::Toggle>("3D effects", &Config::get3D, &Config::set3D);
    visfx.create<i::Toggle>(
        "no rotation", &Config::getNoRotation, &Config::setNoRotation) |
        whenNotOfficial;
    visfx.create<i::Toggle>(
        "no background", &Config::getNoBackground, &Config::setNoBackground) |
        whenNotOfficial;
    visfx.create<i::Toggle>(
        "b&w colors", &Config::getBlackAndWhite, &Config::setBlackAndWhite) |
        whenNotOfficial;
    visfx.create<i::Toggle>("pulse", &Config::getPulse, &Config::setPulse) |
        whenNotOfficial;
    visfx.create<i::Toggle>("flash", &Config::getFlash, &Config::setFlash);
    visfx.create<i::GoBack>("back");

    auto& fps(optionsMenu.createCategory("fps settings"));
    gfx.create<i::Goto>("fps settings", fps);
    fps.create<i::Toggle>("vsync", &Config::getVsync,
        [this](bool mValue) { Config::setVsync(window, mValue); });
    fps.create<i::Toggle>("limit fps", &Config::getLimitFPS,
        [this](bool mValue) { Config::setLimitFPS(window, mValue); });
    fps.create<i::Slider>(
        "max fps", &Config::getMaxFPS,
        [this](unsigned int mValue) { Config::setMaxFPS(window, mValue); }, 30u,
        200u, 5u);
    fps.create<i::Toggle>("show fps", &Config::getShowFPS, &Config::setShowFPS);
    fps.create<i::GoBack>("back");

    gfx.create<i::Toggle>("text outlines", &Config::getDrawTextOutlines,
        &Config::setDrawTextOutlines);
    gfx.create<i::Slider>(
        "text padding", &Config::getTextPadding,
        [](float mValue) { Config::setTextPadding(mValue); }, 0.f, 64.f, 1.f);
    gfx.create<i::Slider>(
        "text scaling", &Config::getTextScaling,
        [](float mValue) { Config::setTextScaling(mValue); }, 0.1f, 4.f, 0.05f);

    gfx.create<i::Slider>(
        "antialiasing",
        [] { return Utils::concat(Config::getAntialiasingLevel(), 'x'); },
        [this]
        {
            if(Config::getAntialiasingLevel() == 0)
            {
                Config::setAntialiasingLevel(window, 1);
                return;
            }

            Config::setAntialiasingLevel(
                window, ssvu::getClamped(
                            Config::getAntialiasingLevel() << 1u, 0u, 16u));
        },
        [this]
        {
            Config::setAntialiasingLevel(
                window, ssvu::getClamped(
                            Config::getAntialiasingLevel() >> 1u, 0u, 16u));
        });

    gfx.create<i::Toggle>("darken background chunk",
        &Config::getDarkenUnevenBackgroundChunk,
        &Config::setDarkenUnevenBackgroundChunk);
    gfx.create<i::Toggle>(
        "show key icons", &Config::getShowKeyIcons, &Config::setShowKeyIcons);
    gfx.create<i::Slider>(
        "key icons scaling", &Config::getKeyIconsScale,
        [](float mValue) { Config::setKeyIconsScale(mValue); }, 0.1f, 4.f,
        0.05f);
    gfx.create<i::Toggle>("show level info", &Config::getShowLevelInfo,
        &Config::setShowLevelInfo);
    gfx.create<i::Toggle>(
        "show timer", &Config::getShowTimer, &Config::setShowTimer) |
        whenNotOfficial;
    gfx.create<i::Toggle>("show status text", &Config::getShowStatusText,
        &Config::setShowStatusText) |
        whenNotOfficial;

    gfx.create<i::GoBack>("back");

    //--------------------------------
    // Sound

    sfx.create<i::Toggle>("no sound", &Config::getNoSound, &Config::setNoSound);
    sfx.create<i::Toggle>("no music", &Config::getNoMusic, &Config::setNoMusic);
    sfx.create<i::Slider>(
        "sound volume", &Config::getSoundVolume,
        [this](unsigned int mValue)
        {
            Config::setSoundVolume(mValue);
            audio.setSoundVolume(mValue);
        },
        0u, 100u, 5u) |
        whenSoundEnabled;
    sfx.create<i::Slider>(
        "music volume", &Config::getMusicVolume,
        [this](unsigned int mValue)
        {
            Config::setMusicVolume(mValue);
            audio.setMusicVolume(mValue);
        },
        0u, 100u, 5u) |
        whenMusicEnabled;
    sfx.create<i::Toggle>("sync music with difficulty",
        &Config::getMusicSpeedDMSync, &Config::setMusicSpeedDMSync) |
        whenMusicEnabled;
    sfx.create<i::Slider>(
        "music speed multiplier", &Config::getMusicSpeedMult,
        [](float mValue) { Config::setMusicSpeedMult(mValue); }, 0.7f, 1.3f,
        0.05f) |
        whenMusicEnabled;
    sfx.create<i::GoBack>("back");


    //--------------------------------
    // MAIN MENU
    //--------------------------------

    auto& main{mainMenu.createCategory("main")};
    auto& localProfiles{mainMenu.createCategory("local profiles")};
    main.create<i::Single>("LEVEL SELECT",
        [this]
        {
            if(firstLevelSelection)
            {
                lvlSlct.packIdx = diffMultIdx = 0;
                lvlSlct.levelDataIds =
                    &assets.getLevelIdsByPack(getNthSelectablePackInfo(0).id);
                setIndex(0);
            }
            changeStateTo(States::LevelSelection);
            playSoundOverride("select.ogg");
        });
    main.create<i::Goto>("LOCAL PROFILES", localProfiles);
    main.create<i::Single>(
        "ONLINE", [this] { changeStateTo(States::MOnline); });
    main.create<i::Single>("OPTIONS", [this] { changeStateTo(States::MOpts); });
    main.create<i::Single>("EXIT", [this] { window.stop(); });

    //--------------------------------
    // ONLINE MENU
    //--------------------------------

    auto whenMustConnect = [this]
    {
        return hexagonClient.getState() == HexagonClient::State::Disconnected ||
               hexagonClient.getState() ==
                   HexagonClient::State::ConnectionError;
    };

    auto whenMustLogin = [this]
    {
        return hexagonClient.getState() == HexagonClient::State::Connected &&
               hexagonClient.hasRTKeys();
    };

    auto whenMustRegister = [this]
    {
        return hexagonClient.getState() == HexagonClient::State::Connected &&
               hexagonClient.hasRTKeys();
    };

    auto whenConnected = [this]
    { return hexagonClient.getState() == HexagonClient::State::Connected; };

    auto whenLoggedIn = [this]
    {
        return (hexagonClient.getState() == HexagonClient::State::LoggedIn ||
                   hexagonClient.getState() ==
                       HexagonClient::State::LoggedIn_Ready) &&
               hexagonClient.hasRTKeys();
    };

    auto whenMustDeleteAccount = [this]
    {
        return hexagonClient.getState() == HexagonClient::State::Connected &&
               hexagonClient.hasRTKeys();
    };


    auto& online(onlineMenu.createCategory("options"));

    online.create<i::Single>("CONNECT", [this] { hexagonClient.connect(); }) |
        whenMustConnect;

    online.create<i::Single>("LOGIN",
        [this]
        {
            if(dialogInputState != DialogInputState::Nothing)
            {
                return;
            }

            dialogInputState = DialogInputState::Login_EnteringUsername;

            showInputDialogBoxNice("LOGIN", "USERNAME");
            setIgnoreAllInputs(2);
        }) |
        whenMustLogin;

    online.create<i::Single>("REGISTER",
        [this]
        {
            if(dialogInputState != DialogInputState::Nothing)
            {
                return;
            }

            dialogInputState = DialogInputState::Registration_EnteringUsername;

            showInputDialogBoxNice("REGISTRATION", "USERNAME");
            setIgnoreAllInputs(2);
        }) |
        whenMustRegister;

    online.create<i::Single>(
        "LOGOUT", [this] { hexagonClient.tryLogoutFromServer(); }) |
        whenLoggedIn;

    online.create<i::Single>(
        "DISCONNECT", [this] { hexagonClient.disconnect(); }) |
        whenConnected;

    online.create<i::Single>("DELETE ACCOUNT",
        [this]
        {
            if(dialogInputState != DialogInputState::Nothing)
            {
                return;
            }

            dialogInputState = DialogInputState::DeleteAccount_EnteringPassword;

            showInputDialogBoxNice("DELETE ACCOUNT", "PASSWORD",
                "WARNING: THIS WILL DELETE ALL YOUR SCORES");
            dialogBox.setInputBoxPassword(true);
            setIgnoreAllInputs(2);
        }) |
        whenMustDeleteAccount;

    //--------------------------------
    // PROFILES MENU
    //--------------------------------

    localProfiles.create<i::Single>(
        "CHOOSE PROFILE", [this] { changeStateTo(States::SLPSelect); });
    localProfiles.create<i::Single>("NEW PROFILE",
        [this]
        {
            changeStateTo(States::ETLPNew);
            enteredStr = "";
            playSoundOverride("select.ogg");
        });
    localProfiles.create<i::GoBack>("BACK");

    //--------------------------------
    // Profiles selection

    auto& profileSelection{
        profileSelectionMenu.createCategory("profile selection")};

    for(auto& p : assets.getLocalProfileNames())
    {
        profileSelection.create<i::Single>(p,
            [this, p]
            {
                assets.pSetCurrent(p);
                changeFavoriteLevelsToProfile();
            });
    }

    profileSelection.sortByName();
}

bool MenuGame::loadCommandLineLevel(
    const std::string& pack, const std::string& level)
{
    // First find the ID of the pack with name matching the one typed by the
    // user. `packDatas` is the only vector in assets with a data type
    // containing the name of the pack (without it being part of the id).
    std::string packID;
    for(auto& d : assets.getPackDatas())
    {
        if(d.second.name == pack)
        {
            packID = d.second.id;
            break;
        }
    }

    if(packID.empty())
    {
        ssvu::lo("hg::Menugame::MenuGame()")
            << "Invalid pack name '" << pack
            << "' command line parameter, aborting boot level load\n";

        return false;
    }

    if(!assets.packHasLevels(packID))
    {
        ssvu::lo("hg::Menugame::MenuGame()")
            << "Pack '" << pack
            << "' has no levels, aborting boot level load\n";

        return false;
    }

    // Iterate through packInfos to find the menu pack index and the index
    // of the level.
    const std::string levelID{packID + "_" + level};
    const auto& p{assets.getSelectablePackInfos()};
    const auto& levelsList{assets.getLevelIdsByPack(packID)};

    for(int i{0}; i < static_cast<int>(p.size()); ++i)
    {
        // once you find the pack index search if it contains the level
        if(packID != p.at(i).id)
        {
            continue;
        }

        auto it{std::find(levelsList.begin(), levelsList.end(), levelID)};
        if(it == levelsList.end())
        {

            ssvu::lo("hg::Menugame::MenuGame()")
                << "Invalid level name '" << level
                << "' command line parameter, aborting boot level load\n";

            return false;
        }

        // Level found, initialize parameters
        lvlSlct.packIdx = i;
        lvlSlct.levelDataIds = &levelsList;
        setIndex(it - levelsList.begin());

        break;
    }

    // Do the sequence of actions user would have
    // to do manually to get to the desired level

    playLocally(); // go to profile selection screen

    if(state == States::ETLPNew)
    {
        ssvu::lo("hg::Menugame::MenuGame()")
            << "No player profiles exist, aborting boot level load\n";

        return false;
    }

    // Go to main menu
    enteredStr = assets.getLocalProfileNames()[0];
    assets.pSetCurrent(enteredStr);
    changeStateTo(States::SMain);

    // Go to level selection
    resetNamesScrolls();
    changeStateTo(States::LevelSelection);

    // Start game
    playSelectedLevel();

    return true;
}

void MenuGame::playLocally()
{
    assets.pSaveCurrent();
    enteredStr = "";
    state = assets.getLocalProfilesSize() == 0 ? States::ETLPNewBoot
                                               : States::SLPSelectBoot;
}

[[nodiscard]] std::pair<const unsigned int, const unsigned int>
MenuGame::pickRandomMainMenuBackgroundStyle()
{
    // If there is no `menubackgrounds.json` abort
    if(!ssvufs::Path{"Assets/menubackgrounds.json"}
            .exists<ssvufs::Type::File>())
    {
        ssvu::lo("MenuGame::$")
            << "File 'Assets/menubackgrounds.json' does not exist" << std::endl;

        return {0, 0};
    }

    std::vector<std::string> levelIDs;
    ssvuj::Obj object = ssvuj::getFromFile("Assets/menubackgrounds.json");
    for(const auto& f : ssvuj::getExtr<std::vector<std::string>>(object, "ids"))
    {
        levelIDs.emplace_back(f);
    }

    // pick one of those at random
    const std::string pickedLevel{levelIDs[ssvu::getRndI(0, levelIDs.size())]};

    // retrieve the level index location
    const auto& p(assets.getSelectablePackInfos());
    const std::vector<std::string>* levelsIDs;

    // store info main menu requires to set the color theme
    for(int i{0}; i < static_cast<int>(p.size()); ++i)
    {
        const std::string& packId = p.at(i).id;

        if(!assets.packHasLevels(packId))
        {
            continue;
        }

        levelsIDs = &assets.getLevelIdsByPack(packId);
        auto it = std::find(levelsIDs->begin(), levelsIDs->end(), pickedLevel);
        if(it != levelsIDs->end())
        {
            return {i, it - levelsIDs->begin()};
        }
    }

    return {0, 0};
}

//*****************************************************
//
// NAVIGATION
//
//*****************************************************

void MenuGame::leftAction()
{
    if(state == States::SLPSelectBoot)
    {
        okAction();
        return;
    }

    // Change difficulty in the level selection menu.
    if(state == States::LevelSelection)
    {
        --diffMultIdx;
        difficultyBumpEffect = difficultyBumpEffectMax;
        playSoundOverride("difficultyMultDown.ogg");
        touchDelay = 50.f;
        return;
    }

    // If there is no valid action abort.
    if(!isInMenu() || !getCurrentMenu()->getItem().canIncrease())
    {
        return;
    }

    getCurrentMenu()->decrease();
    playSoundOverride("beep.ogg");
    touchDelay = 50.f;
}

void MenuGame::rightAction()
{
    if(state == States::SLPSelectBoot)
    {
        okAction();
        return;
    }

    // Change difficulty in the level selection menu.
    if(state == States::LevelSelection)
    {
        ++diffMultIdx;
        difficultyBumpEffect = difficultyBumpEffectMax;
        playSoundOverride("difficultyMultUp.ogg");
        touchDelay = 50.f;
        return;
    }

    // If there is no valid action abort.
    if(!isInMenu() || !getCurrentMenu()->getItem().canIncrease())
    {
        return;
    }

    getCurrentMenu()->increase();
    playSoundOverride("beep.ogg");
    touchDelay = 50.f;
}

inline constexpr int maxProfilesOnScreen{6};

void MenuGame::upAction()
{
    if(state == States::LevelSelection)
    {
        // Do not do anything until the pack change animation is over.
        if(packChangeState != PackChange::Rest)
        {
            return;
        }

        // When focus is held do an instant pack change.
        if(focusHeld)
        {
            changePackQuick(-1);
            return;
        }

        const int prevIdx{lvlDrawer->currentIndex - 1};

        // If there is only one pack behave differently.
        if(getSelectablePackInfosSize() == 1)
        {
            // If we are at the top of the pack go to its end instead
            // and scroll the menu to show it.
            if(prevIdx < 0)
            {
                setIndex(lvlDrawer->levelDataIds->size() - 1);
                calcScrollSpeed();

                const float scroll{
                    packLabelHeight +
                    levelLabelHeight * (lvlDrawer->currentIndex + 1) +
                    2.f * slctFrameSize};
                if(scroll > h - lvlDrawer->YOffset)
                {
                    lvlDrawer->YScrollTo = h - scroll;
                }
            }
            else
            {
                setIndex(prevIdx);
                calcLevelChangeScroll(-2);
            }
        }
        else if(prevIdx < 0)
        {
            // -2 means "go to previous pack and
            // skip to the last level of the list"
            changePackAction(-2);
        }
        else
        {
            setIndex(prevIdx);
            calcLevelChangeScroll(-2);
        }

        // Reset the scroll values of the text items that are related
        // to level specific fields.
        resetLevelNamesScrolls();
        playSoundOverride("beep.ogg");
        touchDelay = 50.f;
        return;
    }

    // In the loading screen scroll through the error messages.
    if(state == States::LoadingScreen)
    {
        if(scrollbarOffset != 0)
        {
            --scrollbarOffset;
            playSoundOverride("beep.ogg");
            touchDelay = 50.f;
        }
        return;
    }

    if(!isInMenu())
    {
        return;
    }

    // Scroll the profiles drawn on screen
    if((state == States::SLPSelect || state == States::SLPSelectBoot) &&
        getCurrentMenu()->getIdx() - 1 < scrollbarOffset)
    {
        const int index = ssvu::getMod(getCurrentMenu()->getIdx() - 1, 0,
            static_cast<int>(getCurrentMenu()->getItems().size()));
        scrollbarOffset = std::max(index - (maxProfilesOnScreen - 1), 0);
    }

    if(getCurrentMenu() != nullptr && !anyItemEnabled(*getCurrentMenu()))
    {
        return;
    }

    // Go to the first enabled menu item.
    do
    {
        getCurrentMenu()->previous();
    }
    while(!getCurrentMenu()->getItem().isEnabled());

    playSoundOverride("beep.ogg");
    touchDelay = 50.f;
}

inline constexpr int maxErrorsOnScreen{7};

void MenuGame::downAction()
{
    if(state == States::LevelSelection)
    {
        if(packChangeState != PackChange::Rest)
        {
            return;
        }

        if(focusHeld)
        {
            changePackQuick(1);
            return;
        }

        const int nextIdx{lvlDrawer->currentIndex + 1};
        if(getSelectablePackInfosSize() == 1)
        {
            if(nextIdx > ssvu::toInt(lvlDrawer->levelDataIds->size() - 1))
            {
                // Skip to the first level label.
                setIndex(0);
                calcScrollSpeed();
                lvlDrawer->YScrollTo = 0.f;
            }
            else
            {
                setIndex(nextIdx);
                calcLevelChangeScroll(2);
            }
        }
        else if(nextIdx > ssvu::toInt(lvlDrawer->levelDataIds->size() - 1))
        {
            // Go to the next pack.
            changePackAction(1);
        }
        else
        {
            setIndex(nextIdx);
            calcLevelChangeScroll(2);
        }

        resetLevelNamesScrolls();
        playSoundOverride("beep.ogg");
        touchDelay = 50.f;
        return;
    }

    if(state == States::LoadingScreen)
    {
        if(scrollbarOffset <
            static_cast<int>(loadInfo.errorMessages.size()) - maxErrorsOnScreen)
        {
            ++scrollbarOffset;
            playSoundOverride("beep.ogg");
            touchDelay = 50.f;
        }
        return;
    }

    if(!isInMenu())
    {
        return;
    }

    if((state == States::SLPSelect || state == States::SLPSelectBoot) &&
        getCurrentMenu()->getIdx() + 1 >
            maxProfilesOnScreen - 1 + scrollbarOffset)
    {
        const int index = ssvu::getMod(getCurrentMenu()->getIdx() + 1, 0,
            static_cast<int>(getCurrentMenu()->getItems().size()));
        scrollbarOffset = std::max(index - (maxProfilesOnScreen - 1), 0);
    }

    if(getCurrentMenu() != nullptr && !anyItemEnabled(*getCurrentMenu()))
    {
        return;
    }

    do
    {
        getCurrentMenu()->next();
    }
    while(!getCurrentMenu()->getItem().isEnabled());

    playSoundOverride("beep.ogg");
    touchDelay = 50.f;
}

void MenuGame::changePackTo(const int idx)
{
    const auto& p{assets.getSelectablePackInfos()};

    // Deduce the new packIdx.
    lvlSlct.packIdx = ssvu::getMod(idx, 0, static_cast<int>(p.size()));

    // Load level ids relative to the new pack
    lvlSlct.levelDataIds =
        &assets.getLevelIdsByPack(p.at(lvlDrawer->packIdx).id);

    // Set the correct level index.
    setIndex(0);

    // Reset all text scrolling.
    resetNamesScrolls();
}

void MenuGame::changePack()
{
    const auto& p{assets.getSelectablePackInfos()};

    // Deduce the new packIdx.
    lvlSlct.packIdx =
        ssvu::getMod(lvlDrawer->packIdx + (packChangeDirection > 0 ? 1 : -1), 0,
            static_cast<int>(p.size()));

    // Load level ids relative to the new pack
    lvlSlct.levelDataIds =
        &assets.getLevelIdsByPack(p.at(lvlDrawer->packIdx).id);

    // Set the correct level index.
    setIndex(packChangeDirection == -2 ? lvlSlct.levelDataIds->size() - 1 : 0);

    // Reset all text scrolling.
    resetNamesScrolls();
}

void MenuGame::changePackQuick(const int direction)
{
    if(isFavoriteLevels())
    {
        return;
    }

    packChangeDirection = direction;
    playSoundOverride("beep.ogg");
    changePack();
    adjustLevelsOffset();

    // YOffset is 0 when the first pack is shown and gets lower
    // the further down we have to scroll.

    // Height of the top of the pack label that is one index before the current
    // one.
    float scroll{packLabelHeight * (lvlDrawer->packIdx - 1)};

    // If the height is lower than the offset of the level selection we must
    // change to that to show the labels before the current one.
    if(scroll < -lvlDrawer->YOffset)
    {
        lvlDrawer->YScrollTo = lvlDrawer->YOffset = -scroll;
        return;
    }

    // Height of the bottom of the pack label that is one index after the
    // current one.
    scroll =
        packLabelHeight * std::min(lvlDrawer->packIdx + 2,
                              static_cast<int>(getSelectablePackInfosSize())) +
        levelLabelHeight + 3.f * slctFrameSize;

    // If the height is outside the boundaries of the screen adjust offset to
    // show it.
    if(scroll > h - lvlDrawer->YOffset)
    {
        lvlDrawer->YScrollTo = lvlDrawer->YOffset = h - scroll;
    }
}

void MenuGame::changePackAction(const int direction)
{
    if(state != States::LevelSelection || getSelectablePackInfosSize() == 1 ||
        packChangeState != PackChange::Rest)
    {
        return;
    }

    lvlDrawer->YScrollTo = lvlDrawer->YOffset; // stop scrolling for safety
    // Initiate the pack change animation.
    packChangeState = PackChange::Folding;
    packChangeDirection = direction;
    calcScrollSpeed();

    touchDelay = 50.f;
    playSoundOverride("beep.ogg");
}

void MenuGame::okAction()
{
    touchDelay = 50.f;

    switch(state)
    {
        case States::ETLPNewBoot: [[fallthrough]];
        case States::ETLPNew:
        {
            if(!enteredStr.empty())
            {
                ssvms::Category& profiles(
                    profileSelectionMenu.getCategoryByName(
                        "profile selection"));

                // Abort if user is trying to create a profile
                // with a name already in use
                for(auto& i : profiles.getItems())
                {
                    if(enteredStr == i->getName())
                    {
                        playSoundOverride("error.ogg");
                        showDialogBox(
                            "A PROFILE WITH THE SAME NAME ALREADY EXISTS\n"
                            "PLEASE ENTER ANOTHER NAME\n\n"
                            "PRESS ANY KEY OR BUTTON TO CLOSE THIS "
                            "MESSAGE\n");
                        setIgnoreAllInputs(2);
                        return;
                    }
                }

                // All good
                assets.pCreate(enteredStr);
                assets.pSetCurrent(enteredStr);
                changeFavoriteLevelsToProfile();

                // Create new menu item
                profiles.create<ssvms::Items::Single>(enteredStr,
                    [this, name = enteredStr]
                    {
                        assets.pSetCurrent(name);
                        changeFavoriteLevelsToProfile();
                    });

                profiles.sortByName();

                enteredStr = "";
                if(state == States::ETLPNewBoot)
                {
                    playSoundOverride("openHexagon.ogg");
                    changeStateTo(States::SMain);
                    return;
                }
                changeStateTo(States::SMain);
            }

            break;
        }

        case States::SLPSelectBoot:
        {
            playSoundOverride("openHexagon.ogg");
            getCurrentMenu()->exec();
            changeStateTo(States::SMain);
            return;
        }

        case States::SMain:
        {
            const std::string& category{
                getCurrentMenu()->getCategory().getName()};
            getCurrentMenu()->exec();

            // Going into the level selection set the selected level
            // label offset to the maximum value and set the other
            // offsets to 0.
            if(state == States::LevelSelection)
            {
                setIndex(lvlDrawer->currentIndex);
                adjustLevelsOffset();
                return;
            }

            if(getCurrentMenu() == nullptr)
            {
                return;
            }

            // Scroll to a menu item that is enabled
            if(scrollToEnabledMenuItem(getCurrentMenu()) == false)
            {
                return;
            }

            // Adjust the indents if we moved to a new submenu
            if(getCurrentMenu()->getCategory().getName() != category)
            {
                adjustMenuOffset(true);
            }

            break;
        }

        case States::MOpts:
        {
            getCurrentMenu()->exec();

            // There are two Bind controllers: KeyboardBindControl and
            // JoystickBindControl. So we cast to the common base class to not
            // check for one and the other.
            const auto* const bc{
                dynamic_cast<BindControlBase*>(&getCurrentMenu()->getItem())};
            if(bc != nullptr && bc->isWaitingForBind())
            {
                setIgnoreAllInputs(2);
                touchDelay = 10.f;
                playSoundOverride("beep.ogg");
                return;
            }

            // Scroll to a menu item that is enabled
            if(scrollToEnabledMenuItem(getCurrentMenu()) == false)
            {
                return;
            }

            break;
        }

        case States::MOnline:
        {
            getCurrentMenu()->exec();

            // Scroll to a menu item that is enabled
            if(scrollToEnabledMenuItem(getCurrentMenu()) == false)
            {
                return;
            }

            break;
        }

        case States::LevelSelection:
        {
            // Reset the scroll of the text fields so that
            // they will be 0 when user exit the level.
            resetNamesScrolls();
            playSelectedLevel();

            break;
        }

        default:
        {
            if(isInMenu())
            {
                getCurrentMenu()->exec();
            }

            break;
        }
    }

    playSoundOverride("select.ogg");
}

void MenuGame::playSelectedLevel()
{
    if(fnHGNewGame)
    {
        setMouseCursorVisible(false);

        fnHGNewGame(                                              //
            getNthSelectablePackInfo(lvlDrawer->packIdx).id,      //
            lvlDrawer->levelDataIds->at(lvlDrawer->currentIndex), //
            true /* firstPlay */,                                 //
            levelData->getNthDiffMult(diffMultIdx),               //
            false /* executeLastReplay */                         //
        );
    }
}

void MenuGame::eraseAction()
{
    if(isEnteringText() && !enteredStr.empty())
    {
        enteredStr.erase(enteredStr.end() - 1);
        playSoundOverride("beep.ogg");
    }
    else if(state == States::SLPSelect)
    {
        const std::string name{
            profileSelectionMenu.getCategory().getItem().getName()};

        // There must be at least one profile, don't erase profile
        // currently in use.
        if(profileSelectionMenu.getCategory().getItems().size() <= 1)
        {
            playSoundOverride("error.ogg");
            showDialogBox(
                "YOU CANNOT ERASE THE ONLY REMAINING PROFILE\n\n"
                "PRESS ANY KEY OR BUTTON TO CLOSE THIS MESSAGE\n");
            setIgnoreAllInputs(2);
            return;
        }
        if(assets.pGetName() == name)
        {
            playSoundOverride("error.ogg");
            showDialogBox(
                "YOU CANNOT ERASE THE CURRENTLY IN USE PROFILE\n\n"
                "PRESS ANY KEY OR BUTTON TO CLOSE THIS MESSAGE\n");
            setIgnoreAllInputs(2);
            return;
        }

        // Remove the profile .json
        const std::string fileName{"Profiles/" + name + ".json"};
        if(std::remove(fileName.c_str()) != 0)
        {
            ssvu::lo("eraseAction()")
                << "Error: file " << fileName << " does not exist\n";

            return;
        }
        else
        {
            // Remove profile from memory as well
            assets.pRemove(name);
        }

        // Remove the item from the menu
        profileSelectionMenu.getCategory().remove();
        playSoundOverride("beep.ogg");
    }
    else if(state == States::MOpts && isInMenu())
    {
        // Do not do anything if it's not a bind setter menu item.
        auto* const bc{
            dynamic_cast<BindControlBase*>(&getCurrentMenu()->getItem())};

        if(bc == nullptr)
        {
            return;
        }

        if(bc->erase())
        {
            playSoundOverride("beep.ogg");
        }

        touchDelay = 10.f;
    }
}

void MenuGame::exitAction()
{
    if(isInMenu() && getCurrentMenu()->getCategory().getName() == "main")
    {
        return;
    }

    playSoundOverride("beep.ogg");

    if(state == States::SLPSelectBoot)
    {
        changeStateTo(States::ETLPNewBoot);
        return;
    }

    if(state == States::LevelSelection)
    {
        changeStateTo(States::SMain);
        resetNamesScrolls();
        adjustMenuOffset(false);
        return;
    }

    if(assets.pIsValidLocalProfile())
    {
        if(isInMenu())
        {
            if(getCurrentMenu()->canGoBack())
            {
                getCurrentMenu()->goBack();
            }
            else
            {
                // If the current menu does not have a
                // super menu it means we must go back to
                // the main menu.
                changeStateTo(States::SMain);
            }
            adjustMenuOffset(false);
        }
        else if(isEnteringText())
        {
            changeStateTo(States::SMain);
        }
    }
}

//*****************************************************
//
// UPDATE
//
//*****************************************************

void MenuGame::update(ssvu::FT mFT)
{
    hexagonClient.update();

    const auto showHCEventDialogBox = [this](const bool error,
                                          const std::string& msg,
                                          const std::string& err = "")
    {
        if(!dialogBox.empty())
        {
            return;
        }

        if(state != States::SMain && state != States::MOnline &&
            state != States::MOpts && state != States::SLPSelect &&
            state != States::LevelSelection)
        {
            return;
        }

        if(error)
        {
            playSoundOverride("error.ogg");
        }
        else
        {
            playSoundOverride("select.ogg");
        }

        strBuf.clear();

        strBuf += msg;
        if(!err.empty())
        {
            strBuf += "\n\n";
            strBuf += err;
        }

        strBuf += "\n";

        // Prevent dialog box from being closed immediately:
        dialogBoxDelay = 16.f;

        showDialogBox(strBuf);
        setIgnoreAllInputs(1);

        SSVOH_ASSERT(!dialogBox.empty());
        SSVOH_ASSERT(ignoreInputs == 1);
    };

    std::optional<HexagonClient::Event> hcEvent;
    while((hcEvent = hexagonClient.pollEvent()).has_value())
    {
        Utils::match(
            *hcEvent, //

            [&](const HexagonClient::EConnectionSuccess&)
            { showHCEventDialogBox(false /* error */, "CONNECTION SUCCESS"); },

            [&](const HexagonClient::EConnectionFailure& e) {
                showHCEventDialogBox(
                    true /* error */, "CONNECTION FAILURE", e.error);
            },

            [&](const HexagonClient::EKicked&) {
                showHCEventDialogBox(
                    true /* error */, "DISCONNECTED FROM SERVER");
            },

            [&](const HexagonClient::ERegistrationSuccess&) {
                showHCEventDialogBox(false /* error */, "REGISTRATION SUCCESS");
            },

            [&](const HexagonClient::ERegistrationFailure& e) {
                showHCEventDialogBox(
                    true /* error */, "REGISTRATION FAILURE", e.error);
            },

            [&](const HexagonClient::ELoginSuccess&)
            {
                showHCEventDialogBox(false /* error */, "LOGIN SUCCESS");
                steamManager.unlock_achievement("a23_login");
            },

            [&](const HexagonClient::ELoginFailure& e) {
                showHCEventDialogBox(
                    true /* error */, "LOGIN FAILURE", e.error);
            },

            [&](const HexagonClient::ELogoutSuccess&)
            { showHCEventDialogBox(false /* error */, "LOGOUT SUCCESS"); },

            [&](const HexagonClient::ELogoutFailure&)
            { showHCEventDialogBox(true /* error */, "LOGOUT FAILURE"); },

            [&](const HexagonClient::EDeleteAccountSuccess&) {
                showHCEventDialogBox(
                    false /* error */, "DELETE ACCOUNT SUCCESS");
            },

            [&](const HexagonClient::EDeleteAccountFailure& e) {
                showHCEventDialogBox(
                    true /* error */, "DELETE ACCOUNT FAILURE", e.error);
            },

            [&](const HexagonClient::EReceivedTopScores& e)
            { leaderboardCache->receivedScores(e.levelValidator, e.scores); },

            [&](const HexagonClient::EReceivedOwnScore& e)
            { leaderboardCache->receivedOwnScore(e.levelValidator, e.score); },

            [&](const HexagonClient::EVersionMismatch&) {
                showHCEventDialogBox(
                    true /* error */, "CLIENT/SERVER VERSION MISMATCH");
            }

            //
        );
    }

    if(fnHGUpdateRichPresenceCallbacks)
    {
        fnHGUpdateRichPresenceCallbacks();
    }

    Joystick::update(Config::getJoystickDeadzone());

    // Focus should have no effect if we are in the favorites menu
    // or a pack change animation is in progress.
    if(state == States::LevelSelection && !isFavoriteLevels() &&
        packChangeState == PackChange::Rest)
    {
        if(!focusHeld)
        {
            focusHeld = Joystick::pressed(Joystick::Jid::Focus);
        }

        // If focus was not pressed it means we have initiated a quick
        // pack switch.
        if(focusHeld && !wasFocusHeld)
        {
            if(lvlDrawer->currentIndex != 0)
            {
                setIndex(0);
            }
            quickPackFoldStretch();
        }
        else if(!focusHeld && wasFocusHeld)
        {
            // focus is now released we have to stretch the level list of the
            // current menu.
            quickPackFoldStretch();
        }
        wasFocusHeld = focusHeld;
    }
    else
    {
        wasFocusHeld = focusHeld = false;
    }

    // TODO (P2): cleanup mouse control
    if((state == States::SMain || state == States::MOpts ||
           state == States::MOnline || state == States::SLPSelect) &&
        mustUseMenuItem.has_value())
    {
        if(getCurrentMenu() != nullptr)
        {
            auto& items = getCurrentMenu()->getItems();
            if(static_cast<int>(items.size()) > *mustUseMenuItem)
            {
                playSoundOverride("beep.ogg");
                items.at(*mustUseMenuItem)->exec();
            }
        }

        mustUseMenuItem.reset();
    }

    // TODO (P2): cleanup mouse control
    if(state == States::LevelSelection && packChangeState == PackChange::Rest)
    {
        if(mustFavorite)
        {
            addRemoveFavoriteLevel();
            mustFavorite = false;
        }
        else if(mustPlay)
        {
            playSelectedLevel();
            mustPlay = false;
        }
        else if(mustChangeIndexTo.has_value())
        {
            if(lvlDrawer != nullptr &&
                lvlDrawer->currentIndex != *mustChangeIndexTo)
            {
                playSoundOverride("beep.ogg");
                setIndex(*mustChangeIndexTo);
            }

            mustChangeIndexTo.reset();
        }
        else if(mustChangePackIndexTo.has_value())
        {
            if(lvlSlct.packIdx != *mustChangePackIndexTo)
            {
                playSoundOverride("beep.ogg");
                changePackTo(*mustChangePackIndexTo);
            }

            mustChangePackIndexTo.reset();
        }
    }

    if(Joystick::risingEdge(Joystick::Jid::NextPack))
    {
        changePackAction(1);
    }
    else if(Joystick::risingEdge(Joystick::Jid::PreviousPack))
    {
        changePackAction(-1);
    }

    if(Joystick::risingEdge(Joystick::Jid::AddToFavorites))
    {
        addRemoveFavoriteLevel();
    }

    if(Joystick::risingEdge(Joystick::Jid::FavoritesMenu))
    {
        switchToFromFavoriteLevels();
    }

    if(Joystick::risingEdge(Joystick::Jdir::Left))
    {
        leftAction();
    }
    else if(Joystick::risingEdge(Joystick::Jdir::Right))
    {
        rightAction();
    }
    else if(Joystick::risingEdge(Joystick::Jdir::Up))
    {
        upAction();
    }
    else if(Joystick::risingEdge(Joystick::Jdir::Down))
    {
        downAction();
    }

    if(Joystick::risingEdge(Joystick::Jid::Select))
    {
        okAction();
    }
    else if(Joystick::risingEdge(Joystick::Jid::Exit))
    {
        exitAction();
    }

    if(Joystick::risingEdge(Joystick::Jid::Screenshot))
    {
        mustTakeScreenshot = true;
    }

    if(touchDelay > 0.f)
    {
        touchDelay -= mFT;
    }

    if(dialogBoxDelay > 0.f)
    {
        dialogBoxDelay -= mFT;
    }

    if(difficultyBumpEffect > 0.f)
    {
        difficultyBumpEffect -= mFT * 2.f;
    }

    if(window.getFingerDownCount() == 1)
    {
        auto wThird{getWindowWidth() / 3.f};
        auto wLT{getWindowWidth() - wThird};
        auto hThird{getWindowHeight() / 3.f};
        auto hLT{getWindowHeight() - hThird};

        for(const auto& p : window.getFingerDownPositions())
        {
            if(p.y > hThird && p.y < hLT)
            {
                if(p.x > 0.f && p.x < wThird)
                {
                    leftAction();
                }
                else if(p.x < ssvu::toNum<int>(getWindowWidth()) && p.x > wLT)
                {
                    rightAction();
                }
                else if(p.x > wThird && p.x < wLT)
                {
                    okAction();
                }
            }
            else
            {
                if(p.y < hThird)
                {
                    upAction();
                }
                else if(p.y > hLT)
                {
                    downAction();
                }
            }
        }
    }

    overlayCamera.update(mFT);
    backgroundCamera.update(mFT);

    if(getCurrentMenu() != nullptr)
    {
        getCurrentMenu()->update();
    }

    currentCreditsId += mFT;
    creditsBar2.setTexture(assets.getTexture(
        ssvu::getByModIdx(creditsIds, ssvu::toInt(currentCreditsId / 100))));

    if(exitTimer > 20)
    {
        window.stop();
    }

    styleData.update(mFT);
    backgroundCamera.turn(levelStatus.rotationSpeed * 10.f);

    if(isEnteringText())
    {
        constexpr unsigned int limit{18u};
        for(auto& c : enteredChars)
        {
            if(enteredStr.size() < limit &&
                (ssvu::isAlphanumeric(c) || ssvu::isPunctuation(c)))
            {
                playSoundOverride("beep.ogg");
                enteredStr.append(ssvu::toStr(c));
            }
        }
    }
    enteredChars.clear();

    switch(state)
    {
        case States::LoadingScreen: hexagonRotation += mFT / 100.f; break;

        case States::LevelSelection:
        {
            // Folding animation of the level list when we change pack.
            switch(packChangeState)
            {
                case PackChange::Rest: scrollLevelListToTargetY(mFT); break;

                case PackChange::Folding:
                {
                    const float listHeight{getLevelListHeight()};
                    // Change the offset of the level list of the current pack
                    // to fold.
                    packChangeOffset = std::min(
                        packChangeOffset + mFT * scrollSpeed, listHeight);
                    // If needed scroll the level list offset to show the
                    // current level.
                    calcPackChangeScrollFold(listHeight);
                    if(packChangeOffset < listHeight)
                    {
                        break;
                    }

                    // Change the pack
                    changePack();

                    // Set the stretch info
                    packChangeOffset = getLevelListHeight();
                    calcScrollSpeed();
                    packChangeState = PackChange::Stretching;
                }
                break;

                case PackChange::Stretching:
                    // Change the offset of the level list of the current pack
                    // to stretch.
                    packChangeOffset =
                        std::max(packChangeOffset - mFT * scrollSpeed, 0.f);
                    // If needed scroll the level list offset to show the
                    // current level.
                    calcPackChangeScrollStretch(getLevelListHeight());
                    if(packChangeOffset > 0.f)
                    {
                        break;
                    }

                    packChangeOffset = 0.f;
                    adjustLevelsOffset();
                    packChangeState = PackChange::Rest;
                    break;
            }

            // Make sure there isn't empty space above the first
            // element of the level list.
            lvlDrawer->YOffset = std::min(lvlDrawer->YOffset, 0.f);
        }
        break;

        default: break;
    }
}

void MenuGame::setIndex(const int mIdx)
{
    lvlDrawer->currentIndex = mIdx;

    const std::string levelID{
        lvlDrawer->levelDataIds->at(lvlDrawer->currentIndex)};

    levelData = &assets.getLevelData(levelID);
    currentPack = &assets.getPackData(levelData->packId);

    formatLevelDescription();

    styleData = assets.getStyleData(levelData->packId, levelData->styleId);
    styleData.computeColors();

    // If we are in the favorite menu we must find the packId relative
    // to the selected level.
    if(isFavoriteLevels())
    {
        isLevelFavorite = true;

        const auto& p{assets.getSelectablePackInfos()};

        for(int i{0}; i < static_cast<int>(p.size()); ++i)
        {
            if(levelData->packId == p.at(i).id)
            {
                lvlDrawer->packIdx = i;
                break;
            }
        }
    }
    else if(!firstLevelSelection)
    {
        // setIndex() is called for the first time at a point
        // of the assets loading process where this call
        // causes a crash, so it can only safely occur when
        // the level selection menu is opened.
        isLevelFavorite =
            assets.getCurrentLocalProfile().isLevelFavorite(levelID);
    }

    // Set the colors of the menus
    auto& colors{styleData.getColors()};
    menuQuadColor = Config::getBlackAndWhite() ? sf::Color(20, 20, 20, 255) : styleData.getTextColor();
    if(ssvu::toInt(menuQuadColor.a) == 0 && !Config::getBlackAndWhite())
    {
        for(auto& c : colors)
        {
            if(ssvu::toInt(c.a) != 0)
            {
                menuQuadColor = c;
                break;
            }
        }
    }
    menuTextColor = Config::getBlackAndWhite() ? sf::Color::White : colors[0];

    dialogBoxTextColor = menuQuadColor;
    dialogBoxTextColor.a = 255;

    // If there is only one color set the remaining colors
    // to be alpha variants of the ones we have already set.
    if(colors.size() == 1)
    {
        menuTextColor.a = 255;
        menuSelectionColor = menuQuadColor;
        menuSelectionColor.a = 75;
    }
    else
    {
        // If the alpha is 0 or the color is the same find another one.
        if(ssvu::toInt(menuTextColor.a) == 0 || menuTextColor == menuQuadColor)
        {
            for(auto& c : colors)
            {
                if(ssvu::toInt(c.a) != 0 && c != menuQuadColor && !Config::getBlackAndWhite())
                {
                    menuTextColor = c;
                    break;
                }
            }
        }

        // Same as above.
        menuSelectionColor = Config::getBlackAndWhite() ? sf::Color::White : colors[1];
        if(ssvu::toInt(menuSelectionColor.a) == 0 ||
            menuSelectionColor == menuQuadColor ||
            menuSelectionColor == menuTextColor)
        {
            for(auto& c : colors)
            {
                if(ssvu::toInt(c.a) != 0 && c != menuQuadColor &&
                    c != menuTextColor && !Config::getBlackAndWhite())
                {
                    menuSelectionColor = c;
                    break;
                }
            }
        }
        menuSelectionColor.a = 175;
    }

    // Set color of the fonts.
    txtSelectionBig.font.setFillColor(menuQuadColor);
    txtSelectionSmall.font.setFillColor(menuTextColor);
    txtSelectionRanked.font.setFillColor(menuTextColor);
    txtInstructionsSmall.font.setFillColor(menuTextColor);
    txtSelectionScore.font.setFillColor(menuTextColor);

    // Set gameplay values
    diffMultIdx = 0;
    for(; levelData->difficultyMults.at(diffMultIdx) != 1.f; ++diffMultIdx)
    {}

    try
    {
        Utils::runLuaFile(lua, levelData->luaScriptPath);
        Utils::runLuaFunctionIfExists<void>(lua, "onInit");
        Utils::runLuaFunctionIfExists<void>(lua, "onLoad");
    }
    catch(std::runtime_error& mError)
    {
        std::cout << "[MenuGame::init] Runtime Lua error on menu "
                     "(loadFile/onInit/onLoad) with level \""
                  << levelData->name << "\": \n"
                  << mError.what() << '\n'
                  << std::endl;

        if(!Config::getDebug())
        {
            playSoundOverride("error.ogg");
        }
    }
    catch(...)
    {
        std::cout << "[MenuGame::init] Unknown runtime Lua error on menu "
                     "(loadFile/onInit/onLoad) with level \""
                  << levelData->name << "\"\n"
                  << std::endl;

        if(!Config::getDebug())
        {
            playSoundOverride("error.ogg");
        }
    }
}

void MenuGame::reloadAssets(const bool reloadEntirePack)
{
    if(state != States::LevelSelection || !dialogBox.empty() ||
        !Config::getDebug())
    {
        return;
    }

    // Do the necessary asset reload operation and get the log
    // of the results.
    std::string reloadOutput;
    if(reloadEntirePack)
    {
        reloadOutput =
            assets.reloadPack(levelData->packId, levelData->packPath);
    }
    else
    {
        reloadOutput = assets.reloadLevel(
            levelData->packId, levelData->packPath, levelData->id);
    }

    setIndex(lvlDrawer->currentIndex); // loads the new levelData

    reloadOutput += "\nPRESS ANY KEY OR BUTTON TO CLOSE THIS MESSAGE\n";
    Utils::uppercasify(reloadOutput);

    // Needs to be two because the dialog box reacts to key releases.
    // First key release is the one of the key press that made the dialog
    // box pop up, the second one belongs to the key press that closes it
    playSoundOverride("select.ogg");
    showDialogBox(reloadOutput);
    setIgnoreAllInputs(2);
}

void MenuGame::refreshCamera()
{
    const float fw{1024.f / getWindowWidth()};
    const float fh{768.f / getWindowHeight()};
    const float fmax{std::max(fw, fh)};

    w = getWindowWidth() * fmax;
    h = getWindowHeight() * fmax;

    backgroundCamera.setView(
        {ssvs::zeroVec2f, {Config::getSizeX() * Config::getZoomFactor(),
                              Config::getSizeY() * Config::getZoomFactor()}});

    overlayCamera.setView(sf::View{sf::FloatRect(0, 0, w, h)});

    titleBar.setOrigin(ssvs::zeroVec2f);
    titleBar.setScale({0.5f, 0.5f});
    titleBar.setPosition({20.f, 20.f});

    txtVersion.font.setString(GAME_VERSION_STR);
    txtVersion.font.setOrigin({ssvs::getLocalRight(txtVersion.font), 0.f});
    txtVersion.font.setPosition({ssvs::getGlobalRight(titleBar) - 15.f,
        ssvs::getGlobalTop(titleBar) + 15.f});

    creditsBar1.setOrigin({ssvs::getLocalWidth(creditsBar1), 0.f});
    creditsBar1.setScale({0.373f, 0.373f});
    creditsBar1.setPosition({w - 20.f, 20.f});

    creditsBar2.setOrigin({ssvs::getLocalWidth(creditsBar2), 0});
    creditsBar2.setScale({0.373f, 0.373f});
    creditsBar2.setPosition(
        {w - 20.f, 17.f + ssvs::getGlobalBottom(creditsBar1)});

    const float scaleFactor{w / 1024.f};
    epilepsyWarning.setOrigin(ssvs::getLocalCenter(epilepsyWarning));
    epilepsyWarning.setPosition({1024 / (2.f / scaleFactor), 768 / 2.f - 50});
    epilepsyWarning.setScale({0.36f, 0.36f});

    // Readjust the menu background skew and the indents
    fourByThree = 10.f * getWindowWidth() / getWindowHeight() < 16;

    if(fourByThree)
    {
        backgroundCamera.setSkew({1.f, 0.8f});
    }
    else
    {
        backgroundCamera.setSkew({1.f, 0.6f});
    }

    for(auto& c : mainMenu.getCategories())
    {
        c->getOffset() = 0.f;
    }

    for(auto& c : welcomeMenu.getCategories())
    {
        c->getOffset() = 0.f;
    }

    for(auto& c : optionsMenu.getCategories())
    {
        c->getOffset() = 0.f;
    }

    for(auto& c : onlineMenu.getCategories())
    {
        c->getOffset() = 0.f;
    }

    for(auto& c : profileSelectionMenu.getCategories())
    {
        c->getOffset() = 0.f;
    }

    // Update the height infos of the fonts.
    if(fourByThree)
    {
        txtMenuBig.font.setCharacterSize(26);
        txtMenuSmall.font.setCharacterSize(16);
        txtSelectionSmall.font.setCharacterSize(16);
        txtSelectionRanked.font.setCharacterSize(12);

        txtLoadBig.font.setCharacterSize(56);
        txtLoadSmall.font.setCharacterSize(16);
        txtRandomTip.font.setCharacterSize(24);
    }
    else
    {
        txtMenuBig.font.setCharacterSize(36);
        txtMenuSmall.font.setCharacterSize(24);
        txtSelectionSmall.font.setCharacterSize(16);
        txtSelectionRanked.font.setCharacterSize(12);

        txtLoadBig.font.setCharacterSize(70);
        txtLoadSmall.font.setCharacterSize(24);
        txtRandomTip.font.setCharacterSize(32);
    }

    // txtVersion and txtProfile are not in here cause they do not need it.
    for(auto f : {&txtProf, &txtLoadBig, &txtLoadSmall, &txtMenuBig,
            &txtMenuSmall, &txtInstructionsBig, &txtRandomTip,
            &txtInstructionsMedium, &txtInstructionsSmall, &txtEnteringText,
            &txtSelectionBig, &txtSelectionMedium, &txtSelectionSmall,
            &txtSelectionScore, &txtSelectionRanked})
    {
        f->updateHeight();
    }

    // Readjust the level selection drawing parameters
    updateLevelSelectionDrawingParameters();

    // Reformat the level description, but not on boot.
    // Otherwise the game crashes.
    if(!firstLevelSelection)
    {
        formatLevelDescription();
    }

    overlayCamera.update(0.5f);
    backgroundCamera.update(0.5f);
}
void MenuGame::renderText(
    const std::string& mStr, sf::Text& mText, const sf::Vector2f& mPos)
{
    mText.setString(mStr);
    mText.setPosition(mPos);
    render(mText);
}

void MenuGame::renderText(const std::string& mStr, sf::Text& mText,
    const sf::Vector2f& mPos, const sf::Color& mColor)
{
    const sf::Color prevColor = mText.getFillColor();
    mText.setFillColor(mColor);
    renderText(mStr, mText, mPos);
    mText.setFillColor(prevColor);
}

void MenuGame::renderText(const std::string& mStr, sf::Text& mText,
    const unsigned int mSize, const sf::Vector2f& mPos)
{
    mText.setCharacterSize(mSize);
    renderText(mStr, mText, mPos);
}

void MenuGame::renderText(const std::string& mStr, sf::Text& mText,
    const unsigned int mSize, const sf::Vector2f& mPos, const sf::Color& mColor)
{
    const auto prevSize = mText.getCharacterSize();
    mText.setCharacterSize(mSize);
    const sf::Color prevColor = mText.getFillColor();
    mText.setFillColor(mColor);
    renderText(mStr, mText, mPos);
    mText.setFillColor(prevColor);
    mText.setCharacterSize(prevSize);
}

// Text rendering centered
void MenuGame::renderTextCentered(
    const std::string& mStr, sf::Text& mText, const sf::Vector2f& mPos)
{
    mText.setString(mStr);
    mText.setPosition(mPos.x - ssvs::getGlobalHalfWidth(mText), mPos.y);
    render(mText);
}

void MenuGame::renderTextCentered(const std::string& mStr, sf::Text& mText,
    const sf::Vector2f& mPos, const sf::Color& mColor)
{
    const sf::Color prevColor = mText.getFillColor();
    mText.setFillColor(mColor);
    renderTextCentered(mStr, mText, mPos);
    mText.setFillColor(prevColor);
}

void MenuGame::renderTextCentered(const std::string& mStr, sf::Text& mText,
    const unsigned int mSize, const sf::Vector2f& mPos)
{
    mText.setCharacterSize(mSize);
    renderTextCentered(mStr, mText, mPos);
}

void MenuGame::renderTextCentered(const std::string& mStr, sf::Text& mText,
    const unsigned int mSize, const sf::Vector2f& mPos, const sf::Color& mColor)
{
    mText.setCharacterSize(mSize);
    const sf::Color prevColor = mText.getFillColor();
    mText.setFillColor(mColor);
    renderTextCentered(mStr, mText, mPos);
    mText.setFillColor(prevColor);
}

// Text rendering centered with an offset
void MenuGame::renderTextCenteredOffset(const std::string& mStr,
    sf::Text& mText, const sf::Vector2f& mPos, const float xOffset)
{
    mText.setString(mStr);
    mText.setPosition(
        xOffset + mPos.x - ssvs::getGlobalHalfWidth(mText), mPos.y);
    render(mText);
}

void MenuGame::renderTextCenteredOffset(const std::string& mStr,
    sf::Text& mText, const sf::Vector2f& mPos, const float xOffset,
    const sf::Color& mColor)
{
    const sf::Color prevColor = mText.getFillColor();
    mText.setFillColor(mColor);
    renderTextCenteredOffset(mStr, mText, mPos, xOffset);
    mText.setFillColor(prevColor);
}

[[nodiscard]] float MenuGame::getWindowWidth() const noexcept
{
    return window.getRenderWindow().getSize().x;
}

[[nodiscard]] float MenuGame::getWindowHeight() const noexcept
{
    return window.getRenderWindow().getSize().y;
}

[[nodiscard]] ssvs::GameState& MenuGame::getGame() noexcept
{
    return game;
}

void MenuGame::returnToLevelSelection()
{
    adjustLevelsOffset();
    lvlDrawer->XOffset = 0.f;
    setIgnoreAllInputs(1); // otherwise you go back to the main menu
}


void MenuGame::refreshBinds()
{
    // Keyboard-mouse
    for(std::size_t i{0u}; i < Config::triggerGetters.size(); ++i)
    {
        game.refreshTrigger(Config::triggerGetters[i](), i);

        if(fnHGTriggerRefresh)
        {
            fnHGTriggerRefresh(Config::triggerGetters[i](), i);
        }
    }

    // Joystick
    Config::loadAllJoystickBinds();
}

void MenuGame::setIgnoreAllInputs(const unsigned int presses)
{
    ignoreInputs = presses;

    if(ignoreInputs == 0)
    {
        game.ignoreAllInputs(false);
        Joystick::ignoreAllPresses(false);
        return;
    }

    game.ignoreAllInputs(true);
    Joystick::ignoreAllPresses(true);
}

//*****************************************************
//
// DRAWING
//
//*****************************************************

void MenuGame::adjustMenuOffset(const bool resetMenuOffset)
{
    if(getCurrentMenu() == nullptr)
    {
        return;
    }

    // Set to 0 the offset of the whole submenu.
    if(resetMenuOffset)
    {
        getCurrentMenu()->getCategory().getOffset() = 0.f;
    }

    // Set to 0 the offset of all elements except the one that is
    // currently selected.
    const auto& items{getCurrentMenu()->getItems()};
    for(auto& i : items)
    {
        i->getOffset() = 0.f;
    }

    items[getCurrentMenu()->getIdx()]->getOffset() = maxOffset;
}

void MenuGame::adjustLevelsOffset()
{
    // Set all level offsets to 0 except the currently selected one.
    for(auto& offset : lvlSlct.lvlOffsets)
    {
        offset = 0.f;
    }

    lvlSlct.lvlOffsets[lvlSlct.currentIndex] = maxOffset;

    // Do the same for the favorites menu but only if there are levels in it.
    if(!favSlct.lvlOffsets.size())
    {
        return;
    }

    for(auto& offset : favSlct.lvlOffsets)
    {
        offset = 0.f;
    }

    favSlct.lvlOffsets[favSlct.currentIndex] = maxOffset;
}

inline constexpr float offsetSpeed{4.f};
inline constexpr float offsetSnap{0.25f};

[[nodiscard]] float MenuGame::calcMenuOffset(float& offset,
    const float maxOffset, const bool revertOffset, const bool speedUp)
{
    // Adjust the offset of the menu depending on wherever it
    // is being opened or closed.

    float speed;
    if(revertOffset)
    {
        // FPS consistent offset speed
        speed = (speedUp ? 12.f : 8.f) * offsetSpeed * getFPSMult();

        offset = std::max(offset - speed, 0.f);
        if(offset <= offsetSnap)
        {
            offset = 0.f;
        }
    }
    else if(offset < maxOffset)
    {
        speed = (speedUp ? 12.f : 8.f) * offsetSpeed * getFPSMult();
        offset += speed * (1.f - offset / maxOffset);
        if(offset >= maxOffset - offsetSnap)
        {
            offset = maxOffset;
        }
    }

    return maxOffset - offset;
}

void MenuGame::calcMenuItemOffset(float& offset, bool selected)
{
    // Same as above but for menu items that are selected and deselected.
    float speed;
    if(selected)
    {
        if(offset < maxOffset)
        {
            speed = offsetSpeed * getFPSMult();
            offset += speed * (1.f - offset / maxOffset);
            if(offset >= maxOffset - offsetSnap)
            {
                offset = maxOffset;
            }
        }
    }
    else if(offset > 0.f)
    {
        speed = offsetSpeed * getFPSMult();
        offset -= 1.5f * speed * offset / maxOffset;
        if(offset <= offsetSnap)
        {
            offset = 0.f;
        }
    }
}

void MenuGame::createQuad(const sf::Color& color, const float x1,
    const float x2, const float y1, const float y2)
{
    sf::Vector2f topLeft{x1, y1}, topRight{x2, y1}, bottomRight{x2, y2},
        bottomLeft{x1, y2};
    menuQuads.batch_unsafe_emplace_back(
        color, topLeft, topRight, bottomRight, bottomLeft);
}

void MenuGame::createQuad(
    const sf::Color& color, const sf::Vector2f& mins, const sf::Vector2f& maxs)
{
    createQuad(color, mins.x, maxs.x, mins.y, maxs.y);
}

void MenuGame::createQuadTrapezoid(const sf::Color& color, const float x1,
    const float x2, const float x3, const float y1, const float y2,
    const bool left)
{
    sf::Vector2f topLeft, topRight, bottomRight, bottomLeft;
    if(left)
    {
        topLeft = {x1, y1};
        topRight = {x2, y1};
        bottomRight = {x3, y2};
        bottomLeft = {x1, y2};
    }
    else
    {
        topLeft = {x1, y1};
        topRight = {x2, y1};
        bottomRight = {x2, y2};
        bottomLeft = {x3, y2};
    }
    menuQuads.batch_unsafe_emplace_back(
        color, topLeft, topRight, bottomRight, bottomLeft);
}

[[nodiscard]] std::pair<int, int> MenuGame::getScrollbarNotches(
    const int size, const int maxSize) const
{
    if(size > maxSize)
    {
        return {size - maxSize, maxSize};
    }

    return {0, size};
}

void MenuGame::drawScrollbar(const float totalHeight, const int size,
    const int notches, const float x, const float y, const sf::Color& color)
{
    // Draw a scrollbar depending on the total amount of elements
    // and the total height of the portion of screen where the scrollbar
    // is drawn.

    const float notchHeight{totalHeight / size},
        barHeight{totalHeight - notches * notchHeight},
        startHeight{y + notchHeight * scrollbarOffset};

    menuQuads.clear();
    menuQuads.reserve(4);
    createQuad(
        color, x, x + textToQuadBorder, startHeight, startHeight + barHeight);
    render(menuQuads);
}

void MenuGame::drawMainSubmenus(
    const std::vector<std::unique_ptr<ssvms::Category>>& subMenus,
    const float indent)
{
    bool currentlySelected, hasOffset;
    for(auto& c : subMenus)
    {
        currentlySelected = mainMenu.getCategory().getName() == c->getName();
        hasOffset = c->getOffset() != 0.f;

        // this submenu has been fully folded so there is no need to draw it.
        if(!currentlySelected && !hasOffset)
        {
            continue;
        }

        drawMainMenu(*c, w - indent, !currentlySelected && hasOffset);
    }
}

void MenuGame::drawSubmenusSmall(
    const std::vector<std::unique_ptr<ssvms::Category>>& subMenus,
    const float indent)
{
    bool currentlySelected, hasOffset;
    for(auto& c : subMenus)
    {
        // Already drawn before this loop
        if(c->getName() == "options")
        {
            continue;
        }

        currentlySelected = optionsMenu.getCategory().getName() == c->getName();
        hasOffset = c->getOffset() != 0.f;

        // this submenu has been fully folded
        if(!currentlySelected && !hasOffset)
        {
            continue;
        }

        drawOptionsSubmenus(*c, indent, !currentlySelected && hasOffset);
    }
}

// What is this font * fontHeightOffset thing about?
// The font used has some empty space on top that causes text to be drawn
// below what you would expect according to the chosen coordinates.
// Therefore, if we assume the text has to be drawn at 'Y' height the
// actual value to be sent to the renderer is 'Y - fontHeight *
// fontHeightOffset.
inline constexpr float fontHeightOffset{0.55f};
inline constexpr float frameSizeMulti{0.6f};

void MenuGame::setMouseCursorVisible(const bool x)
{
    window.setMouseCursorVisible(x);
    mouseCursorVisible = x;
}

[[nodiscard]] bool MenuGame::isMouseCursorVisible() const
{
    return mouseCursorVisible;
}

[[nodiscard]] bool MenuGame::overlayMouseOverlap(
    const sf::Vector2f& mins, const sf::Vector2f& maxs) const
{
    constexpr float tolerance = 1.f;

    if(!isMouseCursorVisible() || !window.hasFocus())
    {
        return false;
    }

    const sf::Vector2f mp = overlayCamera.getMousePosition();

    return mp.x > mins.x - tolerance && mp.x < maxs.x + tolerance &&
           mp.y > mins.y - tolerance && mp.y < maxs.y + tolerance;
}

[[nodiscard]] bool MenuGame::overlayMouseOverlapAndUpdateHover(
    const sf::Vector2f& mins, const sf::Vector2f& maxs)
{
    if(overlayMouseOverlap(mins, maxs))
    {
        mouseHovering = true;
        return true;
    }

    return false;
}

[[nodiscard]] sf::Color MenuGame::mouseOverlapColor(
    const bool mouseOverlap, const sf::Color& c) const
{
    if(!mouseOverlap)
    {
        return c;
    }

    return sf::Color{
        static_cast<sf::Uint8>(255 - c.r), //
        static_cast<sf::Uint8>(255 - c.g), //
        static_cast<sf::Uint8>(255 - c.b)  //
    };
}

[[nodiscard]] bool MenuGame::mouseLeftRisingEdge() const
{
    return !mouseWasPressed && mousePressed;
}

void MenuGame::drawMainMenu(
    ssvms::Category& mSubMenu, float baseIndent, const bool revertOffset)
{
    const auto& items(mSubMenu.getItems());
    const int size = items.size();

    // Global menu offset
    const float panelOffset{
        calcMenuOffset(mSubMenu.getOffset(), -(baseIndent - w), revertOffset)};
    baseIndent += panelOffset;

    // Calculate quads coordinates.
    // We use font height as our reference parameter.
    const float interline{3.f * txtMenuBig.height},
        quadBorder{txtMenuBig.height * frameSizeMulti},
        doubleBorder{2.f * quadBorder},
        totalHeight{interline * (size - 1) + doubleBorder + txtMenuBig.height};
    float quadHeight{(h - totalHeight) / 2.f + interline - quadBorder},
        txtHeight{
            quadHeight - txtMenuBig.height * fontHeightOffset + quadBorder},
        indent;

    // Store info needed to draw the submenus
    menuHalfHeight = quadHeight + totalHeight / 2.f;

    // Draw the quads that surround the text
    menuQuads.clear();
    menuQuads.reserve(4 * size);

    static std::vector<bool> mouseOverlaps;
    mouseOverlaps.resize(size);

    for(int i = 0; i < size; ++i)
    {
        calcMenuItemOffset(items[i]->getOffset(), i == mSubMenu.getIdx());
        indent = baseIndent - items[i]->getOffset();

        const sf::Vector2f bodyMins{
            indent - txtMenuBig.height * 2.5f, quadHeight};

        const sf::Vector2f bodyMaxs{
            w, quadHeight + doubleBorder + txtMenuBig.height};

        const bool mouseOverlap =
            overlayMouseOverlapAndUpdateHover(bodyMins, bodyMaxs);

        mouseOverlaps[i] = mouseOverlap;

        sf::Color c = !items[i]->isEnabled() ? sf::Color{110, 110, 110, 255}
                                             : menuQuadColor;

        createQuadTrapezoid(mouseOverlapColor(mouseOverlap, c),
            indent - txtMenuBig.height * 2.5f, w,
            indent - txtMenuBig.height / 2.f, quadHeight,
            quadHeight + doubleBorder + txtMenuBig.height, false);

        // TODO (P2): cleanup mouse control
        if(mouseOverlap && !mustUseMenuItem.has_value() &&
            mouseLeftRisingEdge())
        {
            mustUseMenuItem = i;
        }

        quadHeight += interline;
    }

    render(menuQuads);

    // Draw the text on top of the quads
    for(int i = 0; i < size; ++i)
    {
        indent = baseIndent - items[i]->getOffset();

        const sf::Color c = mouseOverlapColor(mouseOverlaps.at(i),
            !items[i]->isEnabled() ? sf::Color{150, 150, 150, 255}
                                   : menuTextColor);

        renderText(
            items[i]->getName(), txtMenuBig.font, {indent, txtHeight}, c);

        txtHeight += interline;
    }
}

void MenuGame::drawOptionsSubmenus(
    ssvms::Category& mSubMenu, const float baseIndent, const bool revertOffset)
{
    const auto& items{mSubMenu.getItems()};
    const int size(items.size());

    // Calculate quads coordinates
    float quadBorder{txtMenuSmall.height * frameSizeMulti};
    const float doubleBorder{quadBorder * 2.f};
    const float interline{2.1f * txtMenuSmall.height};
    const float totalHeight{
        interline * (size - 1) + 2.f * doubleBorder + txtMenuSmall.height};
    const float quadHeight{std::max(menuHalfHeight - totalHeight / 2.f,
        ssvs::getGlobalBottom(creditsBar2) + 10.f)};

    // Offset
    const float panelOffset{
        calcMenuOffset(mSubMenu.getOffset(), baseIndent, revertOffset, true)};
    const float indent{baseIndent - quadBorder - panelOffset};

    // Draw the quads that surround the text
    menuQuads.clear();
    menuQuads.reserve(8);

    createQuad(menuTextColor, 0, indent + doubleBorder, quadHeight,
        quadHeight + totalHeight);
    createQuad(menuQuadColor, 0, indent + quadBorder, quadHeight + quadBorder,
        quadHeight + totalHeight - quadBorder);
    render(menuQuads);

    // Draw the text on top of the quads
    quadBorder = quadBorder * 1.5f - panelOffset;
    std::string itemName;
    float txtHeight{
        quadHeight - txtMenuSmall.height * fontHeightOffset + doubleBorder};
    for(int i{0}; i < size; ++i)
    {
        SSVOH_ASSERT(i < static_cast<int>(items.size()));
        itemName = items[i]->getName();

        Utils::uppercasify(itemName);
        if(i == mSubMenu.getIdx())
        {
            itemName = "> " + itemName;
        }

        renderText(itemName, txtMenuSmall.font, {quadBorder, txtHeight},
            !items[i]->isEnabled() ? sf::Color{150, 150, 150, 255}
                                   : menuTextColor);

        txtHeight += interline;
    }
}

std::string MenuGame::formatSurvivalTime(ProfileData* data)
{
    int time{0};
    for(auto& s : data->getScores())
    {
        time += s.second;
    }

    std::stringstream stream;
    stream << "Total survival time ";
    if(time < 60)
    {
        stream << std::setfill('0') << std::setw(2) << time;
    }
    else if(time < 3600)
    {
        stream << std::setfill('0') << std::setw(2) << time / 60 << ":"
               << std::setfill('0') << std::setw(2) << time % 60;
    }
    else
    {
        stream << time / 3600 << ":";
        time %= 3600;
        stream << std::setfill('0') << std::setw(2) << time / 60 << ":"
               << std::setfill('0') << std::setw(2) << time % 60;
    }

    return stream.str();
}

inline constexpr float profFrameSize{10.f};
inline constexpr unsigned int profCharSize{30};
inline constexpr unsigned int profSelectedCharSize{30 + 12};

void MenuGame::drawProfileSelection(
    const float xOffset, const bool revertOffset)
{
    ssvms::Category& mSubmenu{profileSelectionMenu.getCategory()};
    const auto& items{mSubmenu.getItems()};

    const int realSize(items.size());
    auto [scrollbarNotches, drawnSize] =
        getScrollbarNotches(realSize, maxProfilesOnScreen);

    // Calculate the height
    const float fontHeight{Utils::getFontHeight(txtProfile.font, profCharSize)},
        selectedFontHeight{
            Utils::getFontHeight(txtProfile.font, profSelectedCharSize)};

    // check if the width of the menu should be increased
    constexpr float profMinWidth{400.f};
    float textWidth{profMinWidth};
    std::string itemName;
    for(auto& p : items)
    {
        itemName = p->getName();
        Utils::uppercasify(itemName);
        txtProfile.font.setString(itemName);
        textWidth = std::max(textWidth, ssvs::getGlobalWidth(txtProfile.font));
    }

    // Calculate horizontal coordinates
    constexpr float profMinHeight{360.f};
    const float interline{4.f * fontHeight}, doubleBorder{profFrameSize * 2.f},
        totalHeight{std::max(
            interline * (drawnSize - 1) + doubleBorder * 2.f + fontHeight * 3.f,
            profMinHeight)};

    // always account for the scrollbar space
    constexpr float scrollbarInterspace{3.f};
    textWidth += doubleBorder + scrollbarInterspace * 2.f;

    // Make sure the box does not go out of bounds
    float indent{((w + xOffset) * 0.5f - textWidth) / 2.f + profFrameSize};
    indent = std::max(indent, doubleBorder);

    // Make sure the instructions do not go out of bounds
    txtInstructionsSmall.font.setString(
        "Press backspace to delete the selected profile\n"
        "You cannot delete the profile currently in use");
    const float instructionsWidth{
        ssvs::getGlobalWidth(txtInstructionsSmall.font)},
        resultIndent{indent + (textWidth - instructionsWidth) / 2.f};
    if(resultIndent < 0.f)
    {
        indent += -resultIndent + 10.f;
    }

    // Calculate vertical coordinates
    float quadHeight{std::max(
        (h - totalHeight) / 2.f, ssvs::getGlobalBottom(titleBar) + 60.f)},
        txtHeight{quadHeight - fontHeight * fontHeightOffset + doubleBorder +
                  profFrameSize * 0.5f};

    // Submenu global offset
    const float panelOffset{
        calcMenuOffset(mSubmenu.getOffset(), h - quadHeight, revertOffset)};
    txtHeight += panelOffset;
    quadHeight += panelOffset;

    // Draw the quads that surround the text and the scroll bar if needed
    menuQuads.clear();
    menuQuads.reserve(8);

    createQuad(menuTextColor, indent - doubleBorder,
        indent + doubleBorder + textWidth, quadHeight,
        quadHeight + totalHeight);

    createQuad(menuQuadColor, indent - profFrameSize,
        indent + profFrameSize + textWidth, quadHeight + profFrameSize,
        quadHeight + totalHeight - profFrameSize);

    render(menuQuads);

    if(scrollbarNotches != 0)
    {
        drawScrollbar(totalHeight - 2.f * (profFrameSize + scrollbarInterspace),
            realSize, scrollbarNotches,
            indent + textWidth - scrollbarInterspace,
            quadHeight + profFrameSize + scrollbarInterspace, menuTextColor);
    }

    // Draw the text on top of the quads
    float yPos;
    bool selected;
    ProfileData* data;
    txtHeight += profFrameSize / 2.f;
    for(int i{scrollbarOffset}; i < drawnSize + scrollbarOffset; ++i)
    {
        selected = i == mSubmenu.getIdx();

        // Draw profile name
        SSVOH_ASSERT(i < static_cast<int>(items.size()));
        itemName = items[i]->getName();

        yPos = txtHeight - (selected ? fontHeight * 0.75f : 0.f);
        renderTextCentered(Utils::toUppercase(itemName), txtProfile.font,
            selected ? profSelectedCharSize : profCharSize,
            {indent + textWidth / 2.f, yPos}, menuTextColor);

        // Add total survival time for extra flavor
        data = assets.getLocalProfileByName(itemName);
        if(data != nullptr)
        {
            yPos += (selected ? selectedFontHeight : fontHeight) * 1.75f;
            txtProfile.font.setCharacterSize(
                txtProfile.font.getCharacterSize() - 15);
            renderTextCentered(formatSurvivalTime(data), txtProfile.font,
                {indent + textWidth / 2.f, yPos});
        }

        txtHeight += interline;
    }

    // Add message about profile deletion
    txtInstructionsSmall.font.setPosition(
        {indent + (textWidth - instructionsWidth) / 2.f,
            quadHeight + totalHeight});
    render(txtInstructionsSmall.font);
}

void MenuGame::drawProfileSelectionBoot()
{
    ssvms::Category& mSubmenu{profileSelectionMenu.getCategory()};
    const auto& items(mSubmenu.getItems());

    const int realSize(items.size());
    auto [scrollbarNotches, drawnSize] =
        getScrollbarNotches(realSize, maxProfilesOnScreen);

    const float fontHeight{Utils::getFontHeight(txtProfile.font, profCharSize)},
        selectedFontHeight{
            Utils::getFontHeight(txtProfile.font, profSelectedCharSize)};

    // Calculate coordinates
    const float interline{4.f * fontHeight}, totalHeight{interline * drawnSize};
    float height{(h - totalHeight) / 2.f - selectedFontHeight * 1.5f};

    // Draw instructions
    const float instructionsHeight{1.5f * txtInstructionsBig.height};
    // Make sure the instructions do not overlap the title bar or the credits
    height = std::max(height - 2.f * instructionsHeight,
        ssvs::getGlobalBottom(titleBar) + 40.f);

    const std::string instructions[] = {
        "SELECT LOCAL PROFILE", "PRESS ESC TO CREATE A NEW PROFILE"};
    for(auto& s : instructions)
    {
        renderTextCentered(s, txtInstructionsBig.font, {w / 2.f, height});
        height += instructionsHeight;
    }
    height += selectedFontHeight;

    // Draw scrollbar if needed
    std::string itemName;
    if(scrollbarNotches != 0)
    {
        float width = 0.f;
        for(auto& p : items)
        {
            itemName = p->getName();
            Utils::uppercasify(itemName);
            txtProfile.font.setString(itemName);
            width = std::max(width, ssvs::getGlobalWidth(txtProfile.font));
        }

        txtProfile.font.setString("Total survival time 0000:00");
        width = std::max(width, ssvs::getGlobalWidth(txtProfile.font));
        width += 10.f;

        drawScrollbar(totalHeight, realSize, scrollbarNotches,
            (w + width) / 2.f, height, sf::Color::White);
    }

    // Draw profile names and score
    bool selected;
    float yPos;
    ProfileData* data;
    for(int i{scrollbarOffset}; i < drawnSize + scrollbarOffset; ++i)
    {
        selected = i == mSubmenu.getIdx();

        // Draw profile name
        SSVOH_ASSERT(i < static_cast<int>(items.size()));
        itemName = items[i]->getName();

        yPos = height - (selected ? fontHeight * 0.75f : 0.f);
        renderTextCentered(Utils::toUppercase(itemName), txtProfile.font,
            selected ? profSelectedCharSize : profCharSize, {w / 2.f, yPos});

        // Add total survival time for extra flavor
        data = assets.getLocalProfileByName(itemName);
        if(data != nullptr)
        {
            yPos += (selected ? selectedFontHeight : fontHeight) * 1.75f;
            txtProfile.font.setCharacterSize(
                txtProfile.font.getCharacterSize() - 15);
            renderTextCentered(
                formatSurvivalTime(data), txtProfile.font, {w / 2.f, yPos});
        }

        height += interline;
    }
}

void MenuGame::drawEnteringText(const float xOffset, const bool revertOffset)
{
    // Set text parameters
    Utils::uppercasify(enteredStr);
    txtEnteringText.font.setString(enteredStr);
    constexpr float enteringTextMinWidth{200.f};
    const float textWidth{std::max(
        enteringTextMinWidth, ssvs::getGlobalWidth(txtEnteringText.font))};

    // Calculate coordinates
    const float doubleFrame{profFrameSize * 2.f},
        indent{((w + xOffset) * 0.5f - textWidth) / 2.f + profFrameSize},
        txtBottom{txtEnteringText.height * 0.45f},
        totalHeight{txtEnteringText.height + txtBottom + doubleFrame * 2.f};
    float quadHeight{menuHalfHeight - totalHeight / 2.f},
        txtHeight{quadHeight - txtEnteringText.height * fontHeightOffset +
                  doubleFrame};

    // Offset
    const float panelOffset{
        calcMenuOffset(enteringTextOffset, h - quadHeight, revertOffset)};
    txtHeight += panelOffset;
    quadHeight += panelOffset;

    // Draw the quads that surround the text
    menuQuads.clear();
    menuQuads.reserve(8);

    createQuad(menuTextColor, indent - doubleFrame,
        indent + doubleFrame + textWidth, quadHeight, quadHeight + totalHeight);

    createQuad(menuQuadColor, indent - profFrameSize,
        indent + profFrameSize + textWidth, quadHeight + profFrameSize,
        quadHeight + totalHeight - profFrameSize);

    render(menuQuads);

    // Draw the text on top of the quads
    renderTextCenteredOffset(enteredStr, txtEnteringText.font,
        {textWidth / 2.f, txtHeight + profFrameSize / 2.f}, indent,
        menuTextColor);

    // Draw instructions text above the quads
    const std::string instructions[] = {
        "INSERT TEXT", "PRESS ENTER WHEN DONE", "PRESS ESC TO ABORT"};
    const float instructionsHeight{txtInstructionsMedium.height * 1.5f};
    txtHeight -= profFrameSize + instructionsHeight * 3.f;
    txtInstructionsMedium.font.setFillColor(menuQuadColor);
    for(auto& s : instructions)
    {
        renderTextCenteredOffset(s, txtInstructionsMedium.font,
            {textWidth / 2.f, txtHeight}, indent);
        txtHeight += instructionsHeight;
    }
}

void MenuGame::drawEnteringTextBoot()
{
    // Set parameters
    float height{(h - txtEnteringText.height) / 2};

    // Draw instructions text
    const float instructionsHeight{txtInstructionsBig.height * 1.5f};
    height -= instructionsHeight * 2.f;
    const std::string instructions[] = {
        "PROFILE CREATION", "PLEASE TYPE A NAME AND PRESS ENTER"};
    for(auto& s : instructions)
    {
        renderTextCentered(s, txtInstructionsBig.font, {w / 2.f, height});
        height += instructionsHeight;
    }

    // Draw the entered name text
    height += instructionsHeight / 2.f;
    Utils::uppercasify(enteredStr);
    renderTextCentered(
        enteredStr, txtEnteringText.font, {w / 2.f, height}, sf::Color::White);
}

void MenuGame::drawLoadResults()
{
    //--------------------------------------
    // Hexagon
    const float div{ssvu::tau / 6 * 0.5f}, hexagonRadius{100.f};
    const sf::Vector2f centerPos = {w / 2.f, h / 5.f};

    menuQuads.clear();

    int i;
    menuQuads.reserve(4 * 6);
    for(i = 0; i < 6; ++i)
    {
        const float sAngle{div * 2.f * (i + hexagonRotation)};

        const sf::Vector2f topLeft{
            ssvs::getOrbitRad(centerPos, sAngle - div, hexagonRadius)};
        const sf::Vector2f topRight{
            ssvs::getOrbitRad(centerPos, sAngle + div, hexagonRadius)};
        const sf::Vector2f bottomRight{
            ssvs::getOrbitRad(centerPos, sAngle + div, hexagonRadius + 10.f)};
        const sf::Vector2f bottomLeft{
            ssvs::getOrbitRad(centerPos, sAngle - div, hexagonRadius + 10.f)};

        menuQuads.batch_unsafe_emplace_back(
            sf::Color::White, topLeft, topRight, bottomRight, bottomLeft);
    }

    //--------------------------------------
    // Vertical separators

    menuQuads.reserve_more(4 * 3);
    const float xOffset{w / 4.f};
    float topHeight{h / 2.f - h / 15.f}, bottomHeight{h / 2.f + h / 15.f};

    for(i = -1; i < 2; ++i)
    {
        createQuad(sf::Color::White, w / 2.f + i * xOffset - 5.f,
            w / 2.f + i * xOffset + 5.f, topHeight, bottomHeight);
    }

    render(menuQuads);

    //--------------------------------------
    // Counters: text and numbers

    topHeight += 5.f - txtLoadSmall.height;
    const float numbersHeight = bottomHeight - txtLoadBig.height * 2.f;

    txtLoadSmall.font.setFillColor(sf::Color::White);
    txtLoadBig.font.setFillColor(sf::Color::White);

    // 1
    float textOffset{w - 3.f * xOffset};
    renderTextCentered(
        "PACKS LOADED", txtLoadSmall.font, {textOffset / 2.f, topHeight});
    renderTextCentered(ssvu::toStr(loadInfo.packs), txtLoadBig.font,
        {textOffset / 2.f, numbersHeight});

    // 2
    textOffset = w - xOffset;
    renderTextCentered(
        "LEVELS LOADED", txtLoadSmall.font, {textOffset / 2.f, topHeight});
    renderTextCentered(ssvu::toStr(loadInfo.levels), txtLoadBig.font,
        {textOffset / 2.f, numbersHeight});

    // 3
    textOffset = w + xOffset;
    renderTextCentered(
        "ASSETS LOADED", txtLoadSmall.font, {textOffset / 2.f, topHeight});
    renderTextCentered(ssvu::toStr(loadInfo.assets), txtLoadBig.font,
        {textOffset / 2.f, numbersHeight});

    //--------------------------------------
    // Random tip

    const float tipInterline{txtRandomTip.height * 1.5f};
    float height{h - tipInterline * 2.f};
    for(i = 1; i >= 0; --i) // all tips are on two lines
    {
        renderTextCentered(std::string(randomTip[i]), txtRandomTip.font,
            {w / 2.f, height - tipInterline});
        height -= tipInterline;
    }

    //--------------------------------------
    // Errors (if any)

    int size = loadInfo.errorMessages.size();

    textOffset = w + 3.f * xOffset;
    renderTextCentered("ERRORS", txtLoadSmall.font,
        {textOffset / 2.f, topHeight}, sf::Color::Red);
    renderTextCentered(ssvu::toStr(size), txtLoadBig.font,
        {textOffset / 2.f, numbersHeight}, sf::Color::Red);

    // No error messages
    if(!size)
    {
        bottomHeight += txtLoadSmall.height * 1.75f;
        renderTextCentered("NO LOAD ERRORS", txtLoadSmall.font,
            {w / 2.f, bottomHeight}, sf::Color::White);
        return;
    }

    // Print errors from last to first.
    bottomHeight += txtLoadSmall.height / 2.f;
    const float txtSpacing{txtLoadSmall.height * 2.f};

    // But first handle the scrollbar
    auto [scrollbarNotches, drawnSize] =
        getScrollbarNotches(size, maxErrorsOnScreen);
    if(scrollbarNotches != 0)
    {
        drawScrollbar(txtSpacing * drawnSize, size, scrollbarNotches,
            w - 4.f - textToQuadBorder, bottomHeight + txtSpacing / 4.f,
            sf::Color::Red);
    }

    for(i = drawnSize - 1 + scrollbarOffset; i > -1 + scrollbarOffset; --i)
    {
        renderTextCentered(loadInfo.errorMessages[i], txtLoadSmall.font,
            {w / 2.f, bottomHeight});
        bottomHeight += txtSpacing;
    }
}

void MenuGame::updateLevelSelectionDrawingParameters()
{
    levelDetailsOffset = 0.f;
    lvlSlct.XOffset = 0.f;
    favSlct.XOffset = 0.f;

    textToQuadBorder = txtSelectionMedium.height * frameSizeMulti;
    slctFrameSize = textToQuadBorder * 0.3f;

    packLabelHeight =
        txtSelectionMedium.height + 2.f * textToQuadBorder + slctFrameSize;

    levelLabelHeight = txtSelectionBig.height +   // level name
                       txtSelectionSmall.height + // level author
                       textToQuadBorder +         // interspace
                       2.f * textToQuadBorder -   // top and bottom space
                       slctFrameSize;             // the bottom border
}

float MenuGame::getLevelSelectionHeight() const
{
    return packLabelHeight * getSelectablePackInfosSize() +
           getLevelListHeight() - packChangeOffset +
           (lvlDrawer->packIdx !=
                       static_cast<int>(getSelectablePackInfosSize()) - 1
                   ? 2.f
                   : 1.f) *
               slctFrameSize;
}

void MenuGame::scrollName(std::string& text, float& scroller)
{
    // FPS consistent scrolling
    scroller += getFPSMult();
    text += "  ";

    auto it{std::next(text.begin(),
        ssvu::getMod(ssvu::toInt(scroller / 100.f), text.length()))};
    std::string charsToMove;
    std::move(text.begin(), it, std::back_inserter(charsToMove));
    text.erase(text.begin(), it);
    text += charsToMove;
}

void MenuGame::scrollNameRightBorder(std::string& text, const std::string key,
    sf::Text& font, float& scroller, float border)
{
    // Store length of the key
    font.setString(key);
    const float keyWidth = ssvs::getGlobalWidth(font);

    Utils::uppercasify(text);
    font.setString(text);

    // If the text is already within border format and return
    border -= keyWidth;
    if(ssvs::getGlobalWidth(font) <= border)
    {
        text = key + text;
        return;
    }

    // Scroll the name and shrink it to the required length
    scrollName(text, scroller);
    font.setString(text);
    while(ssvs::getGlobalWidth(font) > border && text.length() > 1)
    {
        text.pop_back();
        font.setString(text);
    }
    text = key + text;
}

void MenuGame::scrollNameRightBorder(
    std::string& text, sf::Text& font, float& scroller, const float border)
{
    Utils::uppercasify(text);
    font.setString(text);

    if(ssvs::getGlobalWidth(font) <= border)
    {
        return;
    }

    scrollName(text, scroller);
    font.setString(text);
    while(ssvs::getGlobalWidth(font) > border && text.length() > 1)
    {
        text.pop_back();
        font.setString(text);
    }
}


[[nodiscard]] float MenuGame::getLevelListHeight() const
{
    return levelLabelHeight *
               (focusHeld ? 1 : lvlDrawer->levelDataIds->size()) +
           slctFrameSize;
}

void MenuGame::calcScrollSpeed()
{
    // Only speed up the animation if there are more than 16 levels.
    scrollSpeed =
        baseScrollSpeed * std::max(lvlDrawer->levelDataIds->size() / 16.f, 1.f);
}

void MenuGame::calcLevelChangeScroll(const int dir)
{
    scrollSpeed = baseScrollSpeed;

    // When in the favorites menu the packIdx variable stores the index
    // of the pack the currently selected level belongs to. However
    // the favorite levels are displayed belonging to a single pack
    // so for the purposes of this function the pack index of the
    // favorites menu is always 0.
    const int actualPackIdx{isFavoriteLevels() ? 0 : lvlDrawer->packIdx};
    float scroll;

    if(dir < 0)
    {
        // If we are approaching the top of the pack show either the first
        // level label and the next pack label or two previous pack labels.
        if(lvlDrawer->currentIndex < 2)
        {
            scroll = packLabelHeight *
                     (actualPackIdx + 1 - (2 - lvlDrawer->currentIndex));
        }
        else
        {
            //...otherwise just show the two previous level labels.
            scroll = packLabelHeight * (actualPackIdx + 1) +
                     levelLabelHeight * (lvlDrawer->currentIndex + dir) +
                     slctFrameSize;
        }

        checkWindowTopScroll(scroll,
            [this](const float target) { lvlDrawer->YScrollTo = target; });
        return;
    }

    const int size{static_cast<int>(lvlDrawer->levelDataIds->size())};
    // If we are approaching the bottom of the pack show either the
    // last level label and the next pack label or two next pack labels...
    if(lvlDrawer->currentIndex >= size - 2 &&
        actualPackIdx != static_cast<int>(getSelectablePackInfosSize()) - 1)
    {
        scroll =
            packLabelHeight * (actualPackIdx + 1 +
                                  (2 - (size - 1 - lvlDrawer->currentIndex))) +
            levelLabelHeight * size + 3.f * slctFrameSize;
    }
    else
    {
        //...otherwise just show the two next level labels.
        scroll = packLabelHeight * (actualPackIdx + 1) +
                 levelLabelHeight *
                     std::min(lvlDrawer->currentIndex + dir + 1,
                         static_cast<int>(lvlDrawer->levelDataIds->size())) +
                 2.f * slctFrameSize;
    }

    checkWindowBottomScroll(
        scroll, [this](const float target) { lvlDrawer->YScrollTo = target; });
}

void MenuGame::calcPackChangeScrollFold(const float mLevelListHeight)
{
    if(packChangeDirection == -2)
    {
        return;
    }

    // Make sure the last level and the two before it fit on screen.
    const float scroll{packLabelHeight * lvlDrawer->packIdx + slctFrameSize +
                       std::max(0.f, packLabelHeight + slctFrameSize +
                                         mLevelListHeight - packChangeOffset)};

    // As soon as the bottom of the level list goes out of the screen
    // switch pack by setting the packChangeOffset to the height of the
    // level list.
    checkWindowTopScroll(scroll,
        [this, mLevelListHeight](const float target)
        {
            lvlDrawer->YScrollTo = lvlDrawer->YOffset = target;
            packChangeOffset = mLevelListHeight;
        });
}

void MenuGame::calcPackChangeScrollStretch(const float mLevelListHeight)
{
    float scrollTop, scrollBottom;
    if(packChangeDirection == -2)
    {
        // Scrolling for all packs except the last one of the list.
        if(lvlDrawer->packIdx !=
            static_cast<int>(getSelectablePackInfosSize()) - 1)
        {
            // Height of the top of the pack label of the current pack.
            scrollTop = packLabelHeight * lvlDrawer->packIdx;
            // Height of the bottom of the next pack label.
            scrollBottom = scrollTop + 2.f * (packLabelHeight + slctFrameSize) +
                           std::max(0.f, mLevelListHeight - packChangeOffset);

            if(scrollBottom > h - lvlDrawer->YOffset)
            {
                // With particularly long lists it only makes sense to show
                // the stretch animation for as long as we can see the pack
                // label on screen. After the pack label is outside of the draw
                // window we only see a standing still list since we no more
                // have a point of reference and the list is programmed to keep
                // scrollBottom inside the window. If this occurs cut the
                // animation short.
                if(scrollTop < -lvlDrawer->YOffset)
                {
                    packChangeOffset = 0.f;
                    scrollBottom = scrollTop +
                                   2.f * (packLabelHeight + slctFrameSize) +
                                   std::max(0.f, mLevelListHeight);
                }
                lvlDrawer->YScrollTo = lvlDrawer->YOffset = h - scrollBottom;
            }
            else if(scrollTop < -lvlDrawer->YOffset)
            {
                lvlDrawer->YScrollTo = lvlDrawer->YOffset = -scrollTop;
            }
            return;
        }

        // Bottom of the pack label.
        scrollTop =
            packLabelHeight * getSelectablePackInfosSize() + slctFrameSize;
        // Bottom of the level list.
        scrollBottom =
            scrollTop + std::max(0.f, mLevelListHeight - packChangeOffset);

        checkWindowBottomScroll(scrollBottom, [this](const float target)
            { lvlDrawer->YScrollTo = lvlDrawer->YOffset = target; });
        checkWindowTopScroll(scrollTop, [this](const float target)
            { lvlDrawer->YScrollTo = lvlDrawer->YOffset = target; });
        return;
    }

    // The list is shifted to try fit all levels in the pack.
    // If that is not possible just include the pack label
    // + whatever amount of levels it's possible to fit on screen.
    scrollTop = packLabelHeight * (lvlDrawer->packIdx - 1);
    scrollBottom = scrollTop + packLabelHeight +
                   std::min(packLabelHeight + slctFrameSize + mLevelListHeight -
                                packChangeOffset,
                       h);

    checkWindowBottomScroll(scrollBottom, [this](const float target)
        { lvlDrawer->YScrollTo = lvlDrawer->YOffset = target; });
    checkWindowTopScroll(scrollTop, [this](const float target)
        { lvlDrawer->YScrollTo = lvlDrawer->YOffset = target; });
}

void MenuGame::quickPackFoldStretch()
{
    // Top of the pack label of the previous pack.
    const float scrollTop{packLabelHeight * (lvlDrawer->packIdx - 1)};
    // Bottom of the pack label of the next pack.
    const float scrollBottom{scrollTop + 2.f * packLabelHeight + slctFrameSize +
                             getLevelListHeight()};

    checkWindowBottomScroll(scrollBottom, [this](const float target)
        { lvlDrawer->YScrollTo = lvlDrawer->YOffset = target; });
    checkWindowTopScroll(scrollTop, [this](const float target)
        { lvlDrawer->YScrollTo = lvlDrawer->YOffset = target; });
    adjustLevelsOffset();
}

void MenuGame::scrollLevelListToTargetY(ssvu::FT mFT)
{
    if(std::abs(lvlDrawer->YOffset - lvlDrawer->YScrollTo) <= Utils::epsilon)
    {
        return;
    }

    if(lvlDrawer->YOffset < lvlDrawer->YScrollTo)
    {
        lvlDrawer->YOffset += mFT * scrollSpeed;
        if(lvlDrawer->YOffset >= lvlDrawer->YScrollTo)
        {
            lvlDrawer->YOffset = lvlDrawer->YScrollTo;
        }
        return;
    }

    lvlDrawer->YOffset -= mFT * scrollSpeed;
    if(lvlDrawer->YOffset <= lvlDrawer->YScrollTo)
    {
        lvlDrawer->YOffset = lvlDrawer->YScrollTo;
    }
}

inline constexpr int descLines{5};

void MenuGame::checkWindowTopScroll(
    const float scroll, std::function<void(const float)> action)
{
    const float target{-scroll};
    if(target <= lvlDrawer->YOffset)
    {
        return;
    }

    action(target);
}

void MenuGame::checkWindowBottomScroll(
    const float scroll, std::function<void(const float)> action)
{
    const float target{h - scroll};
    if(target >= lvlDrawer->YOffset)
    {
        return;
    }

    action(target);
}

void MenuGame::resetNamesScrolls()
{
    for(int i = 0; i < static_cast<int>(Label::ScrollsSize); ++i)
    {
        namesScroll[i] = 0;
    }
}

void MenuGame::resetLevelNamesScrolls()
{
    // Reset all scrolls except the ones relative to the pack.
    namesScroll[static_cast<int>(Label::LevelName)] = 0.f;
    for(int i = static_cast<int>(Label::MusicName);
        i < static_cast<int>(Label::ScrollsSize); ++i)
    {
        namesScroll[i] = 0.f;
    }
}

[[nodiscard]] float MenuGame::getMaximumTextWidth() const
{
    return w * 0.33f - 2.f * textToQuadBorder;
}

void MenuGame::formatLevelDescription()
{
    levelDescription.clear();

    std::vector<std::string> words;

    {
        strBuf.clear();
        std::string& desc = strBuf;

        desc += assets
                    .getLevelData(
                        lvlDrawer->levelDataIds->at(lvlDrawer->currentIndex))
                    .description;

        if(desc.empty())
        {
            return;
        }

        Utils::uppercasify(desc);

        // Split description into words.
        desc += '\n'; // Add a safety newline
        std::size_t i{0};
        std::size_t j{0};
        for(; i < desc.size(); ++i)
        {
            if(desc[i] == '\n')
            {
                words.emplace_back(
                    desc.substr(j, i - j + 1)); // include newline.
                j = i + 1;
            }
            else if(desc[i] == ' ')
            {
                words.emplace_back(desc.substr(j, i - j));
                j = i + 1; // skip the space.
            }
        }
    }

    // Group words into lines depending on wherever
    // they fit within the maximum width.
    const float maxWidth{getMaximumTextWidth()};
    std::string candidate;
    std::string temp;
    for(std::size_t i = 0;
        i < words.size() && levelDescription.size() < descLines; ++i)
    {
        if(!candidate.empty())
        {
            temp = " " + words[i];
            txtSelectionSmall.font.setString(candidate + temp);
        }
        else
        {
            temp = words[i];
            txtSelectionSmall.font.setString(temp);
        }

        // If last character is a newline...
        if(!temp.empty() && temp[temp.size() - 1] == '\n')
        {
            // ...if it all fits add to the vector as a single line...
            if(ssvs::getGlobalWidth(txtSelectionSmall.font) < maxWidth)
            {
                candidate += temp;
                levelDescription.push_back(candidate);
            }
            else
            {
                // ...otherwise add "candidate" to the vector and add the new
                // word on its own.
                levelDescription.push_back(candidate);
                if(levelDescription.size() < descLines)
                {
                    levelDescription.push_back(words[i]);
                }
            }
            candidate.clear();
            continue;
        }

        // If there is no newline check if the line fits...
        if(ssvs::getGlobalWidth(txtSelectionSmall.font) < maxWidth)
        {
            candidate += temp;
            continue;
        }

        // ...if it doesn't add to the vector "candidate" and then set it to be
        // just the overflowing word, to be checked upon in the next cycles.
        levelDescription.push_back(candidate);
        candidate = words[i];
    }

    // Add whatever is left if it fits.
    if(levelDescription.size() < descLines)
    {
        levelDescription.push_back(candidate);
    }
}

void MenuGame::changeFavoriteLevelsToProfile()
{
    // Each profile has its own favorite levels.
    // Copy the `ProfileData` favorites into the menu's favorites vector.

    favoriteLevelDataIds.clear();

    for(const std::string& id :
        assets.getCurrentLocalProfile().getFavoriteLevelIds())
    {
        favoriteLevelDataIds.push_back(id);
    }

    std::sort(favoriteLevelDataIds.begin(), favoriteLevelDataIds.end(),
        [this](const std::string& a, const std::string& b) -> bool
        {
            return ssvu::toLower(assets.getLevelData(a).name) <
                   ssvu::toLower(assets.getLevelData(b).name);
        });

    const int sz{static_cast<int>(favoriteLevelDataIds.size())};

    // If the new current profile has no favorites force the level selection
    // to use the regular drawer.
    if(sz == 0)
    {
        lvlDrawer = &lvlSlct;
        return;
    }

    // Make sure the level index is within boundaries.
    ssvu::clamp(favSlct.currentIndex, 0, sz - 1);

    // Resize the level offset parameters.
    favSlct.lvlOffsets.resize(sz);
}


[[nodiscard]] bool MenuGame::isFavoriteLevels() const
{
    return lvlDrawer->isFavorites;
}

[[nodiscard]] std::size_t MenuGame::getSelectablePackInfosSize() const
{
    return isFavoriteLevels() ? 1 : assets.getSelectablePackInfos().size();
}

[[nodiscard]] const PackInfo& MenuGame::getNthSelectablePackInfo(
    const std::size_t i)
{
    return assets.getSelectablePackInfos().at(i);
}

void MenuGame::addRemoveFavoriteLevel()
{
    const LevelData& data{assets.getLevelData(
        lvlDrawer->levelDataIds->at(lvlDrawer->currentIndex))};

    const std::string levelID{data.packId + "_" + data.id};

    // Level is a favorite so remove it.
    if(isLevelFavorite)
    {
        assets.getCurrentLocalProfile().removeFavoriteLevel(levelID);
        favoriteLevelDataIds.erase(std::find(
            favoriteLevelDataIds.begin(), favoriteLevelDataIds.end(), levelID));
        favSlct.lvlOffsets.pop_back();

        // Make sure the index is within bounds.
        if(!favSlct.levelDataIds->empty())
        {
            ssvu::clamp(favSlct.currentIndex, 0,
                static_cast<int>(favSlct.levelDataIds->size()) - 1);
        }
        else
        {
            favSlct.currentIndex = 0;
        }

        if(isFavoriteLevels())
        {
            adjustLevelsOffset();
        }

        // If the favorite levels vector is empty
        // after the removal force exit from the
        // favorites menu.
        if(favSlct.levelDataIds->empty())
        {
            favSlct.XOffset = 0.f; // this way the menu is not drawn anymore.
            lvlDrawer = &lvlSlct;
        }
        else
        {
            // Make sure there is no empty space between the end of the list
            // and the bottom of the window. Needed to ensure the selected
            // level is always on screen.
            const float scroll{packLabelHeight + 2.f * slctFrameSize +
                               levelLabelHeight * favSlct.levelDataIds->size()};
            if(h > scroll + lvlDrawer->YOffset)
            {
                favSlct.YOffset = favSlct.YScrollTo = h - scroll;
            }
        }

        // Update the looks.
        setIndex(lvlDrawer->currentIndex);
    }
    else
    {
        assets.getCurrentLocalProfile().addFavoriteLevel(levelID);
        favSlct.lvlOffsets.emplace_back(0.f);
        isLevelFavorite = true;

        // Add the level to the favorites vector
        // keeping it sorted in alphabetical order.
        auto it{favoriteLevelDataIds.begin()};
        const auto end{favoriteLevelDataIds.end()};
        const std::string tweakedFavName{
            ssvu::toLower(assets.getLevelData(levelID).name)};

        std::string tweakedLevelName;
        while(it != end)
        {
            tweakedLevelName = ssvu::toLower(assets.getLevelData(*it).name);
            if(tweakedLevelName > tweakedFavName)
            {
                break;
            }

            ++it;
        }

        if(it == end)
        {
            favoriteLevelDataIds.emplace_back(levelID);
        }
        else
        {
            favoriteLevelDataIds.insert(it, levelID);
        }

        // Just in case.
        ssvu::clamp(favSlct.currentIndex, 0,
            static_cast<int>(favSlct.levelDataIds->size()) - 1);
    }

    playSoundOverride("select.ogg");
}

void MenuGame::switchToFromFavoriteLevels()
{
    if(state != States::LevelSelection || favSlct.levelDataIds->empty())
    {
        return;
    }

    // Quickly finish any ongoing pack changes.
    if(packChangeState == PackChange::Folding)
    {
        changePack();
    }

    packChangeState = PackChange::Rest;
    lvlDrawer->YScrollTo = lvlDrawer->YOffset;
    packChangeOffset = 0.f;

    lvlDrawer = isFavoriteLevels() ? &lvlSlct : &favSlct;
    setIndex(lvlDrawer->currentIndex); // update the looks
    adjustLevelsOffset();
    resetNamesScrolls();
    playSoundOverride("select.ogg");
}

void MenuGame::drawLevelSelectionRightSide(
    LevelDrawer& drawer, const bool revertOffset)
{
    // total distance from the top of the text
    // to the outer border of the label.
    const float outerFrame{textToQuadBorder + slctFrameSize};
    const float packLabelOffset{w * 0.33f - outerFrame};
    const float quadsIndent{w - packLabelOffset};
    const float txtIndent{w - packLabelOffset / 2.f};
    const float levelIndent{quadsIndent + outerFrame};
    const float panelOffset{
        calcMenuOffset(drawer.XOffset, w - quadsIndent, revertOffset)};

    const auto& infos{assets.getSelectablePackInfos()};
    int packsSize, levelsSize;
    if(drawer.isFavorites)
    {
        levelsSize = drawer.levelDataIds->size();
        packsSize = 1;
    }
    else
    {
        packsSize = infos.size();
        levelsSize = focusHeld ? 1 : drawer.levelDataIds->size();
    }

    static std::string tempString;
    float prevLevelIndent{0.f}, height{0.f};
    sf::Vector2f topLeft, topRight, bottomRight, bottomLeft;

    // The drawing order is: levels list then pack labels.
    // The reason for it is that when a pack is deselected the
    // level list slides up and it must do so below the previous pack labels.
    // Therefore pack labels must be drawn above everything else (aka must
    // be drawn last).

    topLeft = {w / 2.f, 2.f};
    tempString = isFavoriteLevels() ? "PRESS F2 TO SHOW ALL LEVELS"
                                    : "PRESS F2 TO SHOW FAVORITE LEVELS";
    renderTextCentered(tempString, txtSelectionSmall.font, topLeft);
    tempString = "\nHOLD FOCUS TO JUMP BETWEEN PACKS";
    renderTextCentered(tempString, txtSelectionSmall.font, topLeft);

    //----------------------------------------
    // LEVELS LIST

    int i;
    sf::Color alphaTextColor{
        menuTextColor.r, menuTextColor.g, menuTextColor.b, 150};
    txtSelectionMedium.font.setFillColor(menuTextColor);
    height = packLabelHeight * (isFavoriteLevels() ? 1 : drawer.packIdx + 1) +
             slctFrameSize - packChangeOffset + drawer.YOffset;

    for(i = 0; i < levelsSize; ++i)
    {
        //-------------------------------------
        // Quads
        menuQuads.clear();
        menuQuads.reserve(12);

        // If the list is folding give all level labels the same alignment
        if(packChangeState != PackChange::Rest)
        {
            drawer.lvlOffsets[i] = 0.f;
        }
        else
        {
            calcMenuItemOffset(drawer.lvlOffsets[i], i == drawer.currentIndex);
        }

        float indent = quadsIndent + panelOffset;
        if(!focusHeld)
        {
            indent -= drawer.lvlOffsets[i];
        }

        // Top frame
        if(i > 0 && drawer.lvlOffsets[i - 1] > drawer.lvlOffsets[i])
        {
            createQuad(menuQuadColor, prevLevelIndent, w, height,
                height + slctFrameSize);
        }
        else
        {
            createQuad(
                menuQuadColor, indent, w, height, height + slctFrameSize);
        }

        // Side frame
        createQuad(menuQuadColor, indent, indent + slctFrameSize,
            height + slctFrameSize, height + levelLabelHeight);

        // Body
        const sf::Vector2f bodyMins{
            indent + slctFrameSize, height + slctFrameSize};

        const sf::Vector2f bodyMaxs{w, height + levelLabelHeight};

        const bool mouseOverlap =
            overlayMouseOverlapAndUpdateHover(bodyMins, bodyMaxs);

        const sf::Color c =
            i == drawer.currentIndex ? menuSelectionColor : alphaTextColor;

        createQuad(mouseOverlapColor(mouseOverlap, c), bodyMins, bodyMaxs);

        // TODO (P2): cleanup mouse control
        if(mouseOverlap && mouseLeftRisingEdge())
        {
            if(!mustPlay &&
                Clock::now() - lastMouseClick < std::chrono::milliseconds(160))
            {
                mustPlay = true;
            }
            else if(!mustChangeIndexTo.has_value())
            {
                mustChangeIndexTo = i;
            }
        }

        render(menuQuads);
        prevLevelIndent = indent;

        //-------------------------------------
        // Level data
        const LevelData* const levelData{
            &assets.getLevelData(drawer.levelDataIds->at(i))};
        if(levelData == nullptr)
        {
            continue;
        }

        //-------------------------------------
        // Level name

        indent = levelIndent + panelOffset;
        if(!focusHeld)
        {
            indent -= drawer.lvlOffsets[i];
        }
        height += textToQuadBorder;

        tempString = focusHeld ? "..." : levelData->name;
        Utils::uppercasify(tempString);

        const sf::Color c0 = mouseOverlapColor(mouseOverlap, menuQuadColor);

        const auto currentDiffMult = levelData->getNthDiffMult(diffMultIdx);
        const std::string& levelValidator =
            levelData->getValidator(currentDiffMult);

        if(!levelData->unscored &&
            hexagonClient.isLevelSupportedByServer(levelValidator))
        {
            const float padding = 5.f;
            const float width = 50.f;

            menuQuads.clear();
            menuQuads.reserve(4);

            createQuad(menuQuadColor, w - width - padding, w,
                height - textToQuadBorder,
                height - textToQuadBorder + txtSelectionRanked.height +
                    padding);

            render(menuQuads);

            renderText("RANKED", txtSelectionRanked.font,
                {w - width, height -
                                txtSelectionRanked.height * fontHeightOffset -
                                5.5f},
                mouseOverlapColor(mouseOverlap, c));
        }

        renderText(tempString, txtSelectionBig.font,
            {indent, height - txtSelectionBig.height * fontHeightOffset}, c0);

        //-------------------------------------
        // Author
        height += txtSelectionBig.height + textToQuadBorder;

        tempString = focusHeld ? "..." : levelData->author;
        Utils::uppercasify(tempString);

        const sf::Color c1 = mouseOverlapColor(mouseOverlap, menuQuadColor);

        renderText(tempString, txtSelectionSmall.font,
            {indent, height - txtSelectionSmall.height * fontHeightOffset}, c1);

        height += txtSelectionSmall.height + textToQuadBorder - slctFrameSize;
    }

    // Bottom frame for the last element
    menuQuads.clear();
    menuQuads.reserve(4);
    createQuad(
        menuQuadColor, prevLevelIndent, w, height, height + slctFrameSize);
    render(menuQuads);

    height += slctFrameSize;
    i = ssvu::getMod(drawer.packIdx + 1, packsSize);
    if(i == 0)
    {
        height = drawer.YOffset;
    }

    //----------------------------------------
    // PACKS LABELS

    const float arrowWidth{packLabelHeight / 2.f - textToQuadBorder};

    do
    {
        // Quads
        menuQuads.clear();
        menuQuads.reserve(8);

        float temp = quadsIndent - outerFrame + panelOffset;

        createQuad(menuTextColor, temp - slctFrameSize, w, height,
            height + packLabelHeight + slctFrameSize);

        const sf::Vector2f bodyMins{temp, height + slctFrameSize};
        const sf::Vector2f bodyMaxs{w, height + packLabelHeight};

        const bool mouseOverlap =
            overlayMouseOverlapAndUpdateHover(bodyMins, bodyMaxs);

        createQuad(
            mouseOverlapColor(mouseOverlap, menuQuadColor), bodyMins, bodyMaxs);

        // TODO (P2): cleanup mouse control
        if(mouseOverlap && !mustChangePackIndexTo.has_value() &&
            mouseLeftRisingEdge())
        {
            mustChangePackIndexTo = i;
        }

        render(menuQuads);

        // Name & >
        if(drawer.isFavorites)
        {
            tempString = "FAVORITES";
        }
        else
        {
            tempString = assets.getPackData(infos[i].id).name;
            Utils::uppercasify(tempString);
        }

        txtSelectionMedium.font.setString(tempString);
        temp =
            std::max(
                txtIndent - ssvs::getGlobalWidth(txtSelectionMedium.font) / 2.f,
                quadsIndent + arrowWidth + 2.f * slctFrameSize + outerFrame) +
            panelOffset;

        txtSelectionMedium.font.setPosition(
            {temp, height + outerFrame -
                       txtSelectionMedium.height * fontHeightOffset});

        const sf::Color oldC = txtSelectionMedium.font.getFillColor();
        txtSelectionMedium.font.setFillColor(
            mouseOverlapColor(mouseOverlap, menuTextColor));
        render(txtSelectionMedium.font);
        txtSelectionMedium.font.setFillColor(oldC);

        menuQuads.clear();
        menuQuads.reserve(8);

        if(i == drawer.packIdx)
        {
            // Draw > pointing downward, coordinates look a bit complicated
            // cause it's aligned with the middle point of the regular > arrows
            // The arrow is (packLabelHeight / 2.f - textToQuadBorder + 2.f *
            // slctFrameSize) wide
            height +=
                (packLabelHeight - textToQuadBorder - slctFrameSize) / 2.f;
            temp = quadsIndent + arrowWidth / 2.f + slctFrameSize + panelOffset;

            topLeft = {temp - arrowWidth, height};
            bottomLeft = {temp - arrowWidth, height + 2.f * slctFrameSize};

            height += arrowWidth;

            topRight = {temp, height};
            bottomRight = {temp, height + 2.f * slctFrameSize};

            menuQuads.batch_unsafe_emplace_back(
                menuTextColor, topLeft, bottomLeft, bottomRight, topRight);

            topLeft = {temp, height};
            bottomLeft = {temp, height + 2.f * slctFrameSize};

            height -= arrowWidth;
            temp += arrowWidth;

            topRight = {temp, height};
            bottomRight = {temp, height + 2.f * slctFrameSize};

            menuQuads.batch_unsafe_emplace_back(
                menuTextColor, topLeft, bottomLeft, bottomRight, topRight);

            render(menuQuads);
        }
        else
        {
            height += slctFrameSize / 2.f;
            temp = quadsIndent + panelOffset;

            topLeft = {temp, height + textToQuadBorder};
            topRight = {temp + 2.f * slctFrameSize, height + textToQuadBorder};

            height += packLabelHeight / 2.f;
            temp += packLabelHeight / 2.f - textToQuadBorder;

            bottomLeft = {temp, height};
            bottomRight = {temp + 2.f * slctFrameSize, height};

            menuQuads.batch_unsafe_emplace_back(
                menuTextColor, topLeft, topRight, bottomRight, bottomLeft);

            topLeft = {temp, height};
            topRight = {temp + 2.f * slctFrameSize, height};

            height += packLabelHeight / 2.f;
            temp = quadsIndent + panelOffset;

            bottomLeft = {temp, height - textToQuadBorder};
            bottomRight = {
                temp + 2.f * slctFrameSize, height - textToQuadBorder};

            menuQuads.batch_unsafe_emplace_back(
                menuTextColor, topLeft, topRight, bottomRight, bottomLeft);

            render(menuQuads);
            height -= slctFrameSize / 2.f;
        }

        i = ssvu::getMod(i + 1, packsSize);
        if(i == 0)
        {
            height = drawer.YOffset;
        }
    }
    while(i != ssvu::getMod(drawer.packIdx + 1, packsSize));
}

void MenuGame::drawLevelSelectionLeftSide(
    LevelDrawer& drawer, const bool revertOffset)
{
    if(currentPack == nullptr)
    {
        return;
    }

    constexpr float lineThickness{2.f};

    const LevelData& levelData{
        assets.getLevelData(drawer.levelDataIds->at(drawer.currentIndex))};

    const float maxPanelOffset{w * 0.33f};
    const float panelOffset{
        calcMenuOffset(levelDetailsOffset, maxPanelOffset, revertOffset)};
    const float smallInterline{txtSelectionSmall.height * 1.5f};
    const float mediumInterline{txtSelectionSmall.height / 2.f};
    const float textXPos{textToQuadBorder - panelOffset};
    const float textRightBorder{getMaximumTextWidth()};

    const float width{maxPanelOffset - panelOffset};
    float height{textToQuadBorder};

    //-------------------------------------
    // Backdrop - Right border

    menuQuads.clear();
    menuQuads.reserve(8);
    createQuad({menuTextColor.r, menuTextColor.g, menuTextColor.b, 150}, 0,
        width, 0, h);
    createQuad(menuQuadColor, width, width + lineThickness, 0, h);
    render(menuQuads);
    menuQuads.clear();

    //-------------------------------------
    // Level name

    std::string tempString{levelData.name};
    scrollNameRightBorder(tempString, txtSelectionBig.font,
        namesScroll[static_cast<int>(Label::LevelName)], textRightBorder);
    renderText(tempString, txtSelectionBig.font,
        {textXPos, height - txtSelectionBig.height * fontHeightOffset});

    //-------------------------------------
    // Level description

    height += txtSelectionBig.height + textToQuadBorder -
              txtSelectionSmall.height * 0.7f;

    int i;
    for(i = 0; i < static_cast<int>(levelDescription.size()); ++i)
    {
        renderText(
            levelDescription[i], txtSelectionSmall.font, {textXPos, height});
        height +=
            i == descLines - 1 ? txtSelectionSmall.height : smallInterline;
    }
    if(i != descLines)
    {
        height += smallInterline * std::max(0, descLines - 1 - i) +
                  txtSelectionSmall.height;
    }

    height += textToQuadBorder + txtSelectionSmall.height * 0.7f;

    //-------------------------------------
    // Difficulty

    menuQuads.reserve_more(8);

    // Top line
    height += lineThickness;
    createQuad(menuQuadColor, 0, width, height - lineThickness, height);


    txtSelectionSmall.font.setFillColor(menuQuadColor);
    txtSelectionMedium.font.setFillColor(menuQuadColor);

    // Text
    height += textToQuadBorder;
    const float difficultyHeight{
        height - txtSelectionMedium.height * fontHeightOffset};

    renderText("DIFFICULTY: ", txtSelectionMedium.font,
        {textXPos, difficultyHeight}, menuQuadColor);

    tempString =
        levelData.difficultyMults.size() > 1
            ? "< " + ssvu::toStr(levelData.getNthDiffMult(diffMultIdx)) + " >"
            : "NONE";

    const float difficultyBumpFactor =
        1.f + ((difficultyBumpEffect / difficultyBumpEffectMax) * 0.25f);
    txtSelectionMedium.font.setScale(
        difficultyBumpFactor, difficultyBumpFactor);

    renderText(tempString, txtSelectionMedium.font,
        {textXPos + txtSelectionMedium.font.getGlobalBounds().width,
            difficultyHeight});

    txtSelectionMedium.font.setScale(1.f, 1.f);

    // Bottom line
    height += txtSelectionMedium.height + textToQuadBorder + lineThickness;

    createQuad(menuQuadColor, 0, width, height, height - lineThickness);

    //-------------------------------------
    // Pack info

    // "PACK"
    height += textToQuadBorder;

    renderText("PACK", txtSelectionMedium.font,
        {textToQuadBorder - panelOffset,
            height - txtSelectionMedium.height * fontHeightOffset});

    // Pack name
    height += txtSelectionMedium.height + mediumInterline;
    tempString = currentPack->name;


    scrollNameRightBorder(tempString, "NAME: ", txtSelectionSmall.font,
        namesScroll[static_cast<int>(Label::PackName)], textRightBorder);
    renderText(tempString, txtSelectionSmall.font,
        {textXPos, height - txtSelectionSmall.height * fontHeightOffset});

    // Pack author
    height += txtSelectionSmall.height + mediumInterline;

    tempString = currentPack->author;
    scrollNameRightBorder(tempString, "AUTHOR: ", txtSelectionSmall.font,
        namesScroll[static_cast<int>(Label::PackAuthor)], textRightBorder);
    renderText(tempString, txtSelectionSmall.font,
        {textXPos, height - txtSelectionSmall.height * fontHeightOffset});

    // Version
    height += txtSelectionSmall.height + mediumInterline;

    tempString = "VERSION: " + ssvu::toStr(currentPack->version);
    Utils::uppercasify(tempString);
    renderText(tempString, txtSelectionSmall.font,
        {textXPos, height - txtSelectionSmall.height * fontHeightOffset});

    // Bottom line
    menuQuads.reserve_more(4);
    height += txtSelectionSmall.height + txtSelectionMedium.height / 2.f +
              lineThickness;

    createQuad(menuQuadColor, 0, width, height, height - lineThickness);

    //-------------------------------------
    // Music info

    // "MUSIC"
    height += textToQuadBorder;

    renderText("MUSIC", txtSelectionMedium.font,
        {textToQuadBorder - panelOffset,
            height - txtSelectionMedium.height * fontHeightOffset});

    // Track name
    const MusicData& musicDataTemp =
        assets.getMusicData(levelData.packId, levelData.musicId);
    height += txtSelectionMedium.height + mediumInterline;
    tempString = musicDataTemp.name;

    scrollNameRightBorder(tempString, "NAME: ", txtSelectionSmall.font,
        namesScroll[static_cast<int>(Label::MusicName)], textRightBorder);
    renderText(tempString, txtSelectionSmall.font,
        {textXPos, height - txtSelectionSmall.height * fontHeightOffset});

    // Track author
    height += txtSelectionSmall.height + mediumInterline;
    tempString = musicDataTemp.author;

    scrollNameRightBorder(tempString, "AUTHOR: ", txtSelectionSmall.font,
        namesScroll[static_cast<int>(Label::MusicAuthor)], textRightBorder);
    renderText(tempString, txtSelectionSmall.font,
        {textXPos, height - txtSelectionSmall.height * fontHeightOffset});

    // Album name
    height += txtSelectionSmall.height + mediumInterline;
    tempString = !musicDataTemp.album.empty() ? musicDataTemp.album : "NONE";

    scrollNameRightBorder(tempString, "ALBUM: ", txtSelectionSmall.font,
        namesScroll[static_cast<int>(Label::MusicAlbum)], textRightBorder);
    renderText(tempString, txtSelectionSmall.font,
        {textXPos, height - txtSelectionSmall.height * fontHeightOffset});

    height += txtSelectionSmall.height + textToQuadBorder;

    //-------------------------------------
    // Favorite "button"

    menuQuads.reserve_more(8 * 5);
    const float favoriteButtonBottom{height + 3.f * txtSelectionMedium.height};

    // Frame
    createQuad(menuQuadColor, lineThickness - panelOffset, width, height,
        height + lineThickness);
    createQuad(menuQuadColor, -panelOffset, lineThickness - panelOffset, height,
        favoriteButtonBottom);
    createQuad(menuQuadColor, width, width, height, favoriteButtonBottom);
    createQuad(menuQuadColor, lineThickness - panelOffset, width,
        favoriteButtonBottom - lineThickness, favoriteButtonBottom);

    // Backdrop
    const sf::Vector2f bodyMins{
        lineThickness - panelOffset, height + lineThickness};

    const sf::Vector2f bodyMaxs{width, favoriteButtonBottom - lineThickness};

    const bool mouseOverlap =
        overlayMouseOverlapAndUpdateHover(bodyMins, bodyMaxs);

    createQuad(mouseOverlapColor(mouseOverlap, menuSelectionColor), bodyMins,
        bodyMaxs);

    // TODO (P2): cleanup mouse control
    if(mouseOverlap && !mustFavorite && mouseLeftRisingEdge())
    {
        mustFavorite = true;
    }

    // Also renders all previous quads
    render(menuQuads);
    menuQuads.clear();

    renderTextCenteredOffset(
        isLevelFavorite ? "[F1] UNFAVORITE" : "[F1]    FAVORITE",
        txtSelectionMedium.font,
        {maxPanelOffset / 2.f,
            height + txtSelectionMedium.height * (1.f - fontHeightOffset)},
        -panelOffset, mouseOverlapColor(mouseOverlap, menuQuadColor));

    height = favoriteButtonBottom + textToQuadBorder;

    //-------------------------------------
    // Leaderboard

    // Personal best
    renderText("LOCAL PERSONAL BEST", txtSelectionMedium.font,
        {textToQuadBorder - panelOffset,
            height - txtSelectionMedium.height * fontHeightOffset});

    height += txtSelectionMedium.height + textToQuadBorder;

    const auto currentDiffMult = levelData.getNthDiffMult(diffMultIdx);

    if(levelData.unscored)
    {
        renderText("N/A", txtSelectionSmall.font,
            {textToQuadBorder - panelOffset,
                height - txtSelectionSmall.height * fontHeightOffset});
    }
    else
    {
        const std::string& localLevelValidator =
            levelData.getValidatorWithoutPackId(currentDiffMult);

        tempString = localLevelValidator;
        renderText(
            ssvu::toStr(assets.getCurrentLocalProfile().getScore(tempString)) +
                "s",
            txtSelectionSmall.font,
            {textToQuadBorder - panelOffset,
                height - txtSelectionSmall.height * fontHeightOffset});
    }

    // Line
    height += txtSelectionSmall.height + textToQuadBorder + lineThickness;
    menuQuads.reserve(4);
    createQuad(menuQuadColor, 0, width, height, height - lineThickness);

    // "LEADERBOARD"
    height += textToQuadBorder;
    renderTextCenteredOffset("ONLINE LEADERBOARD", txtSelectionBig.font,
        {maxPanelOffset / 2.f,
            height - txtSelectionBig.height * fontHeightOffset * .8f},
        -panelOffset);

    // Line
    height += txtSelectionScore.height + txtSelectionBig.height / 2.f + 3.f;
    menuQuads.reserve_more(4);
    createQuad(menuQuadColor, 0, width, height, height + lineThickness);
    height += lineThickness;

    height += txtSelectionSmall.height;

    const std::string& levelValidator = levelData.getValidator(currentDiffMult);

    if(!levelData.unscored &&
        hexagonClient.getState() == HexagonClient::State::LoggedIn_Ready &&
        hexagonClient.isLevelSupportedByServer(levelValidator) &&
        leaderboardCache->shouldRequestScores(levelValidator))
    {
        hexagonClient.tryRequestTopScoresAndOwnScore(levelValidator);
        leaderboardCache->requestedScores(levelValidator);
    }

    const bool gotScoreInfo = leaderboardCache->hasInformation(levelValidator);

    if(levelData.unscored)
    {
        renderText("LEADERBOARD DISABLED FOR THIS LEVEL",
            txtSelectionSmall.font,
            {textToQuadBorder - panelOffset,
                height - txtSelectionSmall.height * fontHeightOffset});
    }
    else if(hexagonClient.getState() != HexagonClient::State::LoggedIn_Ready)
    {
        renderText("PLEASE LOG IN TO LOAD LEADERBOARD", txtSelectionSmall.font,
            {textToQuadBorder - panelOffset,
                height - txtSelectionSmall.height * fontHeightOffset});
    }
    else if(!hexagonClient.isLevelSupportedByServer(levelValidator))
    {
        renderText("THIS LEVEL IS NOT SUPPORTED BY THE SERVER",
            txtSelectionSmall.font,
            {textToQuadBorder - panelOffset,
                height - txtSelectionSmall.height * fontHeightOffset});
    }
    else if(!gotScoreInfo)
    {
        renderText("...", txtSelectionSmall.font,
            {textToQuadBorder - panelOffset,
                height - txtSelectionSmall.height * fontHeightOffset});
    }
    else
    {
        SSVOH_ASSERT(!levelData.unscored);
        SSVOH_ASSERT(gotScoreInfo);
        SSVOH_ASSERT(hexagonClient.isLevelSupportedByServer(levelValidator));
        SSVOH_ASSERT(
            hexagonClient.getState() == HexagonClient::State::LoggedIn_Ready);

        const auto serializeTimePoint =
            [](const auto& time, const std::string& format)
        {
            std::time_t tt = std::chrono::system_clock::to_time_t(time);
            std::tm tm = *std::gmtime(&tt); // GMT (UTC)
            std::stringstream ss;
            ss << std::put_time(&tm, format.c_str());
            return ss.str();
        };

        const auto drawEntry = [&](const int i, const std::string& userName,
                                   const std::uint64_t scoreTimestamp,
                                   const double scoreValue)
        {
            const float score = scoreValue;

            const auto tp = Database::toTimepoint(scoreTimestamp);

            const std::string timestampStr =
                serializeTimePoint(tp, "%Y-%m-%d %H:%M:%S");

            const std::string posStr = Utils::concat('#', i + 1);
            std::string scoreStr = ssvu::toStr(score) + 's';

            std::string playerStr = userName;
            if(playerStr.size() > 20)
            {
                playerStr.resize(17);
                playerStr += "...";
            }

            const float tx = textToQuadBorder - panelOffset;
            const float ty = height -
                             txtSelectionMedium.height * fontHeightOffset +
                             txtSelectionSmall.height;

            renderText(timestampStr, txtSelectionSmall.font, {tx, ty});
            renderText(posStr, txtSelectionMedium.font, {tx, ty + 7.5f});
            renderText(
                scoreStr, txtSelectionMedium.font, {tx + 54.f, ty + 7.5f});
            renderText(
                playerStr, txtSelectionMedium.font, {tx + 150.f, ty + 7.5f});

            height += txtSelectionMedium.height + txtSelectionSmall.height +
                      txtSelectionSmall.height + 10.f;
        };

        if(gotScoreInfo)
        {
            const auto scores = leaderboardCache->getScores(levelValidator);

            if(!scores.empty())
            {
                int index = 0;
                for(const Database::ProcessedScore& ps : scores)
                {
                    drawEntry(
                        index, ps.userName, ps.scoreTimestamp, ps.scoreValue);
                    ++index;
                }

                height -= txtSelectionMedium.height + txtSelectionSmall.height;
            }
            else
            {
                const float tx = textToQuadBorder - panelOffset;
                const float ty =
                    height - txtSelectionMedium.height * fontHeightOffset;

                renderText(
                    "NO SCORES FOUND", txtSelectionMedium.font, {tx, ty});
            }
        }

        // Line
        height += txtSelectionScore.height + txtSelectionBig.height / 2.f;
        menuQuads.reserve_more(4);
        createQuad(menuQuadColor, 0, width, height, height + lineThickness);
        height += lineThickness;

        height += txtSelectionSmall.height;

        renderText("YOUR POSITION", txtSelectionSmall.font,
            {textToQuadBorder - panelOffset,
                height - txtSelectionSmall.height * fontHeightOffset});

        height += txtSelectionSmall.height * 2.f + 5.f;

        if(gotScoreInfo)
        {
            const auto* ownScore =
                leaderboardCache->getOwnScore(levelValidator);

            if(ownScore == nullptr)
            {
                const float tx = textToQuadBorder - panelOffset;
                const float ty =
                    height - txtSelectionMedium.height * fontHeightOffset;

                renderText(
                    "NO OWN SCORE SET", txtSelectionMedium.font, {tx, ty});
            }
            else
            {
                drawEntry(ownScore->position, ownScore->userName,
                    ownScore->scoreTimestamp, ownScore->scoreValue);
            }
        }
    }

    render(menuQuads);
}

void MenuGame::draw()
{
    mouseHovering = false;
    mouseWasPressed = mousePressed;
    mousePressed =
        (ignoreInputs == 0) && sf::Mouse::isButtonPressed(sf::Mouse::Left);

    if(mustRefresh)
    {
        mustRefresh = false;

        refreshCamera();
        adjustLevelsOffset();
        adjustMenuOffset(true);
        resetNamesScrolls();
    }

    styleData.computeColors();
    window.clear(sf::Color{0, 0, 0, 255});

    backgroundCamera.apply();
    const bool mainOrAbove{state >= States::SMain};

    // Only draw the hexagon background past the loading screens.
    if(mainOrAbove)
    {
        menuBackgroundTris.clear();

        styleData.drawBackgroundMenu(menuBackgroundTris, ssvs::zeroVec2f,
            levelStatus.sides,
            Config::getDarkenUnevenBackgroundChunk() &&
                levelStatus.darkenUnevenBackgroundChunk,
            Config::getBlackAndWhite(), fourByThree);

        render(menuBackgroundTris);
    }

    overlayCamera.apply();

    // Draw the profile name.
    if(mainOrAbove && state != States::LevelSelection)
    {
        strBuf.clear();
        strBuf += "CURRENT PROFILE: ";
        strBuf += assets.pGetName();

        const auto& pwmd = assets.getPackIdsWithMissingDependencies();

        if(!pwmd.empty())
        {
            strBuf += "\n\nWARNING - PACKS WITH MISSING DEPENDENCIES:";

            for(const auto& p : pwmd)
            {
                strBuf += "\n    ";
                strBuf += p;
            }

            strBuf += "\nFORGOT TO DOWNLOAD THEM FROM THE STEAM WORKSHOP?";
        }

        renderText(strBuf, txtSelectionSmall.font,
            sf::Vector2f{20.f, ssvs::getGlobalBottom(titleBar) + 8});
    }

    float indentBig{400.f}, indentSmall{540.f}, profileIndent{-100.f};
    // We need different values to fit menus in 4:3
    if(fourByThree)
    {
        indentBig = 280.f;
        indentSmall = 410.f;
        profileIndent = -75.f;
    }

    switch(state)
    {
        case States::LoadingScreen:
            drawLoadResults();
            renderText("PRESS ANY KEY OR BUTTON TO CONTINUE", txtProf.font,
                {txtProf.height, h - txtProf.height * 2.7f});
            return;

        case States::EpilepsyWarning:
            render(epilepsyWarning);
            renderText("PRESS ANY KEY OR BUTTON TO CONTINUE", txtProf.font,
                {txtProf.height, h - txtProf.height * 2.7f});
            return;

        case States::ETLPNewBoot:
            drawEnteringTextBoot();
            drawGraphics();
            break;

        case States::SLPSelectBoot:
            drawProfileSelectionBoot();
            drawGraphics();
            break;

        case States::SMain:
            // Fold previous menus
            if(optionsMenu.getCategory().getOffset() != 0.f)
            {
                drawMainMenu(optionsMenu.getCategoryByName("options"),
                    w - indentBig, true);
            }

            if(profileSelectionMenu.getCategory().getOffset() != 0.f)
            {
                drawProfileSelection(profileIndent, true);
            }

            if(enteringTextOffset != 0.f)
            {
                drawEnteringText(profileIndent, true);
            }

            if(levelDetailsOffset != 0.f)
            {
                drawLevelSelectionRightSide(*lvlDrawer, true);
                drawLevelSelectionLeftSide(*lvlDrawer, true);
            }

            // Draw main menu (right)
            drawMainSubmenus(mainMenu.getCategories(), indentBig);
            drawGraphics();
            drawOnlineStatus();
            break;

        case States::MOpts:
            // fold the main menu
            if(mainMenu.getCategory().getOffset() != 0.f)
            {
                drawMainMenu(
                    mainMenu.getCategoryByName("main"), w - indentBig, true);
            }

            // Option Menu (right)
            drawMainMenu(
                optionsMenu.getCategoryByName("options"), w - indentBig, false);

            // Draw options submenus (left)
            drawSubmenusSmall(optionsMenu.getCategories(), indentSmall);
            drawGraphics();
            drawOnlineStatus();
            break;

        case States::MOnline:
            // fold the main menu
            if(mainMenu.getCategory().getOffset() != 0.f)
            {
                drawMainMenu(
                    mainMenu.getCategoryByName("main"), w - indentBig, true);
            }

            // Online Menu (right)
            drawMainMenu(
                onlineMenu.getCategoryByName("options"), w - indentBig, false);

            // Draw online submenus (left)
            drawSubmenusSmall(onlineMenu.getCategories(), indentSmall);
            drawGraphics();
            drawOnlineStatus();
            break;

        case States::ETLPNew:
            drawMainMenu(mainMenu.getCategoryByName("local profiles"),
                w - indentBig, false);
            drawEnteringText(profileIndent, false);
            drawGraphics();
            break;

        case States::SLPSelect:
            drawMainMenu(mainMenu.getCategoryByName("local profiles"),
                w - indentBig, false);
            drawProfileSelection(profileIndent, false);
            drawGraphics();
            break;

        case States::LevelSelection:
            // fold the main menu
            if(mainMenu.getCategory().getOffset() != 0.f)
            {
                drawMainMenu(
                    mainMenu.getCategoryByName("main"), w - indentBig, true);
            }

            if(isFavoriteLevels())
            {
                drawLevelSelectionRightSide(favSlct, false);
                if(lvlSlct.XOffset != 0.f)
                {
                    drawLevelSelectionRightSide(lvlSlct, true);
                }
            }
            else
            {
                drawLevelSelectionRightSide(lvlSlct, false);
                if(favSlct.XOffset != 0.f)
                {
                    drawLevelSelectionRightSide(favSlct, true);
                }
            }

            drawLevelSelectionLeftSide(*lvlDrawer, false);
            drawOnlineStatus();
            break;

        default: break;
    }

    if(mustTakeScreenshot)
    {
        window.saveScreenshot("screenshot.png");
        mustTakeScreenshot = false;
    }

    if(!dialogBox.empty())
    {
        overlayCamera.apply();
        dialogBox.draw(dialogBoxTextColor, styleData.getColor(0));
    }

    if(!mouseWasPressed && mousePressed)
    {
        lastMouseClick = Clock::now();
    }
}

void MenuGame::render(sf::Drawable& mDrawable)
{
    window.draw(mDrawable);
}

[[nodiscard]] float MenuGame::getFPSMult() const
{
    // multiplier for FPS consistent drawing operations.
    return 200.f / window.getFPS();
}

void MenuGame::drawGraphics()
{
    render(titleBar);
    render(creditsBar1);
    render(creditsBar2);
    render(txtVersion.font);
}

void MenuGame::drawOnlineStatus()
{
    window.getRenderWindow().setView(
        sf::View{{0.f, 0.f, getWindowWidth(), getWindowHeight()}});

    const float onlineStatusScaling = 1.5f;
    const float scaling = onlineStatusScaling / Config::getZoomFactor();
    const float padding = 3.f * onlineStatusScaling;

    txtOnlineStatus.setCharacterSize(10 * scaling);
    txtOnlineStatus.setFillColor(sf::Color::White);

    const HexagonClient::State state = hexagonClient.getState();

    const auto [stateGood, stateString] = [&]() -> std::tuple<bool, std::string>
    {
        switch(state)
        {
            case HexagonClient::State::Disconnected:
            {
                return {false, "DISCONNECTED"};
            }

            case HexagonClient::State::InitError:
            {
                return {false, "CLIENT ERROR"};
            }

            case HexagonClient::State::Connecting:
            {
                return {false, "CONNECTING"};
            }

            case HexagonClient::State::ConnectionError:
            {
                return {false, "CONNECTION ERROR"};
            }

            case HexagonClient::State::Connected:
            {
                if(hexagonClient.hasRTKeys())
                {
                    return {true, "CONNECTED, PLEASE LOG IN"};
                }
                else
                {
                    return {false, "CONNECTED, BUT KEY EXCHANGE FAILED"};
                }
            }

            case HexagonClient::State::LoggedIn: [[fallthrough]];
            case HexagonClient::State::LoggedIn_Ready:
            {
                return {
                    true, "LOGGED IN AS " +
                              hexagonClient.getLoginName().value_or("UNKNOWN")};
            }
        }

        return {false, "UNKNOWN"};
    }();

    txtOnlineStatus.setString("ABC,:ç@'");
    const auto txtHeight = ssvs::getGlobalHeight(txtOnlineStatus);
    const float spriteScale = (txtHeight + padding * 2.f) / 64.f;

    txtOnlineStatus.setString(stateString);

    if(stateGood)
    {
        sOnline.setTexture(assets.getTexture("onlineIcon.png"));
    }
    else
    {
        sOnline.setTexture(assets.getTexture("onlineIconFail.png"));
    }

    sOnline.setScale(sf::Vector2f{spriteScale, spriteScale});
    sOnline.setOrigin(ssvs::getLocalSW(sOnline));
    sOnline.setPosition(0 + padding, getWindowHeight() - padding);

    rsOnlineStatus.setSize(
        sf::Vector2f{ssvs::getGlobalWidth(txtOnlineStatus) + padding * 4.f,
            txtHeight + padding * 2.f});
    rsOnlineStatus.setFillColor(sf::Color::Black);
    rsOnlineStatus.setOrigin(ssvs::getLocalSW(rsOnlineStatus));
    rsOnlineStatus.setPosition(
        ssvs::getGlobalRight(sOnline) + padding, sOnline.getPosition().y);

    txtOnlineStatus.setOrigin(ssvs::getLocalCenterW(txtOnlineStatus));
    txtOnlineStatus.setPosition(ssvs::getGlobalLeft(rsOnlineStatus) + padding * 2.f,
        ssvs::getGlobalCenter(rsOnlineStatus).y);

    render(sOnline);
    render(rsOnlineStatus);
    render(txtOnlineStatus);
}

void MenuGame::showDialogBox(const std::string& msg)
{
    dialogBox.create(
        msg, 22 /* charSize */, 12.f /* frameSize */, DBoxDraw::center);
}

void MenuGame::showInputDialogBox(const std::string& msg)
{
    dialogBox.createInput(
        msg, 22 /* charSize */, 12.f /* frameSize */, DBoxDraw::center);
}

void MenuGame::showInputDialogBoxNice(const std::string& title,
    const std::string& inputType, const std::string& extra)
{
    strBuf.clear();

    if(extra.empty())
    {
        strBuf += Utils::concat(title, "\n\nPLEASE INSERT ", inputType,
            "\n\nCONFIRM WITH [ENTER]\nCANCEL WITH [ESCAPE]\n");
    }
    else
    {
        strBuf += Utils::concat(title, "\n\nPLEASE INSERT ", inputType, "\n\n",
            extra, "\n\nCONFIRM WITH [ENTER]\nCANCEL WITH [ESCAPE]\n");
    }

    showInputDialogBox(strBuf);
}

} // namespace hg
