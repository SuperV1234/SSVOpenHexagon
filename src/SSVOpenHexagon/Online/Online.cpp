// Copyright (c) 2013 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: http://opensource.org/licenses/AFL-3.0

#include <functional>
#include <SFML/Network.hpp>
#include <SSVUtils/SSVUtils.h>
#include <SSVUtilsJson/SSVUtilsJson.h>
#include "SSVOpenHexagon/Online/Online.h"
#include "SSVOpenHexagon/Global/Config.h"
#include "SSVOpenHexagon/Online/Definitions.h"
#include "SSVOpenHexagon/Utils/Utils.h"

using namespace std;
using namespace sf;
using namespace ssvs;
using namespace ssvs::Utils;
using namespace hg::Utils;
using namespace ssvu;
using namespace ssvuj;
using namespace ssvu::FileSystem;

namespace hg
{
	namespace Online
	{
		using Request = Http::Request;
		using Response = Http::Response;
		using Status = Http::Response::Status;

		const IpAddress hostIp{"209.236.124.147"};
		const unsigned short hostPort{27272};

		const string host{"http://vittorioromeo.info"};
		const string folder{"Misc/Linked/OHServer/"};
		const string infoFile{"OHInfo.json"};
		const string scoresFile{"scores.json"};
		const string sendScoreFile{"sendScore.php"};
		const string getScoresFile{"getScores.php"};

		MemoryManager<ThreadWrapper> memoryManager;
		float serverVersion{-1};
		string serverMessage{""};
		Json::Value scoresRoot;

		void startCheckUpdates()
		{
			if(!getOnline()) { log("Online disabled, aborting", "Online"); return; }

			ThreadWrapper& thread = memoryManager.create([]
			{
				log("Checking updates...", "Online");

				Response response{getGetResponse(host, folder, infoFile)};
				Status status{response.getStatus()};
				if(status == Response::Ok)
				{
					Json::Value root{getRootFromString(response.getBody())};
					serverMessage = as<string>(root, "message", "");
					log("Server message:\n" + serverMessage, "Online");

					serverVersion = as<float>(root, "latest_version", -1);
					log("Server latest version: " + toStr(getServerVersion()), "Online");

					if(serverVersion == getVersion()) log("No updates available", "Online");
					else if(serverVersion < getVersion()) log("Your version is newer than the server's (beta)", "Online");
					else if(serverVersion > getVersion()) log("Update available (" + toStr(serverVersion) + ")", "Online");
				}
				else
				{
					serverVersion = -1;
					serverMessage = "Error connecting to server";
					log("Error checking updates - code: " + status, "Online");
				}

				log("Finished checking updates", "Online");
				cleanUp();
			});

			thread.launch();
		}
		void startCheckScores()
		{
			if(!getOnline()) { log("Online disabled, aborting", "Online"); return; }

			ThreadWrapper& thread = memoryManager.create([]
			{
				log("Checking scores...", "Online");

				Response response{getGetResponse(host, folder, scoresFile)};
				Status status{response.getStatus()};
				if(status == Response::Ok)
				{
					Json::Reader reader; reader.parse(response.getBody(), scoresRoot);
					log("Scores retrieved successfully", "Online");
				}
				else log("Error checking scores - code: " + status, "Online");

				log("Finished checking scores", "Online");
				cleanUp();
			});

			thread.launch();
		}
		void startSendScore(const string& mName, const string& mValidator, float mDifficulty, float mScore)
		{
			if(!getOnline()) { log("Online disabled, aborting", "Online"); return; }

			ThreadWrapper& thread = memoryManager.create([=]
			{
				log("Submitting score...", "Online");

				string scoreString{toStr(mScore)};

				TcpSocket socket;
				Packet packet0x00, packet0x10;
				packet0x00 << int8_t{0x00} << (string)mValidator << (float)mDifficulty << (string)mName << (float)mScore << (string)HG_ENCRYPTIONKEY;
				socket.connect(hostIp, hostPort); socket.send(packet0x00); socket.receive(packet0x10);
				uint8_t packetID, pass;
				if(packet0x10 >> packetID >> pass)
				{
					if(packetID == 0x10 && pass == 0) log("Score successfully sumbitted", "Online");
					else log("Error: could not submit score", "Online");
				}
				socket.disconnect();

				log("Finished submitting score", "Online");
				startCheckScores();
				cleanUp();
			});

			ThreadWrapper& checkThread = memoryManager.create([&thread]
			{
				log("Checking score submission validity...", "Online");

				while(serverVersion == -1)
				{
					log("Can't submit score - version not checked, retrying...", "Online");
					sleep(seconds(5)); startCheckUpdates();
				}

				if(serverVersion > getVersion()) { log("Can't send score to server - version outdated", "Online"); return; }

				log("Score submission valid - submitting", "Online");
				thread.launch();
				cleanUp();
			});

			checkThread.launch();
		}
		void startGetScores(string& mTargetScores, string& mTargetPlayerScore, const string& mName, const string& mValidator, float mDifficulty)
		{
			if(!getOnline()) { log("Online disabled, aborting", "Online"); return; }

			ThreadWrapper& thread = memoryManager.create([=, &mTargetScores, &mTargetPlayerScore]
			{
				mTargetScores = "";
				mTargetPlayerScore = "";

				log("Getting scores from server...", "Online");

				TcpSocket socket;
				Packet packet0x01, packet0x11;
				packet0x01 << int8_t{0x01} << (string)mValidator << (float)mDifficulty << (string)mName;
				socket.connect(hostIp, hostPort); socket.send(packet0x01); socket.receive(packet0x11);
				uint8_t packetID, pass;
				string response[2];
				if(packet0x11 >> packetID >> pass)
				{
					if(packetID == 0x11 && pass == 0)
					{
						if(packet0x11 >> response[0] >> response[1])
						{
							log("Scores successfully received", "Online");
							if(!startsWith(response[0], "MySQL Error") && !startsWith(response[0], "NULL")) mTargetScores = response[0];
							else mTargetScores = "NULL";

							if(!startsWith(response[1], "MySQL Error") && !startsWith(response[1], "NULL")) mTargetPlayerScore = response[1];
							else mTargetPlayerScore = "NULL";
						}
						else log("Error: could not get scores", "Online");
					}
					else log("Error: could not get scores", "Online");
				}
				socket.disconnect();
				log("Finished getting scores", "Online");
				cleanUp();
			});

			thread.launch();
		}

		void cleanUp() 		{ for(auto& t : memoryManager.getItems()) if(t->getFinished()) memoryManager.del(t); memoryManager.cleanUp(); }
		void terminateAll() { for(auto& t : memoryManager.getItems()) t->terminate(); memoryManager.cleanUp(); }

		string getValidator(const string& mPackPath, const string& mLevelId, const string& mLevelRootPath, const string& mStyleRootPath, const string& mLuaScriptPath)
		{
			string luaScriptContents{getFileContents(mLuaScriptPath)};
			unordered_set<string> luaScriptNames;
			recursiveFillIncludedLuaFileNames(luaScriptNames, mPackPath, luaScriptContents);

			string toEncrypt{""};
			toEncrypt.append(mLevelId);
			toEncrypt.append(getFileContents(mLevelRootPath));
			toEncrypt.append(getFileContents(mStyleRootPath));
			toEncrypt.append(luaScriptContents);

			for(auto& luaScriptName : luaScriptNames)
			{
				string path{mPackPath + "/Scripts/" + luaScriptName};
				string contents{getFileContents(path)};
				toEncrypt.append(contents);
			}

			toEncrypt = getControlStripped(toEncrypt);

			string result{getUrlEncoded(mLevelId) + getMD5Hash(toEncrypt + HG_SKEY1 + HG_SKEY2 + HG_SKEY3)};
			return result;
		}

		float getServerVersion() 							{ return serverVersion; }
		string getServerMessage() 							{ return serverMessage; }
		Json::Value getScores(const string& mValidator) 	{ return scoresRoot[mValidator]; }
		string getMD5Hash(const string& mString) 			{ MD5 key{mString}; return key.GetHash(); }
		string getUrlEncoded(const string& mString) 		{ string result{""}; for(auto c : mString) if(isalnum(c)) result += c; return result; }
		string getControlStripped(const string& mString)	{ string result{""}; for(auto c : mString) if(!iscntrl(c)) result += c; return result; }

	}
}

