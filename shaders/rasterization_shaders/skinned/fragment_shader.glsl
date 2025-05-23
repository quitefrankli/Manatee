#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "../../library/library.glsl"

// note that the fragment shader receives the input as interpolated values
layout(location=0) in vec2 frag_tex_coord;
layout(location=1) in vec3 surface_normal;
layout(location=2) in vec3 frag_pos;

layout(location = 0) out vec4 out_color;

const vec3 light_color = vec3(1.0, 1.0, 1.0);

layout(set=RASTERIZATION_HIGH_FREQ_PER_SHAPE_SET_OFFSET, 
	binding=RASTERIZATION_ALBEDO_TEXTURE_DATA_BINDING) uniform sampler2D tex_sampler;

layout(set=RASTERIZATION_LOW_FREQ_SET_OFFSET, 
	binding=RASTERIZATION_GLOBAL_DATA_BINDING) uniform GlobalDataBuffer
{
	GlobalData data;
} global_data;

void main()
{
	vec3 color = texture(tex_sampler, frag_tex_coord).rgb; // note that we lose alpha channel here

	// ambient
	vec3 ambient = color * 0.03;
	
    // diffuse 
    vec3 norm = normalize(surface_normal);
    vec3 lightDir = normalize(global_data.data.light_pos - frag_pos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * color * global_data.data.lighting_scalar;
    
    // specular
    vec3 viewDir = normalize(global_data.data.view_pos - frag_pos);
	const float default_specular_factor = 16.0;
	// in phong model, specular can have value on the opposite face
	// this is not good so we only emit specular is diffuse > 0
	const float spec = diff > 0.0 ? get_bling_phong_spec(lightDir, norm, viewDir, default_specular_factor) : 0.0;
    const vec3 specular = light_color * (SPECULAR_STRENGTH * global_data.data.lighting_scalar * spec);

	out_color = vec4(ambient + diffuse + specular, 1.0);
}