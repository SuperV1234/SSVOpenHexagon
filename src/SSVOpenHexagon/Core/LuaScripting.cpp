// Copyright (c) 2013-2020 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: https://opensource.org/licenses/AFL-3.0

#include "SSVOpenHexagon/Core/LuaScripting.hpp"

#include "SSVOpenHexagon/Global/Assert.hpp"
#include "SSVOpenHexagon/Global/Version.hpp"
#include "SSVOpenHexagon/Utils/LuaWrapper.hpp"
#include "SSVOpenHexagon/Utils/LuaMetadata.hpp"
#include "SSVOpenHexagon/Utils/LuaMetadataProxy.hpp"
#include "SSVOpenHexagon/Utils/ScopeGuard.hpp"
#include "SSVOpenHexagon/Data/LevelStatus.hpp"
#include "SSVOpenHexagon/Data/StyleData.hpp"
#include "SSVOpenHexagon/Core/RandomNumberGenerator.hpp"
#include "SSVOpenHexagon/Core/HGStatus.hpp"
#include "SSVOpenHexagon/Components/CCustomWallHandle.hpp"
#include "SSVOpenHexagon/Components/CCustomWallManager.hpp"
#include "SSVOpenHexagon/Utils/Concat.hpp"
#include "SSVOpenHexagon/Utils/TypeWrapper.hpp"

#include <SSVUtils/Core/Log/Log.hpp>

#include <cstddef>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace hg::LuaScripting {

template <typename F>
Utils::LuaMetadataProxy addLuaFn(
    Lua::LuaContext& lua, const std::string& name, F&& f)
{
    // TODO (P2): does this handle duplicates properly? Both menu and game call
    // the same thing.

    lua.writeVariable(name, std::forward<F>(f));
    return Utils::LuaMetadataProxy{
        Utils::TypeWrapper<F>{}, getMetadata(), name};
}

template <typename T>
auto makeLuaAccessor(Lua::LuaContext& lua, T& obj, const std::string& prefix)
{
    return [&lua, &obj, prefix](const std::string& name, auto pmd,
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

static void initRandom(Lua::LuaContext& lua, random_number_generator& rng)
{
    const auto rndReal = [&rng]() -> float
    { return rng.get_real<float>(0, 1); };

    const auto rndIntUpper = [&rng](int upper) -> float
    { return rng.get_int<int>(1, upper); };

    const auto rndInt = [&rng](int lower, int upper) -> float
    { return rng.get_int<int>(lower, upper); };

    addLuaFn(lua, "u_rndReal", rndReal)
        .doc("Return a random real number in the [0; 1] range.");

    addLuaFn(lua, "u_rndIntUpper", rndIntUpper)
        .arg("upper")
        .doc("Return a random integer number in the [1; `$0`] range.");

    addLuaFn(lua, "u_rndInt", rndInt)
        .arg("lower")
        .arg("upper")
        .doc("Return a random integer number in the [`$0`; `$1`] range.");

    addLuaFn(lua, "u_rndSwitch",
        [rndReal, rndIntUpper, rndInt](int mode, int lower, int upper) -> float
        {
            if(mode == 0)
            {
                return rndReal();
            }

            if(mode == 1)
            {
                return rndIntUpper(upper);
            }

            if(mode == 2)
            {
                return rndInt(lower, upper);
            }

            SSVOH_ASSERT(false);
            return 0;
        })
        .arg("mode")
        .arg("lower")
        .arg("upper")
        .doc(
            "Internal replacement for `math.random`. Calls `u_rndReal()` with "
            "`$0 == 0`, `u_rndUpper($2)` with `$0 == 1`, and `u_rndInt($1, "
            "$2)` with `$0 == 2`.");

    // ------------------------------------------------------------------------
    // Register Lua function to get random seed for the current attempt:
    addLuaFn(lua, "u_getAttemptRandomSeed", //
        [&rng] { return rng.seed(); })
        .doc(
            "Obtain the current random seed, automatically generated at the "
            "beginning of the level. `math.randomseed` is automatically "
            "initialized with the result of this function at the beginning of "
            "a level.");
}

static void redefineIoOpen(Lua::LuaContext& lua)
try
{
    lua.executeCode(
        "local open = io.open; io.open = function(filename, mode) return "
        "open(filename, mode == \"rb\" and mode or \"r\"); end");
}
catch(...)
{
    ssvu::lo("hg::LuaScripting::redefineIoOpen")
        << "Failure to redefine Lua's `io.open` function\n";

    throw;
}

static void redefineRandom(Lua::LuaContext& lua)
try
{
    lua.executeCode(R"(math.random = function(a, b)
    if a == nil and b == nil then
        return u_rndSwitch(0, 0, 0)
    elseif b == nil then
        return u_rndSwitch(1, 0, a)
    else
        return u_rndSwitch(2, a, b)
    end
end
)");
}
catch(...)
{
    ssvu::lo("hg::LuaScripting::redefineRandom")
        << "Failure to redefine Lua's `math.random` function\n";

    throw;
}

static void destroyMaliciousFunctions(Lua::LuaContext& lua)
{
    // This destroys the "os" library completely. This library is capable of
    // file manipulation, running shell commands, and messing up the replay
    // system completely. os.execute(), one of the functions in this library,
    // can be used to create malware and is capable of destroying computers.
    lua.clearVariable("os");

    // This destroys some of the "io" functions completely.
    // This library is dedicated to manipulating files and their contents,
    // which can be used maliciously.
    lua.clearVariable("io.popen");
    lua.clearVariable("io.flush");
    lua.clearVariable("io.write");
    lua.clearVariable("io.setvbuf");

    // This destroys the "debug" library completely. The debug library is next
    // to useless in Open Hexagon (considering we have our own methods of
    // debugging), and it allows people to access destroyed modules with the
    // getregistry function.
    lua.clearVariable("debug");

    // This function allows pack developers to set the seed in Lua.
    lua.clearVariable("math.randomseed");

    // These functions are being deleted as they can assist in restoring
    // destroyed modules. However, we cannot destroy the whole library as
    // the other functions are needed for the "require" function to work
    // properly.
    lua.clearVariable("package.loadlib");
    lua.clearVariable("package.searchpath");
}

static void initUtils(Lua::LuaContext& lua, const bool inMenu)
{
    addLuaFn(lua, "u_inMenu", //
        [inMenu] { return inMenu; })
        .doc(
            "Returns `true` if the script is being executed in the menu, "
            "`false` otherwise.");

    addLuaFn(lua, "u_getVersionMajor", //
        [] { return GAME_VERSION.major; })
        .doc("Returns the major of the current version of the game");

    addLuaFn(lua, "u_getVersionMinor", //
        [] { return GAME_VERSION.minor; })
        .doc("Returns the minor of the current version of the game");

    addLuaFn(lua, "u_getVersionMicro", //
        [] { return GAME_VERSION.micro; })
        .doc("Returns the micro of the current version of the game");

    addLuaFn(lua, "u_getVersionString", //
        []() -> std::string { return GAME_VERSION_STR; })
        .doc("Returns the string representing the current version of the game");
}

static void initCustomWalls(Lua::LuaContext& lua, CCustomWallManager& cwManager)
{
    addLuaFn(lua, "cw_create", //
        [&cwManager]() -> CCustomWallHandle
        { return cwManager.create([](CCustomWall&) {}); })
        .doc("Create a new custom wall and return a integer handle to it.");

    addLuaFn(lua, "cw_createDeadly", //
        [&cwManager]() -> CCustomWallHandle {
            return cwManager.create(
                [](CCustomWall& cw) { cw.setDeadly(true); });
        })
        .doc(
            "Create a new deadly custom wall and return a integer handle to "
            "it.");

    addLuaFn(lua, "cw_createNoCollision", //
        [&cwManager]() -> CCustomWallHandle {
            return cwManager.create(
                [](CCustomWall& cw) { cw.setCanCollide(false); });
        })
        .doc(
            "Create a new custom wall without collision and return a integer "
            "handle to it.");

    addLuaFn(lua, "cw_destroy", //
        [&cwManager](CCustomWallHandle cwHandle)
        { cwManager.destroy(cwHandle); })
        .arg("cwHandle")
        .doc("Destroy the custom wall represented by `$0`.");

    addLuaFn(lua, "cw_setVertexPos", //
        [&cwManager](
            CCustomWallHandle cwHandle, int vertexIndex, float x, float y) {
            cwManager.setVertexPos(cwHandle, vertexIndex, sf::Vector2f{x, y});
        })
        .arg("cwHandle")
        .arg("vertexIndex")
        .arg("x")
        .arg("y")
        .doc(
            "Given the custom wall represented by `$0`, set the position of "
            "its vertex with index `$1` to `{$2, $3}`.");

    addLuaFn(lua, "cw_moveVertexPos", //
        [&cwManager](
            CCustomWallHandle cwHandle, int vertexIndex, float x, float y) {
            cwManager.moveVertexPos(cwHandle, vertexIndex, sf::Vector2f{x, y});
        })
        .arg("cwHandle")
        .arg("vertexIndex")
        .arg("offsetX")
        .arg("offsetY")
        .doc(
            "Given the custom wall represented by `$0`, add `{$2, $3}` to the "
            "position of its vertex with index `$1`.");

    addLuaFn(lua, "cw_moveVertexPos4Same", //
        [&cwManager](CCustomWallHandle cwHandle, float x, float y) {
            cwManager.moveVertexPos4Same(cwHandle, sf::Vector2f{x, y});
        })
        .arg("cwHandle")
        .arg("offsetX")
        .arg("offsetY")
        .doc(
            "Given the custom wall represented by `$0`, add `{$1, $2}` to the "
            "position of its vertex with indices `0`, `1`, `2`, and `3`.");

    addLuaFn(lua, "cw_setVertexColor", //
        [&cwManager](CCustomWallHandle cwHandle, int vertexIndex, int r, int g,
            int b, int a) {
            cwManager.setVertexColor(
                cwHandle, vertexIndex, sf::Color(r, g, b, a));
        })
        .arg("cwHandle")
        .arg("vertexIndex")
        .arg("r")
        .arg("g")
        .arg("b")
        .arg("a")
        .doc(
            "Given the custom wall represented by `$0`, set the color of "
            "its vertex with index `$1` to `{$2, $3, $4, $5}`.");

    addLuaFn(lua, "cw_setVertexPos4",            //
        [&cwManager](CCustomWallHandle cwHandle, //
            float x0, float y0,                  //
            float x1, float y1,                  //
            float x2, float y2,                  //
            float x3, float y3)
        {
            cwManager.setVertexPos4(cwHandle, //
                sf::Vector2f{x0, y0},         //
                sf::Vector2f{x1, y1},         //
                sf::Vector2f{x2, y2},         //
                sf::Vector2f{x3, y3});
        })
        .arg("cwHandle")
        .arg("x0")
        .arg("y0")
        .arg("x1")
        .arg("y1")
        .arg("x2")
        .arg("y2")
        .arg("x3")
        .arg("y3")
        .doc(
            "Given the custom wall represented by `$0`, set the position of "
            "its vertex with index `0` to `{$1, $2}`, index `1` to `{$3, $4}`, "
            "index `2` to `{$5, $6}`, index `3` to `{$7, $8}`. More efficient "
            "than invoking `cw_setVertexPos` four times in a row.");

    addLuaFn(lua, "cw_setVertexColor4",          //
        [&cwManager](CCustomWallHandle cwHandle, //
            int r0, int g0, int b0, int a0,      //
            int r1, int g1, int b1, int a1,      //
            int r2, int g2, int b2, int a2,      //
            int r3, int g3, int b3, int a3)
        {
            cwManager.setVertexColor4(cwHandle, //
                sf::Color(r0, g0, b0, a0),      //
                sf::Color(r1, g1, b1, a1),      //
                sf::Color(r2, g2, b2, a2),      //
                sf::Color(r3, g3, b3, a3));
        })
        .arg("cwHandle")
        .arg("r0")
        .arg("g0")
        .arg("b0")
        .arg("a0")
        .arg("r1")
        .arg("g1")
        .arg("b1")
        .arg("a1")
        .arg("r2")
        .arg("g2")
        .arg("b2")
        .arg("a2")
        .arg("r3")
        .arg("g3")
        .arg("b3")
        .arg("a3")
        .doc(
            "Given the custom wall represented by `$0`, set the color of "
            "its vertex with index `0` to `{$1, $2, $3, $4}`, index `1` to "
            "`{$5, $6, $7, $8}`, index `2` to `{$9, $10, $11, $12}`, index `3` "
            "to `{$13, $14, $15, $16}`. More efficient than invoking "
            "`cw_setVertexColor` four times in a row.");

    addLuaFn(lua, "cw_setVertexColor4Same", //
        [&cwManager](CCustomWallHandle cwHandle, int r, int g, int b, int a)
        { cwManager.setVertexColor4Same(cwHandle, sf::Color(r, g, b, a)); })
        .arg("cwHandle")
        .arg("r")
        .arg("g")
        .arg("b")
        .arg("a")
        .doc(
            "Given the custom wall represented by `$0`, set the color of "
            "its vertices with indiced `0`, `1`, `2`, and `3' to `{$1, $2, $3, "
            "$4}`. More efficient than invoking `cw_setVertexColor` four times "
            "in a row.");

    addLuaFn(lua, "cw_setCollision", //
        [&cwManager](CCustomWallHandle cwHandle, bool collision)
        { cwManager.setCanCollide(cwHandle, collision); })
        .arg("cwHandle")
        .arg("collision")
        .doc(
            "Given the custom wall represented by `$0`, set the collision "
            "of the custom wall to `$1`. If false, the player cannot die "
            "from this wall and can move through the wall. By default, all "
            "custom walls can collide with the player.");

    addLuaFn(lua, "cw_setDeadly", //
        [&cwManager](CCustomWallHandle cwHandle, bool deadly)
        { cwManager.setDeadly(cwHandle, deadly); })
        .arg("cwHandle")
        .arg("deadly")
        .doc(
            "Given the custom wall represented by `$0`, set wherever "
            "it instantly kills player on touch. This is highly "
            "recommended for custom walls that are either very small "
            "or very thin and should definitively kill the player.");

    addLuaFn(lua, "cw_setKillingSide", //
        [&cwManager](CCustomWallHandle cwHandle, unsigned int side)
        { cwManager.setKillingSide(cwHandle, side); })
        .arg("cwHandle")
        .arg("side")
        .doc(
            "Given the custom wall represented by `$0`, set which "
            "one of its sides should beyond any doubt cause the "
            "death of the player. Acceptable values are `0` to `3`. "
            "In a standard wall, side `0` is the side closer to the center. "
            "This parameter is useless if the custom wall is deadly.");

    addLuaFn(lua, "cw_getCollision", //
        [&cwManager](CCustomWallHandle cwHandle) -> bool
        { return cwManager.getCanCollide(cwHandle); })
        .arg("cwHandle")
        .doc(
            "Given the custom wall represented by `$0`, get whether it can "
            "collide with player or not.");

    addLuaFn(lua, "cw_getDeadly", //
        [&cwManager](CCustomWallHandle cwHandle) -> bool
        { return cwManager.getDeadly(cwHandle); })
        .arg("cwHandle")
        .doc(
            "Given the custom wall represented by `$0`, get whether it "
            "instantly kills the player on touch or not.");

    addLuaFn(lua, "cw_getKillingSide", //
        [&cwManager](CCustomWallHandle cwHandle) -> unsigned int
        { return cwManager.getKillingSide(cwHandle); })
        .arg("cwHandle")
        .doc(
            "Given the custom wall represented by `$0`, get which one of its "
            "sides always causes the death of the player.");

    addLuaFn(lua, "cw_getVertexPos", //
        [&cwManager](CCustomWallHandle cwHandle,
            int vertexIndex) -> std::tuple<float, float>
        {
            const sf::Vector2f& pos =
                cwManager.getVertexPos(cwHandle, vertexIndex);

            return {pos.x, pos.y};
        })
        .arg("cwHandle")
        .arg("vertexIndex")
        .doc(
            "Given the custom wall represented by `$0`, return the position of "
            "its vertex with index `$1`.");

    addLuaFn(lua, "cw_getVertexPos4", //
        [&cwManager](
            CCustomWallHandle cwHandle) -> std::tuple<float, float, float,
                                            float, float, float, float, float>
        {
            const auto& [p0, p1, p2, p3] = cwManager.getVertexPos4(cwHandle);
            return {p0.x, p0.y, p1.x, p1.y, p2.x, p2.y, p3.x, p3.y};
        })
        .arg("cwHandle")
        .doc(
            "Given the custom wall represented by `$0`, return the position of "
            "its vertics with indices `0`, `1`, `2`, and `3`, as a tuple.");

    // TODO (P2): implement
    /*
    addLuaFn(lua, "cw_isOverlappingPlayer", //
        [&cwManager](CCustomWallHandle cwHandle) -> bool {
            return cwManager.isOverlappingPlayer(cwHandle);
        })
        .arg("cwHandle")
        .doc(
            "Return `true` if the custom wall represented by `$0` is "
            "overlapping the player, `false` otherwise.");
    */

    addLuaFn(lua, "cw_clear", //
        [&cwManager] { cwManager.clear(); })
        .doc("Remove all existing custom walls.");
}

static void initLevelControl(
    Lua::LuaContext& lua, LevelStatus& levelStatus, HexagonGameStatus& status)
{
    const auto lsVar = makeLuaAccessor(lua, levelStatus, "l");

    lsVar("SpeedMult", &LevelStatus::speedMult,
        "Gets the speed multiplier of the level. The speed multiplier is "
        "the current speed of the walls. Is incremented by ``SpeedInc`` "
        "every increment and caps at ``speedMax``.",

        "Sets the speed multiplier of the level to `$0`. Changes do not apply "
        "to "
        "all walls immediately, and changes apply as soon as the next wall "
        "is created.");

    lsVar("PlayerSpeedMult", &LevelStatus::playerSpeedMult,
        "Gets the speed multiplier of the player.",

        "Sets the speed multiplier of the player.");

    lsVar("SpeedInc", &LevelStatus::speedInc,
        "Gets the speed increment of the level. This is applied every level "
        "increment to the speed multiplier. Increments are additive.",

        "Sets the speed increment of the level to `$0`.");

    lsVar("SpeedMax", &LevelStatus::speedMax,
        "Gets the maximum speed of the level. This is the highest that speed "
        "can go; speed can not get any higher than this.",

        "Sets the maximum speed of the level to `$0`. Keep in mind that speed "
        "keeps going past the speed max, so setting a higher speed max may "
        "make the speed instantly increase to the max.");

    lsVar("RotationSpeed", &LevelStatus::rotationSpeed,
        "Gets the rotation speed of the level. Is incremented by "
        "``RotationSpeedInc`` every increment and caps at "
        "``RotationSpeedMax``.",

        "Sets the rotation speed of the level to `$0`. Changes apply "
        "immediately.");

    lsVar("RotationSpeedInc", &LevelStatus::rotationSpeedInc,
        "Gets the rotation speed increment of the level. This is "
        "applied every level increment to the rotation speed. "
        "Increments are additive.",

        "Sets the rotation speed increment of the level to `$0`. "
        "Is effective on the next level increment.");

    lsVar("RotationSpeedMax", &LevelStatus::rotationSpeedMax,
        "Gets the maximum rotation speed of the level. This is the "
        "highest that rotation speed can go; rotation speed can not "
        "get any higher than this.",

        "Sets the maximum rotation speed of the level to `$0`. Keep "
        "in mind that rotation speed keeps going past the max, so "
        "setting a higher rotation speed max may make the rotation speed "
        "instantly increase to the max.");

    lsVar("DelayMult", &LevelStatus::delayMult,
        "Gets the delay multiplier of the level. The delay multiplier "
        "is the multiplier used to assist in spacing patterns, especially "
        "in cases of higher / lower speeds.  Is incremented by ``DelayInc`` "
        "every increment and is clamped between ``DelayMin`` and ``DelayMax``",

        "Sets the delay multiplier of the level to `$0`. Changes do not apply "
        "to "
        "patterns immediately, and changes apply as soon as the next pattern "
        "is spawned.");

    lsVar("DelayInc", &LevelStatus::delayInc,
        "Gets the delay increment of the level. This is applied every level "
        "increment to the delay multiplier. Increments are additive.",

        "Sets the delay increment of the level to `$0`.");

    lsVar("DelayMin", &LevelStatus::delayMin,
        "Gets the minimum delay of the level. This is the lowest that delay "
        "can go; delay can not get any lower than this.",

        "Sets the minimum delay of the level to `$0`. Keep in mind that delay "
        "can go below the delay min, so setting a lower delay min may "
        "make the delay instantly decrease to the minimum.");

    lsVar("DelayMax", &LevelStatus::delayMax,
        "Gets the maximum delay of the level. This is the highest that delay "
        "can go; delay can not get any higher than this.",

        "Sets the maximum delay of the level to `$0`. Keep in mind that delay "
        "can go above the delay max, so setting a higher delay max may "
        "make the delay instantly increase to the maximum.");

    lsVar("FastSpin", &LevelStatus::fastSpin,
        "Gets the fast spin of the level. The fast spin is a brief moment that "
        "starts at level incrementation where the rotation increases speed "
        "drastically to try and throw off the player a bit. This speed quickly "
        "(or slowly, depending on the value) decelerates and fades away to the "
        " updated rotation speed.",

        "Sets the fast spin of the level to `$0`. A higher value increases "
        "intensity and duration of the fast spin.");

    lsVar("IncTime", &LevelStatus::incTime,
        "Get the incrementation time (in seconds) of a level. This is the "
        "length "
        "of a \"level\" in an Open Hexagon level (It's ambiguous but hopefully "
        "you understand what that means), and when this duration is reached, "
        "the "
        "level increments.",

        "Set the incrementation time (in seconds) of a level to `$0`.");

    lsVar("PulseMin", &LevelStatus::pulseMin,
        "Gets the minimum value the pulse can be. Pulse gives variety in "
        "the wall speed of the level so the wall speed doesn't feel monotone. "
        "Can also be used to help sync a level up with it's music.",

        "Sets the minimum pulse value to `$0`.");

    lsVar("PulseMax", &LevelStatus::pulseMax,
        "Gets the maximum value the pulse can be. Pulse gives variety in "
        "the wall speed of the level so the wall speed doesn't feel monotone. "
        "Can also be used to help sync a level up with it's music.",

        "Sets the maximum pulse value to `$0`.");

    lsVar("PulseSpeed", &LevelStatus::pulseSpeed,
        "Gets the speed the pulse goes from ``PulseMin`` to ``PulseMax``. "
        "Can also be used to help sync a level up with it's music.",

        "Sets the speed the pulse goes from ``PulseMin`` to ``PulseMax`` by "
        "`$0`. Can also be used to help sync a level up with it's music.");

    lsVar("PulseSpeedR", &LevelStatus::pulseSpeedR,
        "Gets the speed the pulse goes from ``PulseMax`` to ``PulseMin``.",

        "Sets the speed the pulse goes from ``PulseMax`` to ``PulseMin`` by "
        "`$0`. Can also be used to help sync a level up with it's music.");

    lsVar("PulseDelayMax", &LevelStatus::pulseDelayMax,
        "Gets the delay the level has to wait before it begins another pulse "
        "cycle.",

        "Sets the delay the level has to wait before it begins another pulse "
        "cycle with `$0`.");

    lsVar("PulseInitialDelay", &LevelStatus::pulseInitialDelay,
        "Gets the initial delay the level has to wait before it begins the "
        "first pulse cycle.",

        "Sets the initial delay the level has to wait before it begins the "
        "first pulse cycle with `$0`.");

    lsVar("SwapCooldownMult", &LevelStatus::swapCooldownMult,
        "Gets the multiplier that controls the cooldown for the player's 180 "
        "degrees swap mechanic.",

        "Sets the multiplier that controls the cooldown for the player's 180 "
        "degrees swap mechanic to `$0`.");

    lsVar("BeatPulseMax", &LevelStatus::beatPulseMax,
        "Gets the maximum beatpulse size of the polygon in a level. This is "
        "the highest value that the polygon will \"pulse\" in size. Useful for "
        "syncing the level to the music.",

        "Sets the maximum beatpulse size of the polygon in a level to `$0`. "
        "Not to be confused with using this property to resize the polygon, "
        "which you should be using ``RadiusMin``.");

    lsVar("BeatPulseDelayMax", &LevelStatus::beatPulseDelayMax,
        "Gets the delay for how fast the beatpulse pulses in frames (assuming "
        "60 FPS "
        "logic). This paired with ``BeatPulseMax`` will be useful to help sync "
        "a level "
        "with the music that it's playing.",

        "Sets the delay for how fast the beatpulse pulses in `$0` frames "
        "(assuming 60 "
        "FPS Logic).");

    lsVar("BeatPulseInitialDelay", &LevelStatus::beatPulseInitialDelay,
        "Gets the initial delay before beatpulse begins pulsing. This is very "
        "useful "
        "to use at the very beginning of the level to assist syncing the "
        "beatpulse "
        "with the song.",

        "Sets the initial delay before beatpulse begins pulsing to `$0`. "
        "Highly "
        "discouraged to use this here. Use this in your music JSON files.");

    lsVar("BeatPulseSpeedMult", &LevelStatus::beatPulseSpeedMult,
        "Gets how fast the polygon pulses with the beatpulse. This is very "
        "useful "
        "to help keep your level in sync with the music.",

        "Sets how fast the polygon pulses with beatpulse to `$0`.");

    lsVar("RadiusMin", &LevelStatus::radiusMin,
        "Gets the minimum radius of the polygon in a level. This is used to "
        "determine the absolute size of the polygon in the level.",

        "Sets the minimum radius of the polygon to `$0`. Use this to set the "
        "size of the polygon in the level, not ``BeatPulseMax``.");

    lsVar("WallSkewLeft", &LevelStatus::wallSkewLeft,
        "Gets the Y axis offset of the top left vertex in all walls.",

        "Sets the Y axis offset of the top left vertex to `$0` in all newly "
        "generated "
        "walls. If you would like to have more individual control of the wall "
        "vertices, "
        "please use the custom walls system under the prefix ``cw_``.");

    lsVar("WallSkewRight", &LevelStatus::wallSkewRight,
        "Gets the Y axis offset of the top right vertex in all walls.",

        "Sets the Y axis offset of the top right vertex to `$0` in all newly "
        "generated "
        "walls. If you would like to have more individual control of the wall "
        "vertices, "
        "please use the custom walls system under the prefix ``cw_``.");

    lsVar("WallAngleLeft", &LevelStatus::wallAngleLeft,
        "Gets the X axis offset of the top left vertex in all walls.",

        "Sets the X axis offset of the top left vertex to `$0` in all newly "
        "generated "
        "walls. If you would like to have more individual control of the wall "
        "vertices, "
        "please use the custom walls system under the prefix ``cw_``.");

    lsVar("WallAngleRight", &LevelStatus::wallAngleRight,
        "Gets the X axis offset of the top right vertex in all walls.",

        "Sets the X axis offset of the top right vertex to `$0` in all newly "
        "generated "
        "walls. If you would like to have more individual control of the wall "
        "vertices, "
        "please use the custom walls system under the prefix ``cw_``.");

    lsVar("WallSpawnDistance", &LevelStatus::wallSpawnDistance,
        "Gets the distance at which standard walls spawn.",

        "Sets how far away the walls can spawn from the center. Higher "
        "values make walls spawn farther away, and will increase the "
        "player's wait for incoming walls.");

    lsVar("3dRequired", &LevelStatus::_3DRequired,
        "Gets whether 3D must be enabled in order to have a valid score in "
        "this level. "
        "By default, this value is ``false``.",

        "Sets whether 3D must be enabled to `$0` to have a valid score. Only "
        "set this "
        "to ``true`` if your level relies on 3D effects to work as intended.");

    // Commenting this one out. This property seems to have NO USE in the actual
    // game itself. lsVar("3dEffectMultiplier",
    // &LevelStatus::_3dEffectMultiplier);

    lsVar("CameraShake", &LevelStatus::cameraShake,
        "Gets the intensity of the camera shaking in a level.",

        "Sets the intensity of the camera shaking in a level to `$0`. This "
        "remains "
        "permanent until you either set this to 0 or the player dies.");

    lsVar("Sides", &LevelStatus::sides,
        "Gets the current number of sides on the polygon in a level.",

        "Sets the current number of sides on the polygon to `$0`. This change "
        "happens "
        "immediately and previously spawned walls will not adjust to the new "
        "side count.");

    lsVar("SidesMax", &LevelStatus::sidesMax,
        "Gets the maximum range that the number of sides can possibly be at "
        "random. "
        "``enableRndSideChanges`` must be enabled for this property to have "
        "any use.",

        "Sets the maximum range that the number of sides can possibly be to "
        "`$0`.");

    lsVar("SidesMin", &LevelStatus::sidesMin,
        "Gets the minimum range that the number of sides can possibly be at "
        "random. "
        "``enableRndSideChanges`` must be enabled for this property to have "
        "any use.",

        "Sets the minimum range that the number of sides can possibly be to "
        "`$0`.");

    lsVar("SwapEnabled", &LevelStatus::swapEnabled,
        "Gets whether the swap mechanic is enabled for a level. By default, "
        "this is "
        "set to ``false``.",

        "Sets the swap mechanic's availability to `$0`.");

    lsVar("TutorialMode", &LevelStatus::tutorialMode,
        "Gets whether tutorial mode is enabled. In tutorial mode, players are "
        "granted "
        "invincibility from dying to walls. This mode is typically enabled "
        "whenever a "
        "pack developer needs to demonstrate a new concept to the player so "
        "that way "
        "they can easily learn the new mechanic/concept. This invincibility "
        "will not "
        "count towards invalidating a score, but it's usually not important to "
        "score "
        "on a tutorial level. By default, this is set to ``false``.",

        "Sets tutorial mode to `$0`. Remember, only enable this if you need to "
        "demonstrate "
        "a new concept for players to learn, or use it as a gimmick to a "
        "level.");

    lsVar("IncEnabled", &LevelStatus::incEnabled,
        "Gets whether the level can increment or not. This is Open Hexagon's "
        "way of "
        "establishing a difficulty curve in the level and set a sense of "
        "progression "
        "throughout the level. By default, this value is set to ``true``.",

        "Toggles level incrementation to `$0`. Only disable this if you feel "
        "like the "
        "level can not benefit from incrementing in any way.");

    lsVar("DarkenUnevenBackgroundChunk",
        &LevelStatus::darkenUnevenBackgroundChunk,
        "Gets whether the ``Nth`` panel of a polygon with ``N`` sides "
        "(assuming ``N`` is odd) will be darkened to make styles look more "
        "balanced. By default, this value is set to ``true``, but there can be "
        "styles where having this darkened panel can look very unpleasing.",

        "Sets the darkened panel to `$0`.");

    lsVar("ManualPulseControl", &LevelStatus::manualPulseControl,
        "Gets whether the pulse effect is being controlled manually via Lua or "
        "automatically by the C++ engine.",

        "Sets whether the pulse effect is being controlled manually via Lua or "
        "automatically by the C++ engine to `$0`.");

    lsVar("ManualBeatPulseControl", &LevelStatus::manualBeatPulseControl,
        "Gets whether the beat pulse effect is being controlled manually via "
        "Lua or automatically by the C++ engine.",

        "Sets whether the beat  pulse effect is being controlled manually via "
        "Lua or automatically by the C++ engine to `$0`.");

    lsVar("CurrentIncrements", &LevelStatus::currentIncrements,
        "Gets the current amount of times the level has incremented. Very "
        "useful for "
        "keeping track of levels.",

        "Sets the current amount of times the level has incremented to `$0`. "
        "This "
        "function is utterly pointless to use unless you are tracking this "
        "variable.");

    addLuaFn(lua, "l_enableRndSideChanges", //
        [&levelStatus](bool mValue)
        { levelStatus.rndSideChangesEnabled = mValue; })
        .arg("enabled")
        .doc(
            "Toggles random side changes to `$0`, (not) allowing sides to "
            "change "
            "between ``SidesMin`` and ``SidesMax`` inclusively every level "
            "increment.");

    addLuaFn(lua, "l_addTracked", //
        [&levelStatus](const std::string& mVar, const std::string& mName)
        { levelStatus.trackedVariables[mVar] = mName; })
        .arg("variable")
        .arg("name")
        .doc(
            "Add or set the variable `$0` to the list of tracked variables, "
            "with name `$1`. Tracked variables are displayed in game, below "
            "the game timer. *NOTE: Your variable must be global for this to "
            "work.*");

    addLuaFn(lua, "l_removeTracked", //
        [&levelStatus](const std::string& mVar)
        { levelStatus.trackedVariables.erase(mVar); })
        .arg("variable")
        .doc("Remove the variable `$0` from the list of tracked variables");

    addLuaFn(lua, "l_clearTracked", //
        [&levelStatus] { levelStatus.trackedVariables.clear(); })
        .doc("Clears all tracked variables.");

    addLuaFn(lua, "l_getLevelTime", //
        [&status] { return status.getTimeSeconds(); })
        .doc("Get the current game timer value, in seconds.");

    addLuaFn(lua, "l_resetTime", //
        [&status] { status.resetTime(); })
        .doc(
            "Resets the lever time to zero, also resets increment time and "
            "pause time.");

    const auto sVar = makeLuaAccessor(lua, status, "l");

    sVar("Pulse", &HexagonGameStatus::pulse,
        "Gets the current pulse value, which will vary between "
        "`l_getPulseMin()` and `l_getPulseMax()` unless manually overridden.",

        "Sets the current pulse value to `$0`.");

    sVar("PulseDirection", &HexagonGameStatus::pulseDirection,
        "Gets the current pulse direction value, which will either be `-1` or "
        "`1` unless manually overridden.",

        "Sets the current pulse direction value to `$0`. Valid choices are "
        "`-1` or `1`.");

    sVar("PulseDelay", &HexagonGameStatus::pulseDelay,
        "Gets the current pulse delay value, which will vary between "
        "`0` and `l_getPulseDelayMax()` unless manually overridden.",

        "Sets the current pulse delay value to `$0`.");

    sVar("BeatPulse", &HexagonGameStatus::beatPulse,
        "Gets the current beat pulse value, which will vary between `0` and "
        "`l_getBeatPulseMax()` unless manually overridden.",

        "Sets the current beat pulse value to `$0`.");

    sVar("BeatPulseDelay", &HexagonGameStatus::beatPulseDelay,
        "Gets the current beat pulse delay value, which will vary between "
        "`0` and `l_getBeatPulseDelayMax()` unless manually overridden.",

        "Sets the current beat pulse delay value to `$0`.");
}

static void initStyleControl(Lua::LuaContext& lua, StyleData& styleData)
{
    const auto sdVar = makeLuaAccessor(lua, styleData, "s");

    sdVar("HueMin", &StyleData::hueMin,
        "Gets the minimum value for the hue range of a level style. The hue "
        "attribute is an important attribute that is dedicated specifically to "
        "all colors that have the ``dynamic`` property enabled.",

        "Sets the minimum value for the hue range to `$0`. Usually you want "
        "this value at 0 to start off at completely red.");

    sdVar("HueMax", &StyleData::hueMax,
        "Gets the maximum value for the hue range of a level style. Only "
        "applies to all colors with the ``dynamic`` property enabled.",

        "Sets the maximum value for the hue range to `$0`. Usually you want "
        "this value at 360 to end off at red, to hopefully loop the colors "
        "around.");

    // backwards-compatible
    sdVar("HueInc", &StyleData::hueIncrement,
        "Alias to ``s_getHueIncrement``. Done for backwards compatibility.",

        "Alias to ``s_setHueIncrement``. Done for backwards compatibility.");

    sdVar("HueIncrement", &StyleData::hueIncrement,
        "Gets how fast the hue increments from ``HueMin`` to ``HueMax``. The "
        "hue value is "
        "added by this value every 1/60th of a second.",

        "Sets how fast the hue increments from ``HueMin`` to ``HueMax`` by "
        "`$0`. Be careful "
        "with high values, as this can make your style induce epileptic "
        "seizures.");

    sdVar("PulseMin", &StyleData::pulseMin,
        "Gets the minimum range for the multiplier of the ``pulse`` attribute "
        "in style colors. "
        "By default, this value is set to 0.",

        "Sets the minimum range for the multiplier of the ``pulse`` attribute "
        "to `$0`.");

    sdVar("PulseMax", &StyleData::pulseMax,
        "Gets the maximum range for the multiplier of the ``pulse`` attribute "
        "in style colors. "
        "By default, this value is set to 0, but ideally it should be set to "
        "1.",

        "Sets the maximum range for the multiplier of the ``pulse`` attribute "
        "to `$0`.");

    // backwards-compatible
    sdVar("PulseInc", &StyleData::pulseIncrement,
        "Alias to ``s_getPulseIncrement``. Done for backwards compatibility.",

        "Alias to ``s_setPulseIncrement``. Done for backwards compatibility.");

    sdVar("PulseIncrement", &StyleData::pulseIncrement,
        "Gets how fast the pulse increments from ``PulseMin`` to ``PulseMax``. "
        "The pulse value is "
        "added by this value every 1/60th of a second.",

        "Sets how fast the pulse increments from ``PulseMin`` to ``PulseMax`` "
        "by `$0`. Be careful "
        "with high values, as this can make your style induce epileptic "
        "seizures.");

    sdVar("HuePingPong", &StyleData::huePingPong,
        "Gets whether the hue should go ``Start-End-Start-End`` or "
        "``Start-End, Start-End`` with "
        "the hue cycling.",

        "Toggles ping ponging in the hue cycling (``Start-End-Start-End``) "
        "with `$0`.");

    sdVar("MaxSwapTime", &StyleData::maxSwapTime,
        "Gets the amount of time that has to pass (in 1/100th of a second) "
        "before the background color offset alternates. "
        "The background colors by default alternate between 0 and 1. By "
        "default, this happens every second.",

        "Sets the amount of time that has to pass (in 1/100th of a second) to "
        "`$0` before the background color alternates.");

    sdVar("3dDepth", &StyleData::_3dDepth,
        "Gets the current amount of 3D layers that are present in the style.",

        "Sets the amount of 3D layers in a style to `$0`.");

    sdVar("3dSkew", &StyleData::_3dSkew,
        "Gets the current value of where the 3D skew is in the style. The Skew "
        "is what gives the 3D effect in the first "
        "place, showing the 3D layers and giving the illusion of 3D in the "
        "game.",

        "Sets the 3D skew at value `$0`.");

    sdVar("3dSpacing", &StyleData::_3dSpacing,
        "Gets the spacing that is done between 3D layers. A higher number "
        "leads to more separation between layers.",

        "Sets the spacing between 3D layers to `$0`.");

    sdVar("3dDarkenMult", &StyleData::_3dDarkenMult,
        "Gets the darkening multiplier applied to the 3D layers in a style. "
        "This is taken from the ``main`` color.",

        "Sets the darkening multiplier to `$0` for the 3D layers.");

    sdVar("3dAlphaMult", &StyleData::_3dAlphaMult,
        "Gets the alpha (transparency) multiplier applied to the 3D layers in "
        "a style. Originally references the "
        "``main`` color.",

        "Sets the alpha multiplier to `$0` for the 3D layers. A higher value "
        "makes the layers more transparent.");

    sdVar("3dAlphaFalloff", &StyleData::_3dAlphaFalloff,
        "Gets the alpha (transparency) multiplier applied to the 3D layers "
        "consecutively in a style. Takes "
        "reference from the ``main`` color.",

        "Sets the alpha multiplier to `$0` for for the 3D layers and applies "
        "them layer after layer. This "
        "property can get finnicky.");

    sdVar("3dPulseMax", &StyleData::_3dPulseMax,
        "Gets the highest value that the ``3DSkew`` can go in a style.",

        "Sets the highest value the ``3DSkew`` can go to `$0`.");

    sdVar("3dPulseMin", &StyleData::_3dPulseMin,
        "Gets the lowest value that the ``3DSkew`` can go in a style.",

        "Sets the lowest value the ``3DSkew`` can go to `$0`.");

    sdVar("3dPulseSpeed", &StyleData::_3dPulseSpeed,
        "Gets how fast the ``3DSkew`` moves between ``3DPulseMin`` and "
        "``3DPulseMax``.",

        "Sets how fast the ``3DSkew`` moves between ``3DPulseMin`` and "
        "``3DPulseMax`` by `$0`.");

    sdVar("3dPerspectiveMult", &StyleData::_3dPerspectiveMult,
        "Gets the 3D perspective multiplier of the style. Works with the "
        "attribute ``3DSpacing`` to space out "
        "layers.",

        "Sets the 3D perspective multiplier to `$0`.");

    sdVar("BGTileRadius", &StyleData::bgTileRadius,
        "Gets the distances of how far the background panels are drawn. By "
        "default, this is a big enough value "
        "so you do not see the border. However, feel free to shrink them if "
        "you'd like.",

        "Sets how far the background panels are drawn to distance `$0`.");

    sdVar("BGColorOffset", &StyleData::BGColorOffset,
        "Gets the offset of the style by how much the colors shift. Usually "
        "this sits between 0 and 1, but can "
        "easily be customized.",

        "Shifts the background colors to have an offset of `$0`.");

    sdVar("BGRotationOffset", &StyleData::BGRotOff,
        "Gets the literal rotation offset of the background panels in degrees. "
        "This usually stays at 0, but can "
        "be messed with to make some stylish level styles.",

        "Sets the rotation offset of the background panels to `$0` degrees.");

    addLuaFn(lua, "s_setCapColorMain", //
        [&styleData] { styleData.setCapColor(CapColorMode::Main{}); })
        .doc(
            "Set the color of the center polygon to match the main style "
            "color.");

    addLuaFn(lua, "s_setCapColorMainDarkened", //
        [&styleData] { styleData.setCapColor(CapColorMode::MainDarkened{}); })
        .doc(
            "Set the color of the center polygon to match the main style "
            "color, darkened.");

    addLuaFn(lua, "s_setCapColorByIndex", //
        [&styleData](int mIndex)
        { styleData.setCapColor(CapColorMode::ByIndex{mIndex}); })
        .arg("index")
        .doc(
            "Set the color of the center polygon to match the style color with "
            "index `$0`.");

    const auto colorToTuple = [](const sf::Color& c) {
        return std::tuple<int, int, int, int>{c.r, c.g, c.b, c.a};
    };

    const auto sdColorGetter =
        [&lua, &styleData, &colorToTuple](
            const char* name, const char* docName, auto pmf)
    {
        addLuaFn(lua, name,
            [&styleData, colorToTuple, pmf]
            { return colorToTuple((styleData.*pmf)()); })
            .doc(Utils::concat("Return the current ", docName,
                " color computed by the level style."));
    };

    sdColorGetter("s_getMainColor", "main", &StyleData::getMainColor);
    sdColorGetter("s_getPlayerColor", "player", &StyleData::getPlayerColor);
    sdColorGetter("s_getTextColor", "text", &StyleData::getTextColor);

    sdColorGetter(
        "s_get3DOverrideColor", "3D override", &StyleData::get3DOverrideColor);

    sdColorGetter("s_getCapColorResult", "cap color result",
        &StyleData::getCapColorResult);

    addLuaFn(lua, "s_getColor",
        [&styleData, &colorToTuple](int mIndex)
        {
            if(styleData.getColors().empty())
            {
                return colorToTuple(sf::Color{0, 0, 0, 0});
            }

            return colorToTuple(styleData.getColor(mIndex));
        })
        .arg("index")
        .doc(
            "Return the current color with index `$0` computed by the level "
            "style.");
}

[[nodiscard]] Utils::LuaMetadata& getMetadata()
{
    static Utils::LuaMetadata lm;
    return lm;
}

void init(Lua::LuaContext& lua, random_number_generator& rng, const bool inMenu,
    CCustomWallManager& cwManager, LevelStatus& levelStatus,
    HexagonGameStatus& hexagonGameStatus, StyleData& styleData)
{
    initRandom(lua, rng);
    redefineIoOpen(lua);
    redefineRandom(lua);

    // ------------------------------------------------------------------------
    // Remove potentially malicious Lua functions, including `math.randomseed`:
    destroyMaliciousFunctions(lua);

    initUtils(lua, inMenu);
    initCustomWalls(lua, cwManager);
    initLevelControl(lua, levelStatus, hexagonGameStatus);
    initStyleControl(lua, styleData);
}

void printDocs()
{
    Utils::LuaMetadata& lm = getMetadata();

    for(std::size_t i = 0; i < lm.getNumCategories(); ++i)
    {
        std::cout << '\n' << lm.prefixHeaders.at(i) << "\n\n";

        lm.forFnEntries(
            [](const std::string& ret, const std::string& name,
                const std::string& args, const std::string& docs)
            {
                std::cout << "* **`" << ret << " " << name << "(" << args
                          << ")`**: " << docs << "\n\n";
            },
            i);
    }
}

const std::vector<std::string>& getAllFunctionNames()
{
    Utils::LuaMetadata& lm = getMetadata();

    static std::vector<std::string> result = [&]
    {
        std::vector<std::string> v;

        for(std::size_t i = 0; i < lm.getNumCategories(); ++i)
        {
            lm.forFnEntries([&](const std::string&, const std::string& name,
                                const std::string&, const std::string&)
                { v.emplace_back(name); },
                i);
        }

        return v;
    }();

    return result;
}

std::string getDocsForFunction(const std::string& fnName)
{
    Utils::LuaMetadata& lm = getMetadata();

    bool found = false;
    std::ostringstream oss;

    for(std::size_t i = 0; i < lm.getNumCategories(); ++i)
    {
        lm.forFnEntries(
            [&](const std::string& ret, const std::string& name,
                const std::string& args, const std::string& docs)
            {
                if(found || name != fnName)
                {
                    return; // basically a `continue`
                }

                found = true;

                oss << ret << " " << name << "(" << args << "):\n"
                    << docs << "\n\n";
            },
            i);
    }

    if(!found)
    {
        return "UNKNOWN FUNCTION";
    }

    return oss.str();
}

} // namespace hg::LuaScripting
