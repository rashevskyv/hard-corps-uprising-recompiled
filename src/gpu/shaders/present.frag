#version 460
layout(binding = 0) uniform sampler2D front;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
void main() {
  color = vec4(texture(front, uv).rgb, 1.0);
}
