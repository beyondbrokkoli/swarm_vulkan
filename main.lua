-- main.lua
print("[LUA] Virtual Machine Booted Successfully.")

Config = {
    window_title = "VibeEngine: Vulkan + LuaJIT",
    particle_count = 1000000,
    fullscreen = true
}

-- Our 3D Camera Object
Camera = {
    x = 0, y = 5000, z = -12000,
    yaw = 0, pitch = 0,
    fwx = 0, fwy = 0, fwz = 1,
    rtx = 1, rty = 0, rtz = 0,
    upx = 0, upy = 1, upz = 0
}

-- Ported straight from your CPU engine!
local function UpdateBasis()
    local cy, sy = math.cos(Camera.yaw), math.sin(Camera.yaw)
    local cp, sp = math.cos(Camera.pitch), math.sin(Camera.pitch)
    
    Camera.fwx, Camera.fwy, Camera.fwz = sy * cp, sp, cy * cp
    Camera.rtx, Camera.rty, Camera.rtz = cy, 0, -sy
    Camera.upx = Camera.fwy * Camera.rtz
    Camera.upy = Camera.fwz * Camera.rtx - Camera.fwx * Camera.rtz
    Camera.upz = -Camera.fwy * Camera.rtx
end

-- Lock the mouse to the center of the screen
Engine.setRelativeMode(true)
UpdateBasis()

function love_update(dt)
    local speed = 15000 * dt
    if Engine.isKeyDown(340) then speed = speed * 3 end -- Shift

    -- Move strictly along the calculated basis vectors!
    if Engine.isKeyDown(87) then -- W (Forward)
        Camera.x = Camera.x + Camera.fwx * speed
        Camera.y = Camera.y + Camera.fwy * speed
        Camera.z = Camera.z + Camera.fwz * speed
    end
    if Engine.isKeyDown(83) then -- S (Backward)
        Camera.x = Camera.x - Camera.fwx * speed
        Camera.y = Camera.y - Camera.fwy * speed
        Camera.z = Camera.z - Camera.fwz * speed
    end
    if Engine.isKeyDown(65) then -- A (Left)
        Camera.x = Camera.x - Camera.rtx * speed
        Camera.z = Camera.z - Camera.rtz * speed
    end
    if Engine.isKeyDown(68) then -- D (Right)
        Camera.x = Camera.x + Camera.rtx * speed
        Camera.z = Camera.z + Camera.rtz * speed
    end
    if Engine.isKeyDown(32) then Camera.y = Camera.y + speed end -- Space
    if Engine.isKeyDown(67) then Camera.y = Camera.y - speed end -- C
end

-- Hooked up to GLFW's cursor callback!
function love_mousemoved(x, y, dx, dy)
    Camera.yaw = Camera.yaw + (dx * 0.002)
    Camera.pitch = Camera.pitch + (dy * 0.002)
    Camera.pitch = math.max(-1.56, math.min(1.56, Camera.pitch))
    UpdateBasis()
end

print("[LUA] Engine Hooks Connected.")
