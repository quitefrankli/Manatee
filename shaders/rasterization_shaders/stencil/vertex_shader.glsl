#version 450

#include "../../library/library.glsl"

layout(location = 0) in vec3 in_position;

layout(set=RASTERIZATION_HIGH_FREQ_PER_OBJ_SET_OFFSET, binding=RASTERIZATION_OBJECT_DATA_BINDING) uniform ObjectDataBuffer
{
	ObjectData data;
} object_data;


void main()
{
	vec3 vertex_offset = normalize(in_position) * STENCIL_OFFSET;
	gl_Position = object_data.data.mvp * vec4(in_position + vertex_offset, 1.0);
}