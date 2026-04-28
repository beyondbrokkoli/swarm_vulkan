-- main.lua
print("[LUA] Virtual Machine Booted Successfully.")

Config = {
    window_title = "VibeEngine: Vulkan + LuaJIT",
    particle_count = 1000000,
    fullscreen = true
}

-- Global Camera State
Camera = { x = 0, y = 5000, z = -12000 }

-- The Callback!
function love_update(dt)
    local speed = 15000 * dt
    
    -- Sprint!
    if Engine.isKeyDown(340) then speed = speed * 3 end 

    -- Fly around the Swarm!
    if Engine.isKeyDown(87) then Camera.z = Camera.z + speed end -- W
    if Engine.isKeyDown(83) then Camera.z = Camera.z - speed end -- S
    if Engine.isKeyDown(65) then Camera.x = Camera.x - speed end -- A
    if Engine.isKeyDown(68) then Camera.x = Camera.x + speed end -- D
    
    if Engine.isKeyDown(32) then Camera.y = Camera.y + speed end -- Space (Up)
    -- 'C' key is 67
    if Engine.isKeyDown(67) then Camera.y = Camera.y - speed end -- C (Down)
end

print("[LUA] Engine Hooks Connected.")
