#version 450
layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    // Additive blending applied to 3D geometry!
    outColor = vec4(fragColor, 0.4); 
}
