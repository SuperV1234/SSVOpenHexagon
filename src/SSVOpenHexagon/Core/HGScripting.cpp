// Copyright (c) 2013-2020 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: https://opensource.org/licenses/AFL-3.0

#include "SSVOpenHexagon/Components/CWall.hpp"
#include "SSVOpenHexagon/Global/Assets.hpp"
#include "SSVOpenHexagon/Global/Config.hpp"
#include "SSVOpenHexagon/Global/Audio.hpp"
#include "SSVOpenHexagon/Utils/Utils.hpp"
#include "SSVOpenHexagon/Utils/Concat.hpp"
#include "SSVOpenHexagon/Utils/ScopeGuard.hpp"
#include "SSVOpenHexagon/Utils/LuaMetadata.hpp"
#include "SSVOpenHexagon/Utils/LuaMetadataProxy.hpp"
#include "SSVOpenHexagon/Core/HexagonGame.hpp"
#include "SSVOpenHexagon/Components/CCustomWallHandle.hpp"
#include "SSVOpenHexagon/Core/LuaScripting.hpp"
#include "SSVOpenHexagon/Utils/TypeWrapper.hpp"
#include "SSVOpenHexagon/Core/Steam.hpp"

#include <SSVUtils/Core/Log/Log.hpp>

#include <SFML/Graphics.hpp>
#include <SFML/System.hpp>

#include <iostream>
#include <string>
#include <chrono>
#include <cmath>

namespace hg {

template <typename F>
Utils::LuaMetadataProxy addLuaFn(
    Lua::LuaContext& lua, const std::string& name, F&& f)
{
    // TODO (P2): reduce instantiations by using captureless lambdas
    lua.writeVariable(name, std::forward<F>(f));
    return Utils::LuaMetadataProxy{
        Utils::TypeWrapper<F>{}, LuaScripting::getMetadata(), name};
}

void HexagonGame::initLua_Utils()
{
    // ------------------------------------------------------------------------
    // Used internally to track values in the console.
    lua.writeVariable("u_impl_addTrackedResult",
        [this](const std::string& result)
        { ilcLuaTrackedResults.emplace_back(result); });

    // ------------------------------------------------------------------------
    addLuaFn(lua, "u_setFlashEffect", //
        [this](float mIntensity) { status.flashEffect = mIntensity; })
        .arg("value")
        .doc("Flash the screen with `$0` intensity (from 0 to 255).");

    addLuaFn(lua, "u_log", //
        [this](const std::string& mLog)
        {
            ssvu::lo("lua") << mLog << '\n';
            ilcCmdLog.emplace_back("[lua]: " + mLog + '\n');
        })
        .arg("message")
        .doc("Print out `$0` to the console.");

    addLuaFn(lua, "u_execScript", //
        [this](const std::string& mScriptName)
        {
            runLuaFile(Utils::getDependentScriptFilename(
                execScriptPackPathContext, levelData->packPath, mScriptName));
        })
        .arg("scriptFilename")
        .doc("Execute the script located at `<pack>/Scripts/$0`.");

    addLuaFn(lua, "u_execDependencyScript", //
        [this](const std::string& mPackDisambiguator,
            const std::string& mPackName, const std::string& mPackAuthor,
            const std::string& mScriptName)
        {
            Utils::withDependencyScriptFilename(
                [this](const std::string& filename) { runLuaFile(filename); },
                execScriptPackPathContext, assets, getPackData(),
                mPackDisambiguator, mPackName, mPackAuthor, mScriptName);
        })
        .arg("packDisambiguator")
        .arg("packName")
        .arg("packAuthor")
        .arg("scriptFilename")
        .doc(
            "Execute the script provided by the dependee pack with "
            "disambiguator `$0`, name `$1`, author `$2`, located at "
            "`<dependeePack>/Scripts/$3`.");

    addLuaFn(lua, "u_isKeyPressed",
        [this](int mKey)
        {
            // TODO (P2): this is not saved in replays. Deprecate?

            return window != nullptr &&
                   window->getInputState()[ssvs::KKey(mKey)];
        })
        .arg("keyCode")
        .doc(
            "Return `true` if the keyboard key with code `$0` is being "
            "pressed, `false` otherwise. The key code must match the "
            "definition of the SFML `sf::Keyboard::Key` enumeration.");

    addLuaFn(lua, "u_haltTime", //
        [this](double mDuration)
        { status.pauseTime(ssvu::getFTToSeconds(mDuration)); })
        .arg("duration")
        .doc("Pause the game timer for `$0` seconds.");

    addLuaFn(lua, "u_clearWalls", //
        [this] { walls.clear(); })
        .doc("Remove all existing walls.");

    addLuaFn(lua, "u_getPlayerAngle", //
        [this] { return player.getPlayerAngle(); })
        .doc("Return the current angle of the player, in radians.");

    addLuaFn(lua, "u_setPlayerAngle",
        [this](float newAng) { player.setPlayerAngle(newAng); })
        .arg("angle")
        .doc("Set the current angle of the player to `$0`, in radians.");

    addLuaFn(lua, "u_isMouseButtonPressed",
        [this](int mKey)
        {
            // TODO (P2): this is not saved in replays. Deprecate?

            return window != nullptr &&
                   window->getInputState()[ssvs::MBtn(mKey)];
        })
        .arg("buttonCode")
        .doc(
            "Return `true` if the mouse button with code `$0` is being "
            "pressed, `false` otherwise. The button code must match the "
            "definition of the SFML `sf::Mouse::Button` enumeration.");

    addLuaFn(lua, "u_isFastSpinning", //
        [this] { return status.fastSpin > 0; })
        .doc(
            "Return `true` if the camera is currently \"fast spinning\", "
            "`false` otherwise.");

    addLuaFn(lua, "u_forceIncrement", //
        [this] { incrementDifficulty(); })
        .doc(
            "Immediately force a difficulty increment, regardless of the "
            "chosen automatic increment parameters.");

    addLuaFn(lua, "u_getDifficultyMult", //
        [this] { return difficultyMult; })
        .doc("Return the current difficulty multiplier.");

    addLuaFn(lua, "u_getSpeedMultDM", //
        [this] { return getSpeedMultDM(); })
        .doc(
            "Return the current speed multiplier, adjusted for the chosen "
            "difficulty multiplier.");

    addLuaFn(lua, "u_getDelayMultDM", //
        [this] { return getDelayMultDM(); })
        .doc(
            "Return the current delay multiplier, adjusted for the chosen "
            "difficulty multiplier.");

    addLuaFn(lua, "u_swapPlayer", //
        [this](bool mPlaySound) { performPlayerSwap(mPlaySound); })
        .arg("playSound")
        .doc(
            "Force-swaps (180 degrees) the player when invoked. If `$0` is "
            "`true`, the swap sound will be played.");
}

void HexagonGame::initLua_AudioControl()
{
    addLuaFn(lua, "a_getMusicSeconds", //
        [this] { return status.getMusicTimeSeconds(); })
        .doc("Return the music pointer's current position in seconds.");

    addLuaFn(lua, "a_getMusicMilliseconds", //
        [this] { return status.getMusicTime(); })
        .doc(
            "Return the music pointer's current position in milliseconds. "
            "Unlike `a_getMusicSeconds`, this will return an integer. Useful "
            "for avoiding floating point error.");

    addLuaFn(lua, "a_evalTo", //
        [this](double mTime, const std::string& mCode)
        {
            musicTimeline.append_to((int32_t)(mTime * 1000),
                [=, this] { Utils::runLuaCode(lua, mCode); });
        })
        .arg("time")
        .arg("code")
        .doc(
            "*Add to the music timeline*: evaluate the Lua code specified in "
            "`$1` at `$0` seconds in the music timeline.");

    addLuaFn(lua, "a_evalToMs", //
        [this](int32_t mTime, const std::string& mCode)
        {
            musicTimeline.append_to(
                mTime, [=, this] { Utils::runLuaCode(lua, mCode); });
        })
        .arg("time")
        .arg("code")
        .doc(
            "*Add to the music timeline*: evaluate the Lua code specified in "
            "`$1` at `$0` milliseconds in the music timeline.");

    addLuaFn(lua, "a_clearTimepoint", //
        [this](double mTime)
        { musicTimeline.clearTimepoint((int32_t)(mTime * 1000)); })
        .arg("time")
        .doc(
            "Removes all events that occur at specific time `$0` seconds in "
            "the music timeline.");

    addLuaFn(lua, "a_clearTimepointMs", //
        [this](int32_t mTime) { musicTimeline.clearTimepoint(mTime); })
        .arg("time")
        .doc(
            "Removes all events that occur at specific time `$0` milliseconds "
            "in the music timeline.");

    addLuaFn(lua, "a_clear", //
        [this] { musicTimeline.clear(); })
        .doc("Removes all events from the music timeline.");

    addLuaFn(lua, "a_setMusic", //
        [this](const std::string& mId)
        {
            musicData = assets.getMusicData(levelData->packId, mId);
            musicData.firstPlay = true;
            stopLevelMusic();
            playLevelMusic();
        })
        .arg("musicId")
        .doc(
            "Stop the current music and play the music with id `$0`. The id is "
            "defined in the music `.json` file, under `\"id\"`.");

    addLuaFn(lua, "a_setMusicSegment", //
        [this](const std::string& mId, int segment)
        {
            musicData = assets.getMusicData(levelData->packId, mId);
            stopLevelMusic();
            playLevelMusicAtTime(musicData.getSegment(segment).time);
        })
        .arg("musicId")
        .arg("segment")
        .doc(
            "Stop the current music and play the music with id `$0`, starting "
            "at segment `$1`. Segments are defined in the music `.json` file, "
            "under `\"segments\"`.");

    addLuaFn(lua, "a_setMusicSeconds", //
        [this](const std::string& mId, float mTime)
        {
            musicData = assets.getMusicData(levelData->packId, mId);
            stopLevelMusic();
            playLevelMusicAtTime(mTime);
        })
        .arg("musicId")
        .arg("time")
        .doc(
            "Stop the current music and play the music with id `$0`, starting "
            "at time `$1` (in seconds).");

    addLuaFn(lua, "a_playSound", //
        [this](const std::string& mId) { playSoundOverride(mId); })
        .arg("soundId")
        .doc(
            "Play the sound with id `$0`. The id must be registered in "
            "`assets.json`, under `\"soundBuffers\"`.");

    addLuaFn(lua, "a_playPackSound", //
        [this](const std::string& fileName)
        { playPackSoundOverride(getPackId(), fileName); })
        .arg("fileName")
        .doc(
            "Dives into the `Sounds` folder of the current level pack and "
            "plays the specified file `$0`.");

    addLuaFn(lua, "a_syncMusicToDM", //
        [this](bool value)
        {
            levelStatus.syncMusicToDM = value;
            refreshMusicPitch();
        })
        .arg("value")
        .doc(
            "This function, when called, overrides the user's preference of "
            "adjusting the music's pitch to the difficulty multiplier. Useful "
            "for levels that rely on the music to time events.");

    addLuaFn(lua, "a_setMusicPitch", //
        [this](float mPitch)
        {
            levelStatus.musicPitch = mPitch;
            refreshMusicPitch();
        })
        .arg("pitch")
        .doc(
            "Manually adjusts the pitch of the music by multiplying it by "
            "`$0`. The amount the pitch shifts may change on DM multiplication "
            "and user's preference of the music pitch. **Negative values will "
            "not work!**");

    addLuaFn(lua, "a_overrideBeepSound", //
        [this](const std::string& mId)
        { levelStatus.beepSound = getPackId() + "_" + mId; })
        .arg("fileName")
        .doc(
            "Dives into the `Sounds` folder of the current level pack and "
            "sets the specified file `$0` to be the new beep sound. This only "
            "applies to the particular level where this function is called.");

    addLuaFn(lua, "a_overrideIncrementSound", //
        [this](const std::string& mId)
        { levelStatus.levelUpSound = getPackId() + "_" + mId; })
        .arg("fileName")
        .doc(
            "Dives into the `Sounds` folder of the current level pack and "
            "sets the specified file `$0` to be the new increment sound. This "
            "only "
            "applies to the particular level where this function is called.");

    addLuaFn(lua, "a_overrideSwapSound", //
        [this](const std::string& mId)
        { levelStatus.swapSound = getPackId() + "_" + mId; })
        .arg("fileName")
        .doc(
            "Dives into the `Sounds` folder of the current level pack and "
            "sets the specified file `$0` to be the new swap sound. This only "
            "applies to the particular level where this function is called.");

    addLuaFn(lua, "a_overrideDeathSound", //
        [this](const std::string& mId)
        { levelStatus.deathSound = getPackId() + "_" + mId; })
        .arg("fileName")
        .doc(
            "Dives into the `Sounds` folder of the current level pack and "
            "sets the specified file `$0` to be the new death sound. This only "
            "applies to the particular level where this function is called.");
}

void HexagonGame::initLua_MainTimeline()
{
    addLuaFn(lua, "t_eval",
        [this](const std::string& mCode)
        { timeline.append_do([=, this] { Utils::runLuaCode(lua, mCode); }); })
        .arg("code")
        .doc(
            "*Add to the main timeline*: evaluate the Lua code specified in "
            "`$0`.");

    addLuaFn(lua, "t_clear", [this]() { timeline.clear(); })
        .doc("Clear the main timeline.");

    addLuaFn(lua, "t_kill", //
        [this] { timeline.append_do([this] { death(true); }); })
        .doc("*Add to the main timeline*: kill the player.");

    addLuaFn(lua, "t_wait",
        [this](double mDuration)
        { timeline.append_wait_for_sixths(mDuration); })
        .arg("duration")
        .doc(
            "*Add to the main timeline*: wait for `$0` frames (under the "
            "assumption of a 60 FPS frame rate).");

    addLuaFn(lua, "t_waitS", //
        [this](double mDuration)
        { timeline.append_wait_for_seconds(mDuration); })
        .arg("duration")
        .doc("*Add to the main timeline*: wait for `$0` seconds.");

    addLuaFn(lua, "t_waitUntilS", //
        [this](double mDuration)
        {
            timeline.append_wait_until_fn(
                [this, mDuration]
                {
                    return status.getLevelStartTP() +
                           std::chrono::milliseconds(
                               static_cast<int>(mDuration * 1000.0));
                });
        })
        .arg("duration")
        .doc(
            "*Add to the main timeline*: wait until the timer reaches `$0` "
            "seconds.");
}

void HexagonGame::initLua_EventTimeline()
{
    addLuaFn(lua, "e_eval",
        [this](const std::string& mCode) {
            eventTimeline.append_do(
                [=, this] { Utils::runLuaCode(lua, mCode); });
        })
        .arg("code")
        .doc(
            "*Add to the event timeline*: evaluate the Lua code specified in "
            "`$0`. (This is the closest you'll get to 1.92 events)");

    addLuaFn(lua, "e_kill", //
        [this] { eventTimeline.append_do([this] { death(true); }); })
        .doc("*Add to the event timeline*: kill the player.");

    addLuaFn(lua, "e_stopTime", //
        [this](double mDuration)
        {
            eventTimeline.append_do([=, this]
                { status.pauseTime(ssvu::getFTToSeconds(mDuration)); });
        })
        .arg("duration")
        .doc(
            "*Add to the event timeline*: pause the game timer for `$0` frames "
            "(under the assumption of a 60 FPS frame rate).");

    addLuaFn(lua, "e_stopTimeS", //
        [this](double mDuration) {
            eventTimeline.append_do([=, this] { status.pauseTime(mDuration); });
        })
        .arg("duration")
        .doc(
            "*Add to the event timeline*: pause the game timer for `$0` "
            "seconds.");

    addLuaFn(lua, "e_wait",
        [this](double mDuration)
        { eventTimeline.append_wait_for_sixths(mDuration); })
        .arg("duration")
        .doc(
            "*Add to the event timeline*: wait for `$0` frames (under the "
            "assumption of a 60 FPS frame rate).");

    addLuaFn(lua, "e_waitS", //
        [this](double mDuration)
        { eventTimeline.append_wait_for_seconds(mDuration); })
        .arg("duration")
        .doc("*Add to the event timeline*: wait for `$0` seconds.");

    addLuaFn(lua, "e_waitUntilS", //
        [this](double mDuration)
        {
            eventTimeline.append_wait_until_fn(
                [this, mDuration]
                {
                    return status.getLevelStartTP() +
                           std::chrono::milliseconds(
                               static_cast<int>(mDuration * 1000.0));
                });
        })
        .arg("duration")
        .doc(
            "*Add to the event timeline*: wait until the timer reaches `$0` "
            "seconds.");

    addLuaFn(lua, "e_messageAdd", //
        [this](const std::string& mMsg, double mDuration)
        {
            eventTimeline.append_do(
                [=, this]
                {
                    if(firstPlay)
                    {
                        addMessage(mMsg, mDuration, /* mSoundToggle */ true);
                    }
                });
        })
        .arg("message")
        .arg("duration")
        .doc(
            "*Add to the event timeline*: print a message with text `$0` for "
            "`$1` seconds. The message will only be printed during the first "
            "run of the level.");

    addLuaFn(lua, "e_messageAddImportant", //
        [this](const std::string& mMsg, double mDuration)
        {
            eventTimeline.append_do([=, this]
                { addMessage(mMsg, mDuration, /* mSoundToggle */ true); });
        })
        .arg("message")
        .arg("duration")
        .doc(
            "*Add to the event timeline*: print a message with text `$0` for "
            "`$1` seconds. The message will be printed during every run of the "
            "level.");

    addLuaFn(lua, "e_messageAddImportantSilent",
        [this](const std::string& mMsg, double mDuration)
        {
            eventTimeline.append_do([=, this]
                { addMessage(mMsg, mDuration, /* mSoundToggle */ false); });
        })
        .arg("message")
        .arg("duration")
        .doc(
            "*Add to the event timeline*: print a message with text `$0` for "
            "`$1` seconds. The message will only be printed during every "
            "run of the level, and will not produce any sound.");

    addLuaFn(lua, "e_clearMessages", //
        [this] { clearMessages(); })
        .doc("Remove all previously scheduled messages.");
}

template <typename T>
auto HexagonGame::makeLuaAccessor(T& obj, const std::string& prefix)
{
    return [this, &obj, prefix](const std::string& name, auto pmd,
               const std::string& getterDesc, const std::string& setterDesc)
    {
        using Type = std::decay_t<decltype(obj.*pmd)>;

        const std::string getterString = prefix + "_get" + name;
        const std::string setterString = prefix + "_set" + name;

        addLuaFn(lua, getterString, //
            [pmd, &obj]() -> Type { return obj.*pmd; })
            .doc(getterDesc);

        addLuaFn(lua, setterString, //
            [pmd, &obj](Type mValue) { obj.*pmd = mValue; })
            .arg("value")
            .doc(setterDesc);
    };
}

void HexagonGame::initLua_LevelControl()
{
    addLuaFn(lua, "l_overrideScore", //
        [this](const std::string& mVar)
        {
            try
            {
                levelStatus.scoreOverride = mVar;
                levelStatus.scoreOverridden = true;
                // Make sure we're not passing in a string
                lua.executeCode("if (type(" + mVar + R"( ) ~= "number") then
								error("Score override must be a number value")
								end
)");
            }
            catch(const std::runtime_error& mError)
            {
                std::cout
                    << "[l_overrideScore] Runtime error on overriding score "
                    << "with level \"" << levelData->name << "\": \n"
                    << mError.what() << '\n'
                    << std::endl;
                if(!Config::getDebug())
                {
                    goToMenu(false /* mSendScores */, true /* mError */);
                }
            };
        })
        .arg("variable")
        .doc(
            "Overrides the default scoring method and determines score based "
            "off the value of `$0`. This allows for custom scoring in levels. "
            "*Avoid using strings, otherwise scores won't sort properly. NOTE: "
            "Your variable must be global for this to work.*");

    addLuaFn(lua, "l_setRotation", //
        [this](float mValue)
        {
            // TODO (P2): might break replays?
            if(backgroundCamera.has_value())
            {
                backgroundCamera->setRotation(mValue);
            }
        })
        .arg("angle")
        .doc("Set the background camera rotation to `$0` degrees.");

    addLuaFn(lua, "l_getRotation", //
        [this]
        {
            // TODO (P2): might break replays?
            return backgroundCamera.has_value()
                       ? backgroundCamera->getRotation()
                       : 0.f;
        })
        .doc("Return the background camera rotation, in degrees.");

    addLuaFn(lua, "l_getOfficial", //
        [] { return Config::getOfficial(); })
        .doc(
            "Return `true` if \"official mode\" is enabled, `false` "
            "otherwise.");
}

void HexagonGame::initLua_StyleControl()
{
    addLuaFn(lua, "s_setStyle", //
        [this](const std::string& mId)
        {
            styleData = assets.getStyleData(levelData->packId, mId);
            styleData.computeColors();
        })
        .arg("styleId")
        .doc(
            "Set the currently active style to the style with id `$0`. Styles "
            "can be defined as `.json` files in the `<pack>/Styles/` folder.");
}

void HexagonGame::initLua_WallCreation()
{
    addLuaFn(lua, "w_wall", //
        [this](int mSide, float mThickness)
        {
            timeline.append_do(
                [=, this]
                {
                    createWall(mSide, mThickness, SpeedData{getSpeedMultDM()},
                        SpeedData{} /* curve */, 0.f /* hueMod */);
                });
        })
        .arg("side")
        .arg("thickness")
        .doc(
            "Create a new wall at side `$0`, with thickness `$1`. The speed of "
            "the wall will be calculated by using the speed multiplier, "
            "adjusted for the current difficulty multiplier.");

    addLuaFn(lua, "w_wallAdj", //
        [this](int mSide, float mThickness, float mSpeedAdj)
        {
            timeline.append_do(
                [=, this]
                {
                    createWall(mSide, mThickness,
                        SpeedData{mSpeedAdj * getSpeedMultDM()},
                        SpeedData{} /* curve */, 0.f /* hueMod */);
                });
        })
        .arg("side")
        .arg("thickness")
        .arg("speedMult")
        .doc(
            "Create a new wall at side `$0`, with thickness `$1`. The speed of "
            "the wall will be calculated by using the speed multiplier, "
            "adjusted for the current difficulty multiplier, and finally "
            "multiplied by `$2`.");

    addLuaFn(lua, "w_wallAcc", //
        [this](int mSide, float mThickness, float mSpeedAdj,
            float mAcceleration, float mMinSpeed, float mMaxSpeed)
        {
            timeline.append_do(
                [=, this]
                {
                    createWall(mSide, mThickness,
                        SpeedData{mSpeedAdj * getSpeedMultDM(),
                            mAcceleration / (std::pow(difficultyMult, 0.65f)),
                            mMinSpeed * getSpeedMultDM(),
                            mMaxSpeed * getSpeedMultDM()},
                        SpeedData{} /* curve */, 0.f /* hueMod */);
                });
        })
        .arg("side")
        .arg("thickness")
        .arg("speedMult")
        .arg("acceleration")
        .arg("minSpeed")
        .arg("maxSpeed")
        .doc(
            "Create a new wall at side `$0`, with thickness `$1`. The speed of "
            "the wall will be calculated by using the speed multiplier, "
            "adjusted for the current difficulty multiplier, and finally "
            "multiplied by `$2`. The wall will have a speed acceleration value "
            "of `$3`. The minimum and maximum speed of the wall are bounded by "
            "`$4` and `$5`, adjusted  for the current difficulty multiplier.");

    addLuaFn(lua, "w_wallHModSpeedData", //
        [this](float mHMod, int mSide, float mThickness, float mSAdj,
            float mSAcc, float mSMin, float mSMax, bool mSPingPong)
        {
            timeline.append_do(
                [=, this]
                {
                    createWall(mSide, mThickness,
                        SpeedData{mSAdj * getSpeedMultDM(), mSAcc, mSMin, mSMax,
                            mSPingPong},
                        SpeedData{} /* curve */, mHMod);
                });
        })
        .arg("hueModifier")
        .arg("side")
        .arg("thickness")
        .arg("speedMult")
        .arg("acceleration")
        .arg("minSpeed")
        .arg("maxSpeed")
        .arg("pingPong")
        .doc(
            "Create a new wall at side `$1`, with thickness `$2`. The speed of "
            "the wall will be calculated by using the speed multiplier, "
            "adjusted for the current difficulty multiplier, and finally "
            "multiplied by `$3`. The wall will have a speed acceleration value "
            "of `$4`. The minimum and maximum speed of the wall are bounded by "
            "`$5` and `$6`, adjusted  for the current difficulty multiplier. "
            "The hue of the wall will be adjusted by `$0`. If `$7` is enabled, "
            "the wall will accelerate back and forth between its minimum and "
            "maximum speed.");

    addLuaFn(lua, "w_wallHModCurveData", //
        [this](float mHMod, int mSide, float mThickness, float mCAdj,
            float mCAcc, float mCMin, float mCMax, bool mCPingPong)
        {
            timeline.append_do(
                [=, this]
                {
                    createWall(mSide, mThickness, SpeedData{getSpeedMultDM()},
                        SpeedData{mCAdj, mCAcc, mCMin, mCMax, mCPingPong},
                        mHMod);
                });
        })
        .arg("hueModifier")
        .arg("side")
        .arg("thickness")
        .arg("curveSpeedMult")
        .arg("curveAcceleration")
        .arg("curveMinSpeed")
        .arg("curveMaxSpeed")
        .arg("pingPong")
        .doc(
            "Create a new curving wall at side `$1`, with thickness `$2`. The "
            "curving speed of the wall will be calculated by using the speed "
            "multiplier, adjusted for the current difficulty multiplier, and "
            "finally multiplied by `$3`. The wall will have a curving speed "
            "acceleration value of `$4`. The minimum and maximum curving speed "
            "of the wall are bounded by `$5` and `$6`, adjusted  for the "
            "current difficulty multiplier. The hue of the wall will be "
            "adjusted by `$0`. If `$7` is enabled, the wall will accelerate "
            "back and forth between its minimum and maximum speed.");
}

void HexagonGame::initLua_Steam()
{
    addLuaFn(lua, "steam_unlockAchievement", //
        [this](const std::string& mId)
        {
            if(inReplay())
            {
                // Do not unlock achievements while watching a replay.
                return;
            }

            if(steamManager != nullptr && Config::getOfficial())
            {
                steamManager->unlock_achievement(mId);
            }
        })
        .arg("achievementId")
        .doc("Unlock the Steam achievement with id `$0`.");
}

// These are all deprecated functions that are only being kept for the sake of
// lessening the impact of incompatibility. Pack Developers have time to change
// to the new functions before they get removed permanently
void HexagonGame::initLua_Deprecated()
{
    addLuaFn(lua, "u_kill", //
        [this]
        {
            raiseWarning("u_kill",
                "This function will be removed in a future version of Open "
                "Hexagon. Please replace all occurrences of this function with "
                "\"t_kill\" in your level files.");
            timeline.append_do([this] { death(true); });
        })
        .doc(
            "*Add to the main timeline*: kill the player. "
            "**This function is deprecated and will be removed in a future "
            "version. Please use t_kill instead!**");

    addLuaFn(lua, "u_eventKill", //
        [this]
        {
            raiseWarning("u_eventKill",
                "This function will be removed in a future version of Open "
                "Hexagon. Please replace all occurrences of this function with "
                "\"e_kill\" in your level files.");
            eventTimeline.append_do([this] { death(true); });
        })
        .doc(
            "*Add to the event timeline*: kill the player. "
            "**This function is deprecated and will be removed in a future "
            "version. Please use e_kill instead!**");

    addLuaFn(lua, "u_playSound", //
        [this](const std::string& mId)
        {
            raiseWarning("u_playSound",
                "This function will be removed in a future version of Open "
                "Hexagon. Please replace all occurrences of this function with "
                "\"a_playSound\" in your level files.");
            playSoundOverride(mId);
        })
        .arg("soundId")
        .doc(
            "Play the sound with id `$0`. The id must be registered in "
            "`assets.json`, under `\"soundBuffers\"`. "
            "**This function is deprecated and will be removed in a future "
            "version. Please use a_playSound instead!**");

    addLuaFn(lua, "u_playPackSound", //
        [this](const std::string& fileName)
        {
            raiseWarning("u_playPackSound",
                "This function will be removed in a future version of Open "
                "Hexagon. Please replace all occurrences of this function with "
                "\"a_playPackSound\" in your level files.");
            playPackSoundOverride(getPackId(), fileName);
        })
        .arg("fileName")
        .doc(
            "Dives into the `Sounds` folder of the current level pack and "
            "plays the specified file `$0`. "
            "**This function is deprecated and will be removed in a future "
            "version. Please use a_playPackSound instead!**");

    addLuaFn(lua, "e_eventStopTime", //
        [this](double mDuration)
        {
            raiseWarning("e_eventStopTime",
                "This function will be removed in a future version of Open "
                "Hexagon. Please replace all occurrences of this function with "
                "\"e_stopTime\" in your level files.");
            eventTimeline.append_do([=, this]
                { status.pauseTime(ssvu::getFTToSeconds(mDuration)); });
        })
        .arg("duration")
        .doc(
            "*Add to the event timeline*: pause the game timer for `$0` frames "
            "(under the assumption of a 60 FPS frame rate). "
            "**This function is deprecated and will be removed in a future "
            "version. Please use e_stopTime instead!**");

    addLuaFn(lua, "e_eventStopTimeS", //
        [this](double mDuration)
        {
            raiseWarning("e_eventStopTimeS",
                "This function will be removed in a future version of Open "
                "Hexagon. Please replace all occurrences of this function with "
                "\"e_stopTimeS\" in your level files.");
            eventTimeline.append_do([=, this] { status.pauseTime(mDuration); });
        })
        .arg("duration")
        .doc(
            "*Add to the event timeline*: pause the game timer for `$0` "
            "seconds. "
            "**This function is deprecated and will be removed in a future "
            "version. Please use e_stopTimeS instead!**");

    addLuaFn(lua, "e_eventWait",
        [this](double mDuration)
        {
            raiseWarning("e_eventWait",
                "This function will be removed in a future version of Open "
                "Hexagon. Please replace all occurrences of this function with "
                "\"e_wait\" in your level files.");
            eventTimeline.append_wait_for_sixths(mDuration);
        })
        .arg("duration")
        .doc(
            "*Add to the event timeline*: wait for `$0` frames (under the "
            "assumption of a 60 FPS frame rate). "
            "**This function is deprecated and will be removed in a future "
            "version. Please use e_wait instead!**");

    addLuaFn(lua, "e_eventWaitS", //
        [this](double mDuration)
        {
            raiseWarning("e_eventWaitS",
                "This function will be removed in a future version of Open "
                "Hexagon. Please replace all occurrences of this function with "
                "\"e_waitS\" in your level files.");
            eventTimeline.append_wait_for_seconds(mDuration);
        })
        .arg("duration")
        .doc(
            "*Add to the event timeline*: wait for `$0` seconds. "
            "**This function is deprecated and will be removed in a future "
            "version. Please use e_waitS instead!**");

    addLuaFn(lua, "e_eventWaitUntilS", //
        [this](double mDuration)
        {
            raiseWarning("e_eventWaitUntilS",
                "This function will be removed in a future version of Open "
                "Hexagon. Please replace all occurrences of this function with "
                "\"e_waitUntilS\" in your level files.");
            eventTimeline.append_wait_until_fn(
                [this, mDuration]
                {
                    return status.getLevelStartTP() +
                           std::chrono::milliseconds(
                               static_cast<int>(mDuration * 1000.0));
                });
        })
        .arg("duration")
        .doc(
            "*Add to the event timeline*: wait until the timer reaches `$0` "
            "seconds. "
            "**This function is deprecated and will be removed in a future "
            "version. Please use e_waitUntilS instead!**");

    addLuaFn(lua, "m_messageAdd", //
        [this](const std::string& mMsg, double mDuration)
        {
            raiseWarning("m_messageAdd",
                "This function will be removed in a future version of Open "
                "Hexagon. Please replace all occurrences of this function with "
                "\"e_messageAdd\" in your level files and common.lua.");
            eventTimeline.append_do(
                [=, this]
                {
                    if(firstPlay)
                    {
                        addMessage(mMsg, mDuration, /* mSoundToggle */ true);
                    }
                });
        })
        .arg("message")
        .arg("duration")
        .doc(
            "*Add to the event timeline*: print a message with text `$0` for "
            "`$1` seconds. The message will only be printed during the first "
            "run of the level. "
            "**This function is deprecated and will be removed in a future "
            "version. Please use e_messageAdd instead!**");

    addLuaFn(lua, "m_messageAddImportant", //
        [this](const std::string& mMsg, double mDuration)
        {
            raiseWarning("m_messageAddImportant",
                "This function will be removed in a future version of Open "
                "Hexagon. Please replace all occurrences of this function with "
                "\"e_messageAddImportant\" in your level files and "
                "common.lua.");
            eventTimeline.append_do([=, this]
                { addMessage(mMsg, mDuration, /* mSoundToggle */ true); });
        })
        .arg("message")
        .arg("duration")
        .doc(
            "*Add to the event timeline*: print a message with text `$0` for "
            "`$1` seconds. The message will be printed during every run of the "
            "level. "
            "**This function is deprecated and will be removed in a future "
            "version. Please use e_messageAddImportant instead!**");

    addLuaFn(lua, "m_messageAddImportantSilent",
        [this](const std::string& mMsg, double mDuration)
        {
            raiseWarning("m_messageAddImportantSilent",
                "This function will be removed in a future version of Open "
                "Hexagon. Please replace all occurrences of this function with "
                "\"e_messageAddImportantSilent\" in your level files.");
            eventTimeline.append_do([=, this]
                { addMessage(mMsg, mDuration, /* mSoundToggle */ false); });
        })
        .arg("message")
        .arg("duration")
        .doc(
            "*Add to the event timeline*: print a message with text `$0` for "
            "`$1` seconds. The message will only be printed during every "
            "run of the level, and will not produce any sound. "
            "**This function is deprecated and will be removed in a future "
            "version. Please use e_messageAddImportantSilent instead!**");

    addLuaFn(lua, "m_clearMessages", //
        [this]
        {
            raiseWarning("m_clearMessages",
                "This function will be removed in a future version of Open "
                "Hexagon. Please replace all occurrences of this function with "
                "\"e_clearMessages\" in your level files.");
            clearMessages();
        })
        .doc(
            "Remove all previously scheduled messages. "
            "**This function is deprecated and will be removed in a future "
            "version. Please use e_clearMessages instead!**");
}

void HexagonGame::initLua()
{
    LuaScripting::init(lua, rng, false /* inMenu */, cwManager, levelStatus,
        status, styleData);

    initLua_Utils();
    initLua_AudioControl();
    initLua_MainTimeline();
    initLua_EventTimeline();
    initLua_LevelControl();
    initLua_StyleControl();
    initLua_WallCreation();
    initLua_Steam();
    initLua_Deprecated();
}

void HexagonGame::runLuaFile(const std::string& mFileName)
try
{
    Utils::runLuaFile(lua, mFileName);
}
catch(...)
{
    if(!Config::getDebug())
    {
        goToMenu(false /* mSendScores */, true /* mError */);
    }
}

void HexagonGame::initLuaAndPrintDocs()
{
    initLua();
    LuaScripting::printDocs();
}

void HexagonGame::luaExceptionLippincottHandler(const std::string& mName)
try
{
    throw;
}
catch(const std::runtime_error& mError)
{
    std::cout << "[runLuaFunctionIfExists] Runtime error on \"" << mName
              << "\" with level \"" << levelData->name << "\": \n"
              << mError.what() << '\n'
              << std::endl;

    if(!Config::getDebug())
    {
        goToMenu(false /* mSendScores */, true /* mError */);
    }
}
catch(...)
{
    std::cout << "[runLuaFunctionIfExists] Unknown runtime error on \"" << mName
              << "\" with level \"" << levelData->name << "\": \n"
              << '\n'
              << std::endl;

    if(!Config::getDebug())
    {
        goToMenu(false /* mSendScores */, true /* mError */);
    }
}

} // namespace hg
