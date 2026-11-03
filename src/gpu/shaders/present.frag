#version 460
// Present pass: the guest front buffer through the display gamma ramp the game set (the SDK's swap applies it the
// same way: a 256-entry table of 10 bits per channel; here as floats, uploaded by the backend).
layout(binding = 0) uniform sampler2D front;
layout(std140, binding = 0) uniform Consts { vec4 c[256]; };
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
void main() {
  // SCENE RT 16F: a float front buffer is not clamped by its format, and the table has 256 entries.
  uvec3 i = uvec3(clamp(texture(front, uv).rgb, 0.0, 1.0) * 255.0 + 0.5);
  color = vec4(c[i.r].x, c[i.g].y, c[i.b].z, 1.0);
}
