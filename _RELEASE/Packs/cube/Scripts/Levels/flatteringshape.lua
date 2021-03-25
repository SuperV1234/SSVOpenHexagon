-- include useful files
u_execDependencyScript("ohvrvanilla", "base", "vittorio romeo", "utils.lua")
u_execDependencyScript("ohvrvanilla", "base", "vittorio romeo", "common.lua")
u_execDependencyScript("ohvrvanilla", "base", "vittorio romeo", "commonpatterns.lua")

-- this function adds a pattern to the timeline based on a key
function addPattern(mKey)
    if mKey == 1 and l_getSides() == 5 then
        -- mirror spiral looks bad with 5 sides
        mKey = 5
    end

    if mKey == 7 and l_getSides() == 5 then
        -- mirror wall strip looks bad with 5 sides
        mKey = 5
    end

        if mKey == 0 then pAltBarrage(u_rndInt(2, 4), 2)
    elseif mKey == 1 then pMirrorSpiral(u_rndInt(3, 6), 0)
    elseif mKey == 2 then pBarrageSpiral(u_rndInt(0, 3), 1, 1)
    elseif mKey == 3 then pBarrageSpiral(u_rndInt(0, 2), 1.2, 2)
    elseif mKey == 4 then pBarrageSpiral(2, 0.7, 1)
    elseif mKey == 5 then pInverseBarrage(0)
    elseif mKey == 6 then pTunnel(u_rndInt(1, 3))
    elseif mKey == 7 then pMirrorWallStrip(1, 0)
    elseif mKey == 8 then pSpiral(l_getSides() * u_rndInt(1, 2), 0)
    end
end

-- shuffle the keys, and then call them to add all the patterns
-- shuffling is better than randomizing - it guarantees all the patterns will be called
keys = { 0, 0, 1, 1, 2, 2, 3, 4, 5, 5, 6, 7, 7, 8 }
shuffle(keys)
index = 0
achievementUnlocked = false

-- onInit is an hardcoded function that is called when the level is first loaded
function onInit()
    l_setSpeedMult(1.74)
    l_setSpeedInc(0.18)
    l_setSpeedMax(6.0)
    l_setRotationSpeed(0.13)
    l_setRotationSpeedMax(1)
    l_setRotationSpeedInc(0.04)
    l_setDelayMult(1.0)
    l_setDelayInc(0.0075)
    l_setDelayMax(1.165)
    l_setFastSpin(0.0)
    l_setSides(6)
    l_setSidesMin(5)
    l_setSidesMax(6)
    l_setIncTime(15)

    l_setPulseMin(74.73)
    l_setPulseMax(91)
    l_setPulseSpeed(1.5)
    l_setPulseSpeedR(0.6)
    l_setPulseDelayMax(11)
    -- ((91 - 74.73) / 1.5) + 11 + ((91 - 74.73) / 0.6) = 48.963 ~= 24.489 * 2 ~= 48.978
    -- (pulseDiff / pulseSpeed) + pulseDelayMax + (pulseDiff / pulseSpeedR) = 3600/BPM * C

    l_setBeatPulseMax(17)
    l_setBeatPulseDelayMax(24.489) -- BPM is 147

    enableSwapIfDMGreaterThan(3)
    disableIncIfDMGreaterThan(3)
end

-- onLoad is an hardcoded function that is called when the level is started/restarted
function onLoad()
end

-- onStep is an hardcoded function that is called when the level timeline is empty
-- onStep should contain your pattern spawning logic
function onStep()
    addPattern(keys[index])
    index = index + 1

    if index - 1 == #keys then
        index = 1
        shuffle(keys)
    end
end

-- onIncrement is an hardcoded function that is called when the level difficulty is incremented
function onIncrement()
    enableSwapIfSpeedGEThan(5);
end

-- onUnload is an hardcoded function that is called when the level is closed/restarted
function onUnload()
end

-- onUpdate is an hardcoded function that is called every frame
function onUpdate(mFrameTime)
    if not achievementUnlocked and l_getLevelTime() > 90 and u_getDifficultyMult() >= 1 then
        steam_unlockAchievement("a2_flattering")
        achievementUnlocked = true
    end
end
