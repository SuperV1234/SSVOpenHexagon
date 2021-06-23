// Copyright (c) 2013-2020 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: https://opensource.org/licenses/AFL-3.0

#pragma once

#include "SSVOpenHexagon/Core/HGStatus.hpp"
#include "SSVOpenHexagon/Core/RandomNumberGenerator.hpp"
#include "SSVOpenHexagon/Core/Replay.hpp"
#include "SSVOpenHexagon/Data/LevelStatus.hpp"
#include "SSVOpenHexagon/Data/MusicData.hpp"
#include "SSVOpenHexagon/Data/StyleData.hpp"
#include "SSVOpenHexagon/Components/CPlayer.hpp"
#include "SSVOpenHexagon/Utils/Utils.hpp"
#include "SSVOpenHexagon/Utils/LuaWrapper.hpp"
#include "SSVOpenHexagon/Utils/FastVertexVector.hpp"
#include "SSVOpenHexagon/Utils/Timeline2.hpp"
#include "SSVOpenHexagon/Utils/TimelineGlobal.hpp"
#include "SSVOpenHexagon/Components/CCustomWallManager.hpp"

#include <SSVStart/GameSystem/GameSystem.hpp>
#include <SSVStart/Camera/Camera.hpp>
#include <SSVStart/VertexVector/VertexVector.hpp>

#include <SSVUtils/Core/Common/Frametime.hpp>
#include <SSVUtils/Timeline/Timeline.hpp>

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Drawable.hpp>
#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Texture.hpp>

#include <SFML/System/Vector2.hpp>
#include <SFML/System/Clock.hpp>

#include <cstdint>
#include <sstream>
#include <unordered_set>
#include <functional>
#include <optional>
#include <vector>
#include <string>

struct ImGuiInputTextCallbackData;

namespace Json {
class Value;
}

namespace ssvuj {
using Obj = Json::Value;
}

namespace ssvs::Input {
class Trigger;
}

namespace hg {

class Audio;
class HGAssets;
class HexagonClient;
struct LevelData;
struct SpeedData;
struct PackData;

namespace Steam {
class steam_manager;
}

namespace Discord {
class discord_manager;
}

class HexagonGame
{
private:
    Steam::steam_manager* steamManager;
    Discord::discord_manager* discordManager;
    bool discordHung{false};
    bool steamHung{false};
    std::int8_t discordAttempt{1};
    std::int8_t steamAttempt{1};

    HGAssets& assets;
    sf::Font& font;
    sf::Font& fontBold;

    Audio* audio;
    MusicData::Segment segment;

    const LevelData* levelData;

    ssvs::GameState game;
    ssvs::GameWindow* window;

    HexagonClient* hexagonClient;

    // IMGUI Lua Console
    sf::Clock ilcDeltaClock;
    std::vector<std::string> ilcCmdLog;
    std::vector<std::string> ilcHistory;
    int ilcHistoryPos{-1};
    std::string ilcCmdBuffer;
    std::string ilcTrackBuffer;
    bool ilcShowConsole{false};
    bool ilcShowConsoleNext{false};
    std::vector<std::string> ilcLuaTracked;
    std::vector<std::string> ilcLuaTrackedNames;
    std::vector<std::string> ilcLuaTrackedResults;
    bool debugPause{false};

    std::vector<std::string> execScriptPackPathContext;

public:
    int ilcTextEditCallback(ImGuiInputTextCallbackData* data);

public:
    CPlayer player;
    std::vector<CWall> walls;
    CCustomWallManager cwManager;
    float timeUntilRichPresenceUpdate = 0.f;

private:
    std::optional<ssvs::Camera> backgroundCamera;
    std::optional<ssvs::Camera> overlayCamera;

    ssvu::TimelineManager effectTimelineManager;

    const sf::Vector2f centerPos{0.f, 0.f};

    Lua::LuaContext lua;
    std::unordered_set<std::string> calledDeprecatedFunctions;

    LevelStatus levelStatus;
    MusicData musicData;
    StyleData styleData;

    Utils::timeline2 timeline;
    Utils::timeline2_runner timelineRunner;

    Utils::timeline2 eventTimeline;
    Utils::timeline2_runner eventTimelineRunner;

    Utils::timelineGlobal musicTimeline;
    Utils::timelineGlobal_runner musicTimelineRunner;

    Utils::timeline2 messageTimeline;
    Utils::timeline2_runner messageTimelineRunner;

    sf::Text messageText;
    sf::Text pbText;

    ssvs::VertexVector<sf::PrimitiveType::Quads> flashPolygon{4};

    struct Particle
    {
        sf::Sprite sprite;
        sf::Vector2f velocity;
        float angularVelocity;
    };

    std::vector<Particle> particles;
    bool mustSpawnPBParticles{false};
    float nextPBParticleSpawn{0.f};
    float pbTextGrowth{0.f};

    sf::Sprite keyIconLeft;
    sf::Sprite keyIconRight;
    sf::Sprite keyIconFocus;
    sf::Sprite keyIconSwap;
    sf::Sprite replayIcon;

    sf::RectangleShape levelInfoRectangle;
    sf::Text levelInfoTextLevel;
    sf::Text levelInfoTextPack;
    sf::Text levelInfoTextAuthor;
    sf::Text levelInfoTextBy;
    sf::Text levelInfoTextDM;

    bool firstPlay{true};
    bool restartFirstTime{true};
    bool inputFocused{false};
    bool inputSwap{false};
    bool mustTakeScreenshot{false};
    bool mustChangeSides{false};
    bool mustStart{false};

    random_number_generator rng;
    HexagonGameStatus status;

    float deathInputIgnore{0.f};

    struct ActiveReplay
    {
        replay_file replayFile;
        replay_player replayPlayer;
        std::string replayPackName;
        std::string replayLevelName;

        explicit ActiveReplay(const replay_file& mReplayFile);
    };

    std::optional<ActiveReplay> activeReplay;

    random_number_generator::seed_type lastSeed{};
    replay_data lastReplayData{};
    bool lastFirstPlay{};
    double lastPlayedScore{};

    std::string restartId;
    float difficultyMult{1};
    int inputImplLastMovement{0};
    int inputMovement{0};
    bool inputImplCW{false};
    bool inputImplCCW{false};
    std::ostringstream os;

    sf::Text fpsText;
    sf::Text timeText;
    sf::Text text;
    sf::Text replayText;

    // Color of the polygon in the center.
    CapColor capColor;

    std::string packId;
    std::string levelId;

    // Lua related methods
    void initLua_Utils();
    void initLua_AudioControl();
    void initLua_MainTimeline();
    void initLua_EventTimeline();
    void initLua_LevelControl();
    void initLua_StyleControl();
    void initLua_WallCreation();
    void initLua_Steam();
    void initLua_CustomWalls();
    void initLua_Deprecated();

    void initLua();
    void runLuaFile(const std::string& mFileName);

    // Wall creation
    void createWall(int mSide, float mThickness, const SpeedData& mSpeed,
        const SpeedData& mCurve, float mHueMod);

public:
    // TODO (P2): For testing
    std::function<void(const replay_file&)> onDeathReplayCreated;

    // TODO (P2): For testing
    void setMustStart(const bool x)
    {
        mustStart = x;
    }

    // TODO (P2): For testing
    bool executeRandomInputs{false};
    bool alwaysSpinRight{false};

    void initLuaAndPrintDocs();

    void luaExceptionLippincottHandler(const std::string& mName);

    template <typename T, typename... TArgs>
    auto runLuaFunctionIfExists(const std::string& mName, const TArgs&... mArgs)
    try
    {
        return Utils::runLuaFunctionIfExists<T, TArgs...>(lua, mName, mArgs...);
    }
    catch(...)
    {
        luaExceptionLippincottHandler(mName);
        return decltype(
            Utils::runLuaFunctionIfExists<T, TArgs...>(lua, mName, mArgs...)){};
    }

    void raiseWarning(
        const std::string& mFunctionName, const std::string& mAdditionalInfo);

    void setLastReplay(const replay_file& mReplayFile);

private:
    void start();

    void initKeyIcons();
    void initFlashEffect();

    // Fast-forward
    std::optional<double> fastForwardTarget;
    void fastForwardTo(const double target);

    // Update methods
    void update(ssvu::FT mFT, const float timescale);
    void updateInput();
    void updateInput_UpdateJoystickControls();
    void updateInput_UpdateTouchControls();
    void updateInput_ResolveInputImplToInputMovement();
    void updateInput_RecordCurrentInputToLastReplayData();
    void updateWalls(ssvu::FT mFT);
    void updateIncrement();
    void updateEvents(ssvu::FT mFT);
    void updateMusic(ssvu::FT mFT);
    void updateLevel(ssvu::FT mFT);
    void updateCustomWalls(ssvu::FT mFT);
    void updatePulse(ssvu::FT mFT);
    void updateBeatPulse(ssvu::FT mFT);
    void updateRotation(ssvu::FT mFT);
    void updateFlash(ssvu::FT mFT);
    void updatePulse3D(ssvu::FT mFT);
    void updateText(ssvu::FT mFT);
    void updateKeyIcons();
    void updateLevelInfo();
    void updateParticles(ssvu::FT mFT);

    // Post update methods
    void postUpdate();
    void postUpdate_ImguiLuaConsole();

    // Draw methods
    void draw();

    // Gameplay methods
    void incrementDifficulty();
    void sideChange(unsigned int mSideNumber);

    // Draw methods
    void drawText_TimeAndStatus(const sf::Color& offsetColor);
    void drawText_Message(const sf::Color& offsetColor);
    void drawText_PersonalBest(const sf::Color& offsetColor);
    void drawText();
    void drawKeyIcons();
    void drawLevelInfo();
    void drawParticles();
    void drawImguiLuaConsole();

    // Data-related methods
    void setLevelData(const LevelData& mLevelData, bool mMusicFirstPlay);
    void playLevelMusic();
    void playLevelMusicAtTime(float mSeconds);
    void stopLevelMusic();

    // Message-related methods
    void addMessage(std::string mMessage, double mDuration, bool mSoundToggle);
    void clearMessages();

    enum class CheckScore
    {
        NoLocalProfile,
        Ineligible,
        Invalid,
        Valid,
    };

    // Level/menu loading/unloading/changing
    [[nodiscard]] bool shouldSaveScore();
    void goToMenu(bool mSendScores = true, bool mError = false);

    void invalidateScore(const std::string& mReason);

    [[nodiscard]] bool imguiLuaConsoleHasInput();

    template <typename T>
    auto makeLuaAccessor(T& obj, const std::string& prefix);

    static void nameFormat(std::string& name);
    [[nodiscard]] static std::string diffFormat(float diff);
    [[nodiscard]] static std::string timeFormat(float time);

private:
    void performPlayerSwap(const bool mPlaySound);
    void performPlayerKill();

    Utils::FastVertexVectorTris backgroundTris;
    Utils::FastVertexVectorQuads wallQuads;
    Utils::FastVertexVectorQuads pivotQuads;
    Utils::FastVertexVectorTris playerTris;
    Utils::FastVertexVectorTris capTris;
    Utils::FastVertexVectorQuads wallQuads3D;
    Utils::FastVertexVectorQuads pivotQuads3D;
    Utils::FastVertexVectorTris playerTris3D;

public:
    std::function<void(const bool)> fnGoToMenu;

    HexagonGame(Steam::steam_manager* mSteamManager,
        Discord::discord_manager* mDiscordManager, HGAssets& mAssets,
        Audio* mAudio, ssvs::GameWindow* mGameWindow,
        HexagonClient* mHexagonClient);

    ~HexagonGame();

    void refreshTrigger(const ssvs::Input::Trigger& trigger, const int bindID);

    // Gameplay methods
    void newGame(const std::string& mPackId, const std::string& mId,
        bool mFirstPlay, float mDifficultyMult, bool executeLastReplay);

    enum class SaveScoreIfNeededResult
    {
        NoWindow = 0,
        ShouldNotSave = 1,
        NotPersonalBest = 2,
        PersonalBest = 3,
    };

    void death(bool mForce = false);
    void death_shakeCamera();
    [[nodiscard]] replay_file death_createReplayFile();
    void death_updateRichPresence();
    [[nodiscard]] SaveScoreIfNeededResult death_saveScoreIfNeeded();
    void death_saveScoreIfNeededAndShowPBEffects();
    void death_sendAndSaveReplay(const replay_file& rf);
    [[nodiscard]] bool death_sendReplay(
        const std::string& levelValidator, const compressed_replay_file& crf);
    [[nodiscard]] bool death_saveReplay(
        const std::string& filename, const compressed_replay_file& crf);

    struct GameExecutionResult
    {
        double playedTimeSeconds;
        double pausedTimeSeconds;
        double totalTimeSeconds;
        float customScore;
    };

    [[nodiscard]] std::optional<GameExecutionResult> executeGameUntilDeath(
        const int maxProcessingSeconds, const float timescale);

    [[nodiscard]] std::optional<GameExecutionResult>
    runReplayUntilDeathAndGetScore(const replay_file& mReplayFile,
        const int maxProcessingSeconds, const float timescale);

    // Other methods
    void executeEvents(ssvuj::Obj& mRoot, float mTime);
    void updateRichPresenceCallbacks();

    [[nodiscard]] bool shouldPlaySounds() const;
    [[nodiscard]] bool shouldPlayMusic() const;
    void playSoundOverride(const std::string& mId);
    void playSoundAbort(const std::string& mId);
    void playPackSoundOverride(
        const std::string& mPackId, const std::string& mId);

    // Graphics-related methods
    void render(sf::Drawable& mDrawable,
        const sf::RenderStates& mStates = sf::RenderStates::Default);

    // Setters
    void setSides(unsigned int mSides);

    // Getters
    [[nodiscard]] ssvs::GameState& getGame() noexcept;
    [[nodiscard]] float getRadius() const noexcept;
    [[nodiscard]] const sf::Color& getColor(int mIdx) const noexcept;
    [[nodiscard]] float getSpeedMultDM() const noexcept;
    [[nodiscard]] float getDelayMultDM() const noexcept;
    [[nodiscard]] float getRotationSpeed() const noexcept;
    [[nodiscard]] unsigned int getSides() const noexcept;
    [[nodiscard]] float getWallSkewLeft() const noexcept;
    [[nodiscard]] float getWallSkewRight() const noexcept;
    [[nodiscard]] float getWallAngleLeft() const noexcept;
    [[nodiscard]] float getWallAngleRight() const noexcept;
    [[nodiscard]] float get3DEffectMult() const noexcept;
    [[nodiscard]] HexagonGameStatus& getStatus() noexcept;
    [[nodiscard]] const HexagonGameStatus& getStatus() const noexcept;
    [[nodiscard]] LevelStatus& getLevelStatus();
    [[nodiscard]] HGAssets& getAssets();
    [[nodiscard]] sf::Color getColorMain() const;
    [[nodiscard]] sf::Color getColorPlayer() const;
    [[nodiscard]] sf::Color getColorText() const;
    [[nodiscard]] sf::Color getColorCap() const;
    [[nodiscard]] sf::Color getColorWall() const;
    [[nodiscard]] float getMusicDMSyncFactor() const;
    [[nodiscard]] float getOptionalMusicDMSyncFactor() const;

    void refreshMusicPitch();

    // Input
    [[nodiscard]] bool getInputFocused() const;
    [[nodiscard]] float getPlayerSpeedMult() const;
    [[nodiscard]] bool getInputSwap() const;
    [[nodiscard]] int getInputMovement() const;

    // Pack information
    [[nodiscard]] const PackData& getPackData() const noexcept;
    [[nodiscard]] const std::string& getPackId() const noexcept;
    [[nodiscard]] const std::string& getPackDisambiguator() const noexcept;
    [[nodiscard]] const std::string& getPackAuthor() const noexcept;
    [[nodiscard]] const std::string& getPackName() const noexcept;
    [[nodiscard]] int getPackVersion() const noexcept;

    [[nodiscard]] bool inReplay() const noexcept;
    [[nodiscard]] bool mustReplayInput() const noexcept;
    [[nodiscard]] bool mustShowReplayUI() const noexcept;

    [[nodiscard]] float getSwapCooldown() const noexcept;
};

} // namespace hg
