// Copyright (c) 2013-2020 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: https://opensource.org/licenses/AFL-3.0

#include "SSVOpenHexagon/Global/Audio.hpp"

#include "SSVOpenHexagon/Global/Assert.hpp"

#include "SSVOpenHexagon/Utils/Concat.hpp"
#include "SSVOpenHexagon/Utils/UniquePtr.hpp"

#include <SSVStart/SoundPlayer/SoundPlayer.hpp>

#include <SSVUtils/Core/Log/Log.hpp>

#include <SFML/Audio/SoundBuffer.hpp>
#include <SFML/Audio/Music.hpp>

#include <optional>
#include <string>

namespace hg {

class Audio::AudioImpl
{
private:
    // TODO (P2): cleaner way of doing this
    SoundBufferGetter _soundBufferGetter;
    MusicPathGetter _musicPathGetter;

    // TODO (P2): remove these, roll own system
    ssvs::SoundPlayer _soundPlayer;

    std::optional<sf::Music> _music;
    float _musicVolume;
    std::string _lastLoadedMusicPath;


    void playSoundImpl(
        const std::string& assetId, const ssvs::SoundPlayer::Mode mode)
    {
        if(sf::SoundBuffer* soundBuffer = _soundBufferGetter(assetId);
            soundBuffer != nullptr)
        {
            _soundPlayer.play(*soundBuffer, mode);
        }
    }

public:
    explicit AudioImpl(const SoundBufferGetter& soundBufferGetter,
        const MusicPathGetter& musicPathGetter)
        : _soundBufferGetter{soundBufferGetter},
          _musicPathGetter{musicPathGetter},
          _soundPlayer{},
          _music{},
          _musicVolume{100.f},
          _lastLoadedMusicPath{}
    {
        SSVOH_ASSERT(static_cast<bool>(_soundBufferGetter));
    }

    [[nodiscard]] int32_t getCurrentMusicTime() {
        return _music->getPlayingOffset().asMilliseconds();
    }

    void setSoundVolume(const float volume)
    {
        SSVOH_ASSERT(volume >= 0.f && volume <= 100.f);
        _soundPlayer.setVolume(volume);
    }

    void setMusicVolume(const float volume)
    {
        SSVOH_ASSERT(volume >= 0.f && volume <= 100.f);
        _musicVolume = volume;

        if(_music.has_value())
        {
            _music->setVolume(_musicVolume);
        }
    }

    void resumeMusic()
    {
        if(_music.has_value())
        {
            _music->setVolume(_musicVolume);
            _music->play();
        }
    }

    void pauseMusic()
    {
        if(_music.has_value())
        {
            _music->pause();
        }
    }

    void stopMusic()
    {
        if(_music.has_value())
        {
            _music->stop();
        }
    }

    void stopSounds()
    {
        _soundPlayer.stop();
    }

    void playSoundOverride(const std::string& id)
    {
        playSoundImpl(id, ssvs::SoundPlayer::Mode::Override);
    }

    void playPackSoundOverride(const std::string& packId, const std::string& id)
    {
        playSoundImpl(
            Utils::concat(packId, '_', id), ssvs::SoundPlayer::Mode::Override);
    }

    void playSoundAbort(const std::string& id)
    {
        playSoundImpl(id, ssvs::SoundPlayer::Mode::Abort);
    }

    void playPackSoundAbort(const std::string& packId, const std::string& id)
    {
        playSoundImpl(
            Utils::concat(packId, '_', id), ssvs::SoundPlayer::Mode::Abort);
    }

    [[nodiscard]] bool loadAndPlayMusic(const std::string& packId,
        const std::string& id, const float playingOffsetSeconds)
    {
        const std::string assetId = Utils::concat(packId, '_', id);
        const std::string* path = _musicPathGetter(assetId);

        if(path == nullptr)
        {
            ssvu::lo("hg::AudioImpl::playMusic")
                << "No path for music id '" << assetId << "'\n";

            return false;
        }

        if(!_music.has_value())
        {
            _music.emplace();
        }

        if(_lastLoadedMusicPath != *path)
        {
            if(!_music->openFromFile(*path))
            {
                ssvu::lo("hg::AudioImpl::playMusic")
                    << "Failed loading music file '" << path << "'\n";

                _music.reset();
                return false;
            }

            _lastLoadedMusicPath = *path;
        }

        _music->setLoop(true);
        _music->setPlayingOffset(sf::seconds(playingOffsetSeconds));
        resumeMusic();

        return true;
    }

    void setCurrentMusicPitch(const float pitch)
    {
        if(_music.has_value())
        {
            _music->setPitch(pitch);
        }
    }
};

[[nodiscard]] const Audio::AudioImpl& Audio::impl() const noexcept
{
    SSVOH_ASSERT(_impl != nullptr);
    return *_impl;
}

[[nodiscard]] Audio::AudioImpl& Audio::impl() noexcept
{
    SSVOH_ASSERT(_impl != nullptr);
    return *_impl;
}

Audio::Audio(const SoundBufferGetter& soundBufferGetter,
    const MusicPathGetter& musicPathGetter)
    : _impl{Utils::makeUnique<AudioImpl>(soundBufferGetter, musicPathGetter)}
{}

Audio::~Audio() = default;

[[nodiscard]] int32_t Audio::getCurrentMusicTime() {
    return impl().getCurrentMusicTime();
}

void Audio::setSoundVolume(const float volume)
{
    impl().setSoundVolume(volume);
}

void Audio::setMusicVolume(const float volume)
{
    impl().setMusicVolume(volume);
}

void Audio::resumeMusic()
{
    impl().resumeMusic();
}

void Audio::pauseMusic()
{
    impl().pauseMusic();
}

void Audio::stopMusic()
{
    impl().stopMusic();
}

void Audio::stopSounds()
{
    impl().stopSounds();
}

void Audio::playSoundOverride(const std::string& id)
{
    impl().playSoundOverride(id);
}

void Audio::playPackSoundOverride(
    const std::string& packId, const std::string& id)
{
    impl().playPackSoundOverride(packId, id);
}

void Audio::playSoundAbort(const std::string& id)
{
    impl().playSoundAbort(id);
}

void Audio::playPackSoundAbort(const std::string& packId, const std::string& id)
{
    impl().playPackSoundAbort(packId, id);
}

[[nodiscard]] bool Audio::loadAndPlayMusic(const std::string& packId,
    const std::string& id, const float playingOffsetSeconds)
{
    return impl().loadAndPlayMusic(packId, id, playingOffsetSeconds);
}

void Audio::setCurrentMusicPitch(const float pitch)
{
    impl().setCurrentMusicPitch(pitch);
}

} // namespace hg
