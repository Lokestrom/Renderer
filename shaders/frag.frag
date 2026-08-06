#version 460 core

layout (location = 0) in vec3 fragPosWorld;
layout (location = 1) in vec3 fragNormalWorld;
layout (location = 2) in vec2 uv;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform GlobalUbo {
    mat4 projection;
    mat4 view;
    vec4 cameraPos;
    vec4 lightSource; // w for intencity
	mat4 lightViewProjections[3];
	vec4 shadowCascadeSplits; // w enables shadows
} ubo;

layout(push_constant) uniform Push {
	layout(offset = 80) vec4 color;
	layout(offset = 96) uint textureIndex;
} push;

layout(set = 1, binding = 0) uniform sampler2D textures[1024];
layout(set = 0, binding = 1) uniform sampler2DArrayShadow shadowMap;

float getShadowVisibility(uint cascadeIndex)
{
	vec4 lightClip = ubo.lightViewProjections[cascadeIndex] * vec4(fragPosWorld, 1.0);
	vec3 shadowCoord = lightClip.xyz / lightClip.w;
	shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;

	if (shadowCoord.z <= 0.0 || shadowCoord.z >= 1.0 ||
		any(lessThan(shadowCoord.xy, vec2(0.0))) || any(greaterThan(shadowCoord.xy, vec2(1.0)))) {
		return 1.0;
	}

	const vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0).xy);
	// Use a conservative hard-shadow filter: if any neighboring shadow texel is
	// occluded, shadow the fragment. This intentionally grows silhouettes by one
	// texel to prevent light gaps caused by shadow-map resolution.
	float visibility = 1.0;
	for (int x = -1; x <= 1; ++x) {
		for (int y = -1; y <= 1; ++y) {
			const vec2 offset = vec2(x, y) * texelSize;
			visibility = min(
				visibility,
				texture(shadowMap, vec4(
					shadowCoord.xy + offset,
					float(cascadeIndex),
					shadowCoord.z)));
		}
	}
	return visibility;
}

void main()
{
    vec3 N = normalize(fragNormalWorld);
    vec3 L = -ubo.lightSource.xyz;
	float NdotL = max(dot(N, L), 0.0);
	float shadowVisibility = 1.0;

	if(ubo.shadowCascadeSplits.w == 1.0 && ubo.lightSource.w != 0.0) {
		float viewDepth = (ubo.view * vec4(fragPosWorld, 1.0)).z;
		uint cascadeIndex = viewDepth < ubo.shadowCascadeSplits.x ? 0 :
			viewDepth < ubo.shadowCascadeSplits.y ? 1 : 2;
		if (viewDepth > 0.0 && viewDepth <= ubo.shadowCascadeSplits.z) {
			shadowVisibility = getShadowVisibility(cascadeIndex);
		}
	}

    vec3 diffuse = push.color.rgb * NdotL * ubo.lightSource.w * shadowVisibility;
    vec3 ambient = 0.5 * push.color.rgb;

    vec3 lighting = ambient + diffuse;

    vec3 albedo = texture(textures[push.textureIndex], uv).rgb;

    outColor = vec4(lighting * albedo, 1.0);
}
