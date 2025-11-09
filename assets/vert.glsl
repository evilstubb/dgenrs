#version 100

uniform vec4 u_color;
uniform mat4 u_mvp_matrix;
uniform mat3 u_tex_matrix;
attribute vec4 a_color;
attribute vec4 a_position;
attribute vec2 a_texcoord;
varying vec4 v_color;
varying vec2 v_texcoord;

void main() {
  gl_Position = u_mvp_matrix * a_position;
  v_color = a_color * u_color;
  v_texcoord = (u_tex_matrix * vec3(a_texcoord, 1)).xy;
}
