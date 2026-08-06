#version 460 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec3 fragPosWorld;
layout(location = 1) out vec3 fragNormalWorld;
layout(location = 2) out vec2 fragUV;

layout(binding = 0) uniform GlobalUbo {
    mat4 projection;
    mat4 view;
    vec4 cameraPos;
    vec4 lightSource; // w for intencity
	mat4 lightViewProjections[3];
	vec4 shadowCascadeSplits;
	vec4 shadowSettings;
} ubo;

layout(push_constant) uniform Push {
  	mat4 modelMatrix;
	vec4 shadowData;
} push;

void main(){
  	vec4 positionWorld = push.modelMatrix * vec4(position, 1.0);
  	gl_Position = ubo.projection * ubo.view * positionWorld;
  	fragNormalWorld = normalize(mat3(push.modelMatrix) * normal);
  	fragPosWorld = positionWorld.xyz;
    fragUV = uv;
}
