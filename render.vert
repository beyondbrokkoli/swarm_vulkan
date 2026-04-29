#version 450

struct Particle { vec3 pos; float seed; vec3 vel; float pad; };
layout(std430, binding = 0) readonly buffer SwarmBuffer { Particle particles[]; };

layout(push_constant) uniform Camera {
    vec3 pos; float pad1;
    vec3 fwd; float pad2;
    vec3 right; float pad3;
    vec3 up; float fov;
    float w; float h;
} cam;

layout(location = 0) out vec3 fragColor;

// 1. The 4 corners of a 3D Tetrahedron
const vec3 v0 = vec3(0.0, 1.0, 0.0);       // Top
const vec3 v1 = vec3(-0.866, -0.5, 0.5);   // Bottom Left
const vec3 v2 = vec3(0.866, -0.5, 0.5);    // Bottom Right
const vec3 v3 = vec3(0.0, -0.5, -1.0);     // Bottom Back

// 2. Build the 12 Vertices (4 Triangles)
const vec3 VERTS[12] = vec3[](
    v0, v1, v2, // Front Face
    v0, v2, v3, // Right Face
    v0, v3, v1, // Left Face
    v1, v3, v2  // Bottom Face
);

// 3. Pre-calculated Normals for Lambertian Lighting
const vec3 NORMS[12] = vec3[](
    vec3(0.0, 0.447, 0.894), vec3(0.0, 0.447, 0.894), vec3(0.0, 0.447, 0.894), // Front
    vec3(0.872, 0.218, -0.436), vec3(0.872, 0.218, -0.436), vec3(0.872, 0.218, -0.436), // Right
    vec3(-0.872, 0.218, -0.436), vec3(-0.872, 0.218, -0.436), vec3(-0.872, 0.218, -0.436), // Left
    vec3(0.0, -1.0, 0.0), vec3(0.0, -1.0, 0.0), vec3(0.0, -1.0, 0.0) // Bottom
);

void main() {
    Particle p = particles[gl_InstanceIndex];
    
    // Scale the particle up
    float size = 45.0;
    vec3 localPos = VERTS[gl_VertexIndex] * size;
    vec3 localNorm = NORMS[gl_VertexIndex];

    // [THE MAGIC] Anchor the geometry in true 3D space!
    vec3 worldPos = p.pos + localPos;

    // Project into Camera Space
    vec3 d = worldPos - cam.pos;
    float cz = dot(d, cam.fwd);

    if (cz < 0.1) {
        gl_Position = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float f = cam.fov / cz;
    float cx = dot(d, cam.right) * f;
    float cy = dot(d, cam.up) * f;

    float ndc_x = cx / (cam.w * 0.5);
    // Vulkan's Y-axis points DOWN. Negate it so our Lua camera math maps perfectly!
    float ndc_y = -(cy / (cam.h * 0.5));
    
    // Calculate true depth
    float ndc_z = 1.0 - (100.0 / cz);

    gl_Position = vec4(ndc_x, ndc_y, ndc_z, 1.0);

    // --- SUN LIGHTING (From vibemath.c) ---
    vec3 sunDir = normalize(vec3(0.577, -0.577, 0.577));
    float light = max(0.15, dot(localNorm, sunDir)); 

    // Base color with a slight speed glow
    float speed = length(p.vel) * 0.0005;
    vec3 baseColor = vec3(0.1 + speed, 0.5, 1.0); // Cyan
    
    fragColor = baseColor * light;
}
