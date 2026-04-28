#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    // Turn the square quad into a soft, glowing circle
    vec2 uv = fragUV - 0.5;
    float dist = length(uv);
    if (dist > 0.5) discard; 

    float intensity = 1.0 - (dist * 2.0); 
    // Premultiplied alpha for additive blending!
    outColor = vec4(fragColor * intensity, intensity); 
}
