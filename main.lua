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
-- ========================================================
-- [NEW] The Load Callback
-- ========================================================
local ffi = require("ffi")

print("[LUA] Virtual Machine Booted Successfully.")

-- 1. We define the C struct in Lua
ffi.cdef[[
    typedef struct {
        float pos[3];
        float seed;
        float vel[3];
        float pad;
    } Particle;
]]

-- 2. We grab the raw memory address from C
local raw_vram_pointer = Engine.getVRAM()

-- 3. We cast the raw memory into an array of our struct
local vram = ffi.cast("Particle*", raw_vram_pointer)

print("[LUA] Successfully mapped RTX 3050 VRAM to Lua FFI!")

-- ========================================================
-- BACK TO BASICS: Drawing a single Rotating Tetrahedron!
-- ========================================================
function love_load()
    -- We are going to hijack the first 4 particles in VRAM to represent 
    -- the 4 corners of our Tetrahedron.
    -- (The vertex shader from the previous step will automatically draw this!)
end

local time = 0

function love_update(dt)
    time = time + dt
    
    -- A simple rotating radius
    local radius = 3000.0
    local c = math.cos(time * 2.0)
    local s = math.sin(time * 2.0)

    -- Write DIRECTLY to the GPU memory from Lua!
    -- Top point
    vram[0].pos[0] = 0.0
    vram[0].pos[1] = radius
    vram[0].pos[2] = 0.0

    -- Bottom Left
    vram[1].pos[0] = -radius * 0.866 * c - (radius * 0.5 * s)
    vram[1].pos[1] = -radius * 0.5
    vram[1].pos[2] = -radius * 0.866 * s + (radius * 0.5 * c)

    -- Bottom Right
    vram[2].pos[0] = radius * 0.866 * c - (radius * 0.5 * s)
    vram[2].pos[1] = -radius * 0.5
    vram[2].pos[2] = radius * 0.866 * s + (radius * 0.5 * c)

    -- Bottom Back
    vram[3].pos[0] = radius * s
    vram[3].pos[1] = -radius * 0.5
    vram[3].pos[2] = -radius * c
end
-- function love_load()
--    print("[LUA] Running love_load()...")
--    Engine.setRelativeMode(true)
--    UpdateBasis()
--end


-- Hooked up to GLFW's cursor callback!
function love_mousemoved(x, y, dx, dy)
    Camera.yaw = Camera.yaw + (dx * 0.002)
    Camera.pitch = Camera.pitch + (dy * 0.002)
    Camera.pitch = math.max(-1.56, math.min(1.56, Camera.pitch))
    UpdateBasis()
end

print("[LUA] Engine Hooks Connected.")
