#version 460 core

layout(location = 0) in vec3 position;

layout(binding = 0) uniform GlobalUbo {
	mat4 projection;
	mat4 view;
	vec4 cameraPos;
	vec4 lightSource;
	mat4 lightViewProjections[3];
	vec4 shadowCascadeSplits;
} ubo;

layout(push_constant) uniform Push {
	mat4 modelMatrix;
	vec4 shadowData;
} push;

void main()
{
	uint cascadeIndex = uint(push.shadowData.x);
	gl_Position = ubo.lightViewProjections[cascadeIndex] * push.modelMatrix * vec4(position, 1.0);
}
