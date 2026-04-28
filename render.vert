#version 450

struct Particle { vec3 pos; float seed; vec3 vel; float pad; };
// Binding 0: The exact same VRAM buffer the Compute Shader just updated!
layout(std430, binding = 0) readonly buffer SwarmBuffer { Particle particles[]; };

layout(push_constant) uniform Camera {
    vec3 pos; float pad1;
    vec3 fwd; float pad2;
    vec3 right; float pad3;
    vec3 up; float fov;
    float w; float h;
} cam;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragColor;

void main() {
    // 1. Procedurally generate a Quad (6 vertices / 2 Triangles)
    vec2 uvs[6] = vec2[](vec2(0,0), vec2(1,0), vec2(0,1), vec2(0,1), vec2(1,0), vec2(1,1));
    vec2 quad[6] = vec2[](vec2(-0.5,-0.5), vec2(0.5,-0.5), vec2(-0.5,0.5), vec2(-0.5,0.5), vec2(0.5,-0.5), vec2(0.5,0.5));

    fragUV = uvs[gl_VertexIndex];
    vec2 vertexOffset = quad[gl_VertexIndex];

    // 2. Fetch the specific Particle from VRAM
    Particle p = particles[gl_InstanceIndex];

    // 3. Project into Camera Space (Your exact CPU math!)
    vec3 d = p.pos - cam.pos;
    float cz = dot(d, cam.fwd);

    if (cz < 0.1) {
        gl_Position = vec4(0.0, 0.0, 0.0, 0.0); // Cull behind camera
        return;
    }

    float f = cam.fov / cz;
    float cx = dot(d, cam.right) * f;
    float cy = dot(d, cam.up) * f;

    // Convert to Vulkan NDC Space (-1.0 to 1.0)
    float ndc_x = cx / (cam.w * 0.5);
    float ndc_y = cy / (cam.h * 0.5);

    // Scale the Quad based on depth
    float size = 50.0 * f; 
    ndc_x += (vertexOffset.x * size) / cam.w;
    ndc_y += (vertexOffset.y * size) / cam.h;

    gl_Position = vec4(ndc_x, ndc_y, 0.5, 1.0);

    // Color it based on velocity speed!
    float speed = length(p.vel) * 0.001;
    fragColor = vec3(0.1 + speed, 0.6, 1.0); // Cyan to White
}
