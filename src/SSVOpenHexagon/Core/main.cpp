// Copyright (c) 2013-2020 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: http://opensource.org/licenses/AFL-3.0

#include "SSVOpenHexagon/Online/Online.hpp"
#include "SSVOpenHexagon/Online/Server.hpp"
#include "SSVOpenHexagon/Online/OHServer.hpp"
#include "SSVOpenHexagon/Core/HexagonGame.hpp"
#include "SSVOpenHexagon/Core/MenuGame.hpp"
#include "SSVOpenHexagon/Core/Steam.hpp"
#include "SSVOpenHexagon/Core/Discord.hpp"
#include "SSVOpenHexagon/Core/Replay.hpp"
#include "SSVOpenHexagon/Global/Assets.hpp"
#include "SSVOpenHexagon/Global/Config.hpp"

#include <SSVStart/GameSystem/GameWindow.hpp>

#include <string>
#include <optional>
#include <vector>
#include <memory>
#include <filesystem>
#include <string_view>

static void createFolderIfNonExistant(const std::string& folderName)
{
    const ssvu::FileSystem::Path path{folderName};

    if(path.exists<ssvufs::Type::Folder>())
    {
        return;
    }

    ssvu::lo("::createFolderIfNonExistant")
        << "'" << folderName << "' folder does not exist, creating\n";

    createFolder(path);
}

static std::vector<std::string> argsToVector(int argc, char* argv[])
{
    std::vector<std::string> result;

    for(int i = 0; i < argc; ++i)
    {
        result.emplace_back(argv[i]);
    }

    return result;
}

static std::string makeWindowTitle()
{
    return "Open Hexagon " + std::string{hg::Config::getVersionString()} +
           " - by vittorio romeo - http://vittorioromeo.info";
}

static std::optional<std::string> getFirstReplayFilenameFromArgs(
    const std::vector<std::string>& args)
{
    for(const std::string& arg : args)
    {
        if(arg.find(".ohreplay") != std::string::npos)
        {
            return arg;
        }
    }

    return std::nullopt;
}

int main(int argc, char* argv[])
{
    if(argc < 1)
    {
        std::cerr << "Fatal error: no executable specified" << std::endl;
        return -1;
    }

    // Set working directory to current executable location
    std::filesystem::current_path(std::filesystem::path{argv[0]}.parent_path());

    // Steam integration
    hg::Steam::steam_manager steamManager;
    steamManager.request_stats_and_achievements();

    // Discord integration
    hg::Discord::discord_manager discordManager;

// TODO: remove
#if 0
    hg::Online::setCurrentGtm(
        std::make_unique<hg::Online::GlobalThreadManager>());
#endif

    const std::vector<std::string> args = argsToVector(argc, argv);

// TODO: remove
#if 0
    if(ssvu::contains(args, "server"))
    {
        hg::Config::loadConfig(args);

        auto levelOnlyAssets = std::make_unique<hg::HGAssets>(
            steamManager, true /* mLevelsOnly */);
        hg::Online::initializeValidators(*levelOnlyAssets);

        auto ohServer = std::make_unique<hg::Online::OHServer>();
        ohServer->start();

        return 0;
    }
#endif

    createFolderIfNonExistant("Profiles/");
    createFolderIfNonExistant("Replays/");

// TODO: remove
#if 0
    hg::Online::initializeClient();
    hg::Online::tryConnectToServer();
#endif

    hg::Config::loadConfig(args);

// TODO: remove
#if 0
    if(hg::Config::getServerLocal())
    {
        ssvu::lo("Server") << "LOCAL MODE ON\n";
    }
#endif

    ssvs::GameWindow window;

    window.setTitle(makeWindowTitle());
    window.setSize(hg::Config::getWidth(), hg::Config::getHeight());
    window.setPixelMult(hg::Config::getPixelMultiplier());
    window.setFullscreen(hg::Config::getFullscreen());
    window.setAntialiasingLevel(hg::Config::getAntialiasingLevel());
    window.setVsync(hg::Config::getVsync());
    window.setFPSLimited(hg::Config::getLimitFPS());
    window.setMaxFPS(hg::Config::getMaxFPS());

    hg::Config::setTimerStatic(window, hg::Config::getTimerStatic());

    auto assets = std::make_unique<hg::HGAssets>(steamManager);
    hg::Online::initializeValidators(*assets);

    auto hg = std::make_unique<hg::HexagonGame>(
        steamManager, discordManager, *assets, window);

    auto mg = std::make_unique<hg::MenuGame>(
        steamManager, discordManager, *assets, *hg, window);

    hg->mgPtr = mg.get();

    assets->refreshVolumes();

    const std::optional<std::string> replayFilename =
        getFirstReplayFilenameFromArgs(args);

    const auto gotoMenu = [&] {
        window.setGameState(mg->getGame());
        mg->init(false /* mError */);
    };

    const auto gotoGameReplay = [&](const hg::replay_file& replayFile) {
        hg->setLastReplay(replayFile);
        hg->newGame(replayFile._pack_id, replayFile._level_id,
            /* mFirstPlay */ true, replayFile._difficulty_mult,
            /* mExecuteLastReplay */ true);

        window.setGameState(hg->getGame());
    };

    // TODO: cleanup
    if(!replayFilename.has_value())
    {
        gotoMenu();
    }
    else
    {
        if(hg::replay_file rf; rf.deserialize_from_file(*replayFilename))
        {
            ssvu::lo("Replay")
                << "Playing replay file '" << *replayFilename << "'\n";

            gotoGameReplay(rf);
        }
        else
        {
            ssvu::lo("Replay") << "Failed to read replay file '"
                               << replayFilename.value() << "'\n";

            gotoMenu();
        }
    }

    window.run();

    // TODO: remove
#if 0
    if(hg::Online::getLoginStatus() != hg::Online::LoginStat::Logged)
    {
        hg::Online::logout();
    }
#endif

    ssvu::lo().flush();

    hg::Config::saveConfig();
    assets->pSaveCurrent();

    ssvu::saveLogToFile("log.txt");

    // TODO: remove
#if 0
    hg::Online::cleanup();
#endif

    return 0;
}
