// Copyright (c) 2013 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: http://opensource.org/licenses/AFL-3.0

#include <iostream>
#include <fstream>
#include <memory>
#include <SSVStart/Json/UtilsJson.h>
#include "SSVOpenHexagon/Global/Config.h"
#include "SSVOpenHexagon/Global/Assets.h"
#include "SSVOpenHexagon/Utils/Utils.h"
#include "SSVOpenHexagon/Online/Online.h"

using namespace std;
using namespace sf;
using namespace ssvs;
using namespace ssvs::Input;
using namespace ssvs::Utils;
using namespace ssvu::FileSystem;
using namespace ssvuj;
using namespace ssvu;

namespace hg
{
	ssvuj::Value root{getRootFromFile("config.json")};
	LinkedValueManager lvm{root};

	auto& online					(lvm.create<bool>("online"));
	auto& official					(lvm.create<bool>("official"));
	auto& noRotation				(lvm.create<bool>("no_rotation"));
	auto& noBackground				(lvm.create<bool>("no_background"));
	auto& noSound					(lvm.create<bool>("no_sound"));
	auto& noMusic					(lvm.create<bool>("no_music"));
	auto& blackAndWhite				(lvm.create<bool>("black_and_white"));
	auto& pulseEnabled				(lvm.create<bool>("pulse_enabled"));
	auto& _3DEnabled				(lvm.create<bool>("3D_enabled"));
	auto& _3DMultiplier				(lvm.create<float>("3D_multiplier"));
	auto& _3DMaxDepth				(lvm.create<int>("3D_max_depth"));
	auto& invincible				(lvm.create<bool>("invincible"));
	auto& autoRestart				(lvm.create<bool>("auto_restart"));
	auto& soundVolume				(lvm.create<int>("sound_volume"));
	auto& musicVolume				(lvm.create<int>("music_volume"));
	auto& flashEnabled				(lvm.create<bool>("flash_enabled"));
	auto& zoomFactor				(lvm.create<float>("zoom_factor"));
	auto& pixelMultiplier			(lvm.create<int>("pixel_multiplier"));
	auto& playerSpeed				(lvm.create<float>("player_speed"));
	auto& playerFocusSpeed			(lvm.create<float>("player_focus_speed"));
	auto& playerSize				(lvm.create<float>("player_size"));
	auto& staticFrameTime			(lvm.create<bool>("static_frametime"));
	auto& staticFrameTimeValue		(lvm.create<float>("static_frametime_value"));
	auto& limitFps					(lvm.create<bool>("limit_fps"));
	auto& vsync						(lvm.create<bool>("vsync"));
	auto& autoZoomFactor			(lvm.create<bool>("auto_zoom_factor"));
	auto& fullscreen				(lvm.create<bool>("fullscreen"));
	auto& windowedAutoResolution	(lvm.create<bool>("windowed_auto_resolution"));
	auto& fullscreenAutoResolution	(lvm.create<bool>("fullscreen_auto_resolution"));
	auto& fullscreenWidth			(lvm.create<int>("fullscreen_width"));
	auto& fullscreenHeight			(lvm.create<int>("fullscreen_height"));
	auto& windowedWidth				(lvm.create<int>("windowed_width"));
	auto& windowedHeight			(lvm.create<int>("windowed_height"));
	auto& showMessages				(lvm.create<bool>("show_messages"));
	auto& changeStyles				(lvm.create<bool>("change_styles"));
	auto& changeMusic				(lvm.create<bool>("change_music"));
	auto& debug						(lvm.create<bool>("debug"));
	auto& beatPulse					(lvm.create<bool>("beatpulse_enabled"));
	auto& showTrackedVariables		(lvm.create<bool>("show_tracked_variables"));
	auto& triggerRotateCCW			(lvm.create<Trigger>("t_rotate_ccw"));
	auto& triggerRotateCW			(lvm.create<Trigger>("t_rotate_cw"));
	auto& triggerFocus				(lvm.create<Trigger>("t_focus"));
	auto& triggerExit				(lvm.create<Trigger>("t_exit"));
	auto& triggerForceRestart		(lvm.create<Trigger>("t_force_restart"));
	auto& triggerRestart			(lvm.create<Trigger>("t_restart"));
	auto& triggerScreenshot			(lvm.create<Trigger>("t_screenshot"));
	auto& triggerSwap				(lvm.create<Trigger>("t_swap"));

	float sizeX{1500}, sizeY{1500};
	constexpr float spawnDistance{1600};
	string uneligibilityReason{""};

	void applyAutoWindowedResolution() { auto d(VideoMode::getDesktopMode()); windowedWidth = d.width; windowedHeight = d.height; }
	void applyAutoFullscreenResolution() { auto d(VideoMode::getDesktopMode()); fullscreenWidth = d.width; fullscreenHeight = d.height; }

	void loadConfig(const vector<string>& mOverridesIds)
	{
		log("loading config", "::loadConfig");

		for(const auto& p : getScan<Mode::Single, Type::File, Pick::ByExt>("ConfigOverrides/", ".json"))
		{
			const auto& fileName(getNameFromPath(p, "ConfigOverrides/", ".json"));
			if(contains(mOverridesIds, fileName))
			{
				const auto& overrideRoot(getRootFromFile(p));
				for(auto itr(begin(overrideRoot)); itr != end(overrideRoot); ++itr) root[as<string>(itr.key())] = *itr;
			}
		}

		lvm.syncFromRoot();

		if(getWindowedAutoResolution()) applyAutoWindowedResolution();
		if(getFullscreenAutoResolution()) applyAutoFullscreenResolution();

		recalculateSizes();

	}
	void saveConfig() { log("saving config", "::saveConfig"); lvm.syncToRoot(); writeRootToFile(root, "config.json"); }

	bool isEligibleForScore()
	{
		if(!getOfficial())								{ uneligibilityReason = "official mode off"; return false; }
		if(getDebug())									{ uneligibilityReason = "debug mode on"; return false; }
		if(!getAutoZoomFactor())						{ uneligibilityReason = "modified zoom factor"; return false; }
		if(getPlayerSpeed() != 9.45f)					{ uneligibilityReason = "player speed modified"; return false; }
		if(getPlayerFocusSpeed() != 4.625f)				{ uneligibilityReason = "player focus speed modified"; return false; }
		if(getPlayerSize() != 7.3f)						{ uneligibilityReason = "player size modified"; return false; }
		if(getInvincible())								{ uneligibilityReason = "invincibility on"; return false; }
		if(getNoRotation())								{ uneligibilityReason = "rotation off"; return false; }
		if(Online::getServerVersion() == -1)			{ uneligibilityReason = "connection error"; return false; }
		if(Online::getServerVersion() > getVersion())	{ uneligibilityReason = "version mismatch"; return false; }
		return true;
	}

	void recalculateSizes()
	{
		sizeX = sizeY = max(getWidth(), getHeight()) * 1.3f;
		if(!getAutoZoomFactor()) return;

		float factorX(1024.0f / static_cast<float>(getWidth())), factorY(768.0f / static_cast<float>(getHeight()));
		zoomFactor = max(factorX, factorY);
	}
	void setFullscreen(GameWindow& mWindow, bool mFullscreen)
	{
		fullscreen = mFullscreen;

		mWindow.setSize(getWidth(), getHeight());
		mWindow.setFullscreen(getFullscreen());
		mWindow.setMouseCursorVisible(false);

		recalculateSizes();
	}

	void refreshWindowSize(unsigned int mWidth, unsigned int mHeight) { windowedWidth = mWidth; windowedHeight = mHeight; }

	void setCurrentResolution(GameWindow& mWindow, unsigned int mWidth, unsigned int mHeight)
	{
		if(getFullscreen())
		{
			fullscreenAutoResolution = false;
			fullscreenWidth = mWidth;
			fullscreenHeight = mHeight;
		}
		else
		{
			windowedAutoResolution = false;
			windowedWidth = mWidth;
			windowedHeight = mHeight;
		}

		mWindow.setSize(getWidth(), getHeight());
		mWindow.setFullscreen(getFullscreen());
		mWindow.setMouseCursorVisible(false);
		recalculateSizes();
	}
	void setCurrentResolutionAuto(GameWindow& mWindow)
	{
		if(getFullscreen())
		{
			fullscreenAutoResolution = true;
			applyAutoFullscreenResolution();
		}
		else
		{
			windowedAutoResolution = true;
			applyAutoWindowedResolution();
		}

		mWindow.setSize(getWidth(), getHeight());
		mWindow.setFullscreen(getFullscreen());
		mWindow.setMouseCursorVisible(false);
		recalculateSizes();
	}

	void setOnline(bool mOnline)				{ online = mOnline; if(mOnline) Online::startCheckUpdates(); }
	void setOfficial(bool mOfficial)			{ official = mOfficial; }
	void setNoRotation(bool mNoRotation)		{ noRotation = mNoRotation; }
	void setNoBackground(bool mNoBackground)	{ noBackground = mNoBackground; }
	void setBlackAndWhite(bool mBlackAndWhite)	{ blackAndWhite = mBlackAndWhite; }
	void setNoSound(bool mNoSound)				{ noSound = mNoSound; }
	void setNoMusic(bool mNoMusic)				{ noMusic = mNoMusic; }
	void setPulse(bool mPulse) 					{ pulseEnabled = mPulse; }
	void set3D(bool m3D)						{ _3DEnabled = m3D; }
	void setInvincible(bool mInvincible)		{ invincible = mInvincible; }
	void setAutoRestart(bool mAutoRestart) 		{ autoRestart = mAutoRestart; }
	void setSoundVolume(int mVolume) 			{ soundVolume = mVolume; }
	void setMusicVolume(int mVolume) 			{ musicVolume = mVolume; }
	void setFlash(bool mFlash)					{ flashEnabled = mFlash; }

	bool getOnline()					{ return online; }
	bool getOfficial()					{ return official; }
	string getUneligibilityReason()  	{ return uneligibilityReason; }
	float getSizeX() 					{ return sizeX; }
	float getSizeY() 					{ return sizeY; }
	float getSpawnDistance() 			{ return spawnDistance; }
	float getZoomFactor() 				{ return zoomFactor; }
	int getPixelMultiplier() 			{ return pixelMultiplier; }
	float getPlayerSpeed() 				{ return getOfficial() ? 9.45f : playerSpeed; }
	float getPlayerFocusSpeed() 		{ return getOfficial() ? 4.625f : playerFocusSpeed; }
	float getPlayerSize() 				{ return getOfficial() ? 7.3f : playerSize; }
	bool getNoRotation() 				{ return getOfficial() ? false : noRotation; }
	bool getNoBackground() 				{ return getOfficial() ? false : noBackground; }
	bool getBlackAndWhite() 			{ return getOfficial() ? false : blackAndWhite; }
	bool getNoSound()					{ return noSound; }
	bool getNoMusic()					{ return noMusic; }
	int getSoundVolume()  				{ return soundVolume; }
	int getMusicVolume() 				{ return musicVolume; }
	bool getStaticFrameTime()			{ return getOfficial() ? false : staticFrameTime; }
	float getStaticFrameTimeValue()		{ return staticFrameTimeValue; }
	bool getLimitFps()					{ return limitFps; }
	bool getVsync()						{ return vsync; }
	bool getAutoZoomFactor()			{ return getOfficial() ? true : autoZoomFactor; }
	bool getFullscreen()				{ return fullscreen; }
	float getVersion() 					{ return 2.00f; }
	bool getWindowedAutoResolution()	{ return windowedAutoResolution; }
	bool getFullscreenAutoResolution() 	{ return fullscreenAutoResolution; }
	unsigned int getFullscreenWidth()	{ return fullscreenWidth; }
	unsigned int getFullscreenHeight() 	{ return fullscreenHeight; }
	unsigned int getWindowedWidth()		{ return windowedWidth; }
	unsigned int getWindowedHeight()	{ return windowedHeight; }
	unsigned int getWidth() 			{ return getFullscreen() ? getFullscreenWidth() : getWindowedWidth(); }
	unsigned int getHeight() 			{ return getFullscreen() ? getFullscreenHeight() : getWindowedHeight(); }
	bool getShowMessages()				{ return showMessages; }
	bool getChangeStyles()				{ return changeStyles; }
	bool getChangeMusic()				{ return changeMusic; }
	bool getDebug()						{ return debug; }
	bool getPulse()						{ return getOfficial() ? true : pulseEnabled; }
	bool getBeatPulse()					{ return getOfficial() ? true : beatPulse; }
	bool getInvincible()				{ return getOfficial() ? false :invincible; }
	bool get3D()						{ return _3DEnabled; }
	float get3DMultiplier()				{ return _3DMultiplier; }
	unsigned int get3DMaxDepth()		{ return _3DMaxDepth; }
	bool getAutoRestart()				{ return autoRestart; }
	bool getFlash() 					{ return flashEnabled; }
	bool getShowTrackedVariables()		{ return showTrackedVariables; }

	Trigger getTriggerRotateCCW()		{ return triggerRotateCCW; }
	Trigger getTriggerRotateCW()		{ return triggerRotateCW; }
	Trigger getTriggerFocus()			{ return triggerFocus; }
	Trigger getTriggerExit()			{ return triggerExit; }
	Trigger getTriggerForceRestart()	{ return triggerForceRestart; }
	Trigger getTriggerRestart()			{ return triggerRestart; }
	Trigger getTriggerScreenshot()		{ return triggerScreenshot; }
	Trigger getTriggerSwap()			{ return triggerSwap; }
}
