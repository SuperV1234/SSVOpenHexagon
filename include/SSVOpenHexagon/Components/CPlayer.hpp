// Copyright (c) 2013-2020 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: https://opensource.org/licenses/AFL-3.0

#pragma once

#include "SSVOpenHexagon/Utils/Ticker.hpp"

#include <SSVUtils/Core/Common/Frametime.hpp>

#include <SFML/System/Vector2.hpp>
#include <SFML/Graphics/Color.hpp>

namespace hg
{

class HexagonGame;
class CWall;
class CCustomWall;

class CPlayer
{
private:
    sf::Vector2f startPos;
    sf::Vector2f pos;
    sf::Vector2f lastPos;
    std::array<sf::Vector2f, 3> vertexPositions;
    std::vector<sf::Vector2f> pivotVertexes;

    float hue;
    float angle;
    float lastAngle;
    float size;
    float speed;
    float focusSpeed;

    bool dead;
    bool justSwapped;

    Ticker swapTimer;
    Ticker swapBlinkTimer;
    Ticker deadEffectTimer;

    float pivotRadius;
    static constexpr float pivotBorderThickness{5.f};

    void drawCommon(HexagonGame& mHexagonGame);
    void drawPivot(HexagonGame& mHexagonGame, const sf::Color& mCapColor);
    void drawPivot3D(HexagonGame& mHexagonGame, const sf::Color& mWallColor,
        const sf::Color& mCapColor);
    void drawDeathEffect(HexagonGame& mHexagonGame);
    void drawDeathEffect3D(
        HexagonGame& mHexagonGame, const sf::Color& mWallColors);

public:
    CPlayer(const sf::Vector2f& mPos, const float swapCooldown) noexcept;

    [[gnu::always_inline, nodiscard]] const sf::Vector2f&
    getPosition() const noexcept
    {
        return pos;
    }

    [[nodiscard]] float getPlayerAngle() const noexcept;

    [[gnu::always_inline, nodiscard]] const std::vector<sf::Vector2f>&
    getPivotVertexes() const noexcept
    {
        return pivotVertexes;
    }

    [[gnu::always_inline, nodiscard]] float getPivotRadius() const noexcept
    {
        return pivotRadius;
    }

    void setPlayerAngle(const float newAng) noexcept;
    void playerSwap(HexagonGame& mHexagonGame, bool mPlaySound);

    void kill(HexagonGame& mHexagonGame);

    void update(HexagonGame& mHexagonGame, const ssvu::FT mFT);
    void updateInput(HexagonGame& mHexagonGame, const ssvu::FT mFT);
    void updatePosition(const HexagonGame& mHexagonGame, const ssvu::FT mFT);

    void draw(HexagonGame& mHexagonGame, const sf::Color& mCapColor);
    void draw3D(HexagonGame& mHexagonGame, const sf::Color& mWallColor,
        const sf::Color& mCapColor);

    void setSides(unsigned int mSideNumber) noexcept
    {
        pivotVertexes.resize(mSideNumber);
    }

    [[nodiscard]] bool push(const HexagonGame& mHexagonGame, const CWall& wall,
        const sf::Vector2f& mCenterPos, ssvu::FT mFT);

    [[nodiscard]] bool push(const HexagonGame& mHexagonGame,
        const hg::CCustomWall& wall, ssvu::FT mFT);

    [[nodiscard]] bool getJustSwapped() const noexcept;
};

} // namespace hg
