#version 330 core

layout(location=0) in vec3 pos;
layout(location=1) in vec3 norm;
layout(location=2) in vec2 txc;

out vec2 texCoord;
out vec3 interpolatedNormal;
out vec3 vertPos;

uniform mat4 mvp;
uniform mat4 mv;
uniform mat3 mvNorms;

void main()
{
    texCoord = txc;
    interpolatedNormal = normalize(vec3(mvNorms * norm));
    vec4 vertPos4 = mv * vec4(pos, 1);
    vertPos = vec3(vertPos4) / vertPos4.w;
    gl_Position = mvp * vec4 (pos, 1);
}
