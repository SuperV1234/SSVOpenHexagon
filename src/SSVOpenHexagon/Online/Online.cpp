// Copyright (c) 2013-2020 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: http://opensource.org/licenses/AFL-3.0

#include "SSVOpenHexagon/Online/Online.hpp"
#include "SSVOpenHexagon/Online/Client.hpp"
#include "SSVOpenHexagon/Online/Server.hpp"
#include "SSVOpenHexagon/Global/Config.hpp"
#include "SSVOpenHexagon/Online/Definitions.hpp"
#include "SSVOpenHexagon/Utils/Utils.hpp"
#include "SSVOpenHexagon/Online/OHServer.hpp"
#include "SSVOpenHexagon/Global/Assets.hpp"
#include "SSVOpenHexagon/SSVUtilsJson/SSVUtilsJson.hpp"

#include <SSVUtils/Core/FileSystem/FileSystem.hpp>
#include <SSVUtils/Encoding/Encoding.hpp>

using namespace std;
using namespace sf;
using namespace ssvs;
using namespace hg::Utils;
using namespace ssvu;
using namespace ssvu::Encoding;
using namespace ssvuj;
using namespace ssvu::FileSystem;

namespace hg::Online
{

const IpAddress localIp{"127.0.0.1"};
const IpAddress hostIp{"46.4.172.228"};
const unsigned short localPort{54000};
const unsigned short hostPort{27273};

ConnectStat connectionStatus{ConnectStat::Disconnected};
LoginStat loginStatus{LoginStat::Unlogged};
std::unique_ptr<Online::GlobalThreadManager> currentGtm{nullptr};

float serverVersion{-1};
string serverMessage;

ValidatorDB validators;

PacketHandler<Client> clientPHandler;
std::unique_ptr<Client> client;

bool gettingLeaderboard{false}, forceLeaderboardRefresh{false};
string lastLeaderboardId;
float lastLeaderboardDM;

bool newUserReg{false}, needsCleanup{false};

string currentUsername{"NULL"}, currentLeaderboard{"NULL"},
    currentUserStatsStr{"NULL"};
UserStats currentUserStats;
ssvuj::Obj currentFriendScores;

void refreshUserStats()
{
    ssvuj::Obj root{getFromStr(currentUserStatsStr)};
    currentUserStats = ssvuj::getExtr<UserStats>(root);
}

void setCurrentGtm(std::unique_ptr<GlobalThreadManager> mGtm)
{
    currentGtm = std::move(mGtm);
}

void initializeClient()
{
    if(!Config::getOnline())
    {
        return;
    }

    clientPHandler[FromServer::LoginResponseValid] = [](Client& /*unused*/,
                                                         Packet& mP) {
        lo("PacketHandler") << "Successfully logged in!\n";
        loginStatus = LoginStat::Logged;
        newUserReg = ssvuj::getExtr<bool>(getDecompressedPacket(mP), 0);
        trySendInitialRequests();
    };

    clientPHandler[FromServer::LoginResponseInvalid] = [](Client& /*unused*/,
                                                           Packet& /*unused*/) {
        loginStatus = LoginStat::Unlogged;
        lo("PacketHandler") << "Login invalid!\n";
    };

    clientPHandler[FromServer::RequestInfoResponse] = [](Client& /*unused*/,
                                                          Packet& mP) {
        ssvuj::Obj r{getDecompressedPacket(mP)};
        serverVersion = ssvuj::getExtr<float>(r, 0);
        serverMessage = ssvuj::getExtr<string>(r, 1);
    };

    clientPHandler[FromServer::SendLeaderboard] = [](Client& /*unused*/,
                                                      Packet& mP) {
        currentLeaderboard =
            ssvuj::getExtr<string>(getDecompressedPacket(mP), 0);
        gettingLeaderboard = false;
    };

    clientPHandler[FromServer::SendLeaderboardFailed] =
        [](Client& /*unused*/, Packet& /*unused*/) {
            currentLeaderboard = "NULL";
            lo("PacketHandler") << "Server failed sending leaderboard\n";
            gettingLeaderboard = false;
        };

    clientPHandler[FromServer::SendScoreResponseValid] =
        [](Client& /*unused*/, Packet& /*unused*/) {
            lo("PacketHandler") << "Server successfully accepted score\n";
        };

    clientPHandler[FromServer::SendScoreResponseInvalid] =
        [](Client& /*unused*/, Packet& /*unused*/) {
            lo("PacketHandler") << "Server refused score\n";
        };

    clientPHandler[FromServer::SendUserStats] = [](Client& /*unused*/,
                                                    Packet& mP) {
        currentUserStatsStr =
            ssvuj::getExtr<string>(getDecompressedPacket(mP), 0);
        refreshUserStats();
    };

    clientPHandler[FromServer::SendUserStatsFailed] = [](Client& /*unused*/,
                                                          Packet& /*unused*/) {
        currentUserStatsStr = "NULL";
        lo("PacketHandler") << "Server failed sending user stats\n";
    };

    clientPHandler[FromServer::SendFriendsScores] = [](Client& /*unused*/,
                                                        Packet& mP) {
        currentFriendScores = ssvuj::getFromStr(
            ssvuj::getExtr<string>(getDecompressedPacket(mP), 0));
    };

    clientPHandler[FromServer::SendLogoutValid] = [](Client& /*unused*/,
                                                      Packet& /*unused*/) {
        loginStatus = LoginStat::Unlogged;
    };

    clientPHandler[FromServer::NUR_EmailValid] =
        [](Client& /*unused*/, Packet& /*unused*/) { newUserReg = false; };

    client = std::make_unique<Client>(clientPHandler);

    currentGtm->start([] {
        while(true)
        {
            if(connectionStatus == ConnectStat::Connected)
            {
                client->send(buildCPacket<FromClient::Ping>());
                if(!client->isBusy())
                {
                    connectionStatus = ConnectStat::Disconnected;
                    loginStatus = LoginStat::Unlogged;
                }
            }

            if(needsCleanup)
            {
                return;
            }

            std::this_thread::sleep_for(std::chrono::seconds{1});
        }
    });
}

bool canSendPacket()
{
    return connectionStatus == ConnectStat::Connected &&
           loginStatus == LoginStat::Logged && currentUsername != "NULL";
}

template <typename T>
void trySendFunc(T mFunc)
{
    if(!Config::getOnline())
    {
        return;
    }

    if(!canSendPacket())
    {
        lo("hg::Online::trySendFunc") << "Can't send data to server: "
                                         "not connected / not logged "
                                         "in\n";
        return;
    }

    HG_LO_VERBOSE("hg::Online::trySendFunc") << "Sending data to server...\n";

    currentGtm->start([mFunc] {
        if(!canSendPacket())
        {
            lo("hg::Online::trySendFunc")
                << "Client not connected - aborting\n";
            return;
        }
        mFunc();
    });
}

template <unsigned int TType, typename... TArgs>
void trySendPacket(TArgs&&... mArgs)
{
    if(!Config::getOnline())
    {
        return;
    }

    auto packet(buildCPacket<TType>(mArgs...));
    trySendFunc([packet] { client->send(packet); });
}

void tryConnectToServer()
{
    if(!Config::getOnline())
    {
        return;
    }

    if(connectionStatus == ConnectStat::Connecting)
    {
        lo("hg::Online::connectToServer") << "Already connecting\n";
        return;
    }

    if(connectionStatus == ConnectStat::Connected)
    {
        lo("hg::Online::connectToServer") << "Already connected\n";
        return;
    }

    lo("hg::Online::connectToServer") << "Connecting to server...\n";
    client->disconnect();
    connectionStatus = ConnectStat::Connecting;

    currentGtm->start([] {
        if(client->connect(getCurrentIpAddress(), getCurrentPort()))
        {
            lo("hg::Online::connectToServer") << "Connected to server!\n";
            connectionStatus = ConnectStat::Connected;
            return;
        }

        lo("hg::Online::connectToServer") << "Failed to connect\n";
        connectionStatus = ConnectStat::Disconnected;
        client->disconnect();
    });
}

void tryLogin(const string& mUsername, const string& mPassword)
{
    if(!Config::getOnline())
    {
        return;
    }

    if(loginStatus != LoginStat::Unlogged)
    {
        logout();
        return;
    }

    if(connectionStatus != ConnectStat::Connected)
    {
        lo("hg::Online::tryLogin") << "Client not connected - aborting\n";
        loginStatus = LoginStat::Unlogged;
        return;
    }

    lo("hg::Online::tryLogin") << "Logging in...\n";
    loginStatus = LoginStat::Logging;

    currentGtm->start([&mUsername, &mPassword] {
        client->send(buildCPacket<FromClient::Login>(mUsername, mPassword));
        currentUsername = mUsername;

        std::this_thread::sleep_for(std::chrono::seconds{6});

        if(loginStatus == LoginStat::Logging)
        {
            lo("hg::Online::tryLogin") << "Failed to login - aborting\n";
            loginStatus = LoginStat::Unlogged;
            return;
        }
    });
}

void trySendScore(const string& mLevelId, float mDiffMult, float mScore)
{
    trySendPacket<FromClient::SendScore>(currentUsername, mLevelId,
        validators.getValidator(mLevelId), mDiffMult, mScore);
}

void tryRequestLeaderboard(const string& mLevelId, float mDiffMult)
{
    trySendPacket<FromClient::RequestLeaderboard>(currentUsername, mLevelId,
        validators.getValidator(mLevelId), mDiffMult);
}

void trySendDeath()
{
    trySendPacket<FromClient::US_Death>(currentUsername);
}

void trySendMinutePlayed()
{
    trySendPacket<FromClient::US_MinutePlayed>(currentUsername);
}

void trySendRestart()
{
    trySendPacket<FromClient::US_Restart>(currentUsername);
}

void trySendInitialRequests()
{
    trySendPacket<FromClient::RequestInfo>();
    trySendPacket<FromClient::RequestUserStats>(currentUsername);
}

void trySendAddFriend(const string& mFriendName)
{
    trySendPacket<FromClient::US_AddFriend>(currentUsername, mFriendName);
    trySendInitialRequests();
}

void trySendClearFriends()
{
    trySendPacket<FromClient::US_ClearFriends>(currentUsername);
    trySendInitialRequests();
}

void tryRequestFriendsScores(const string& mLevelId, float mDiffMult)
{
    trySendPacket<FromClient::RequestFriendsScores>(
        currentUsername, mLevelId, mDiffMult);
}

void trySendUserEmail(const string& mEmail)
{
    trySendPacket<FromClient::NUR_Email>(currentUsername, mEmail);
}

void logout()
{
    trySendPacket<FromClient::Logout>(currentUsername);
}

void cleanup()
{
    needsCleanup = true;

    if(currentGtm != nullptr)
    {
        currentGtm->join();
    }

    currentGtm.reset();
    client.reset();
}

void requestLeaderboardIfNeeded(const string& mLevelId, float mDiffMult)
{
    if(!forceLeaderboardRefresh)
    {
        if(gettingLeaderboard ||
            (lastLeaderboardId == mLevelId && lastLeaderboardDM == mDiffMult))
        {
            return;
        }
    }
    else
    {
        forceLeaderboardRefresh = false;
    }

    invalidateCurrentLeaderboard();
    invalidateCurrentFriendsScores();
    gettingLeaderboard = true;
    lastLeaderboardId = mLevelId;
    lastLeaderboardDM = mDiffMult;
    tryRequestLeaderboard(mLevelId, mDiffMult);
    tryRequestFriendsScores(mLevelId, mDiffMult);
    trySendPacket<FromClient::RequestUserStats>(currentUsername);
}

void setForceLeaderboardRefresh(bool mValue)
{
    forceLeaderboardRefresh = mValue;
}

ConnectStat SSVU_ATTRIBUTE(pure) getConnectionStatus()
{
    return connectionStatus;
}

LoginStat SSVU_ATTRIBUTE(pure) getLoginStatus()
{
    return loginStatus;
}

string getCurrentUsername()
{
    return loginStatus == LoginStat::Logged ? currentUsername : "NULL";
}

const ssvuj::Obj& SSVU_ATTRIBUTE(const) getCurrentFriendScores()
{
    return currentFriendScores;
}

const UserStats& SSVU_ATTRIBUTE(const) getUserStats()
{
    return currentUserStats;
}

ValidatorDB& SSVU_ATTRIBUTE(const) getValidators()
{
    return validators;
}

bool SSVU_ATTRIBUTE(pure) getNewUserReg()
{
    return newUserReg;
}

void invalidateCurrentLeaderboard()
{
    currentLeaderboard = "NULL";
}

void invalidateCurrentFriendsScores()
{
    currentFriendScores = ssvuj::Obj{};
}

const string& SSVU_ATTRIBUTE(const) getCurrentLeaderboard()
{
    return currentLeaderboard;
}

float SSVU_ATTRIBUTE(pure) getServerVersion()
{
    return serverVersion;
}

const string& SSVU_ATTRIBUTE(const) getServerMessage()
{
    return serverMessage;
}

string getValidator(const ssvufs::Path& mPackPath, const string& mLevelId,
    const string& mLevelRootString, const ssvufs::Path& mStyleRootPath,
    const ssvufs::Path& mLuaScriptPath)
{
    string luaScriptContents{mLuaScriptPath.getContentsAsStr()};
    std::set<string> luaScriptNames;

    recursiveFillIncludedLuaFileNames(
        luaScriptNames, mPackPath, luaScriptContents);

    string toEncrypt;
    toEncrypt += mLevelId;
    toEncrypt += getControlStripped(mLevelRootString);
    toEncrypt += mStyleRootPath.getContentsAsStr();
    toEncrypt += luaScriptContents;
    for(const auto& lsn : luaScriptNames)
    {
        Path path{mPackPath + "/Scripts/" + lsn};
        toEncrypt += path.getContentsAsStr();
    }

    toEncrypt = getControlStripped(toEncrypt);
    return getUrlEncoded(mLevelId) + getMD5Hash(HG_ENCRYPT(toEncrypt));
}

string getMD5Hash(const string& mStr)
{
    return encode<Encoding::Type::MD5>(mStr);
}

void initializeValidators(HGAssets& mAssets)
{
    if(!Config::getOnline())
    {
        return;
    }

    HG_LO_VERBOSE("hg::Online::initializeValidators")
        << "Initializing validators...\n";

    for(const auto& p : mAssets.getLevelDatas())
    {
        HG_LO_VERBOSE("hg::Online::initializeValidators")
            << "Adding (" << p.first << ") validator\n";

        const auto& l(p.second);
        const auto& validator(getValidator(l.packPath, l.id, l.getRootString(),
            mAssets.getStyleData(l.styleId).getRootPath(), l.luaScriptPath));
        validators.addValidator(p.first, validator);

        HG_LO_VERBOSE("hg::Online::initializeValidators")
            << "Added (" << p.first << "): " << validator << "\n";
    }

    HG_LO_VERBOSE("hg::Online::initializeValidators")
        << "Finished initializing validators...\n";
}

const sf::IpAddress& getCurrentIpAddress()
{
    return Config::getServerLocal() ? localIp : hostIp;
}

unsigned short getCurrentPort()
{
    return Config::getServerLocal() ? localPort : hostPort;
}

} // namespace hg::Online
