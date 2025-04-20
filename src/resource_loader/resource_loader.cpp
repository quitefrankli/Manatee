#include "resource_loader.hpp"
#include "resource_loader_mesh.ipp"
#include "resource_loader_animation.ipp"
#include "maths.hpp"
#include "objects/object.hpp"
#include "analytics.hpp"
#include "entity_component_system/ecs.hpp"
#include "entity_component_system/skeletal.hpp"
#include "entity_component_system/mesh_system.hpp"
#include "entity_component_system/material_system.hpp"
#include "renderable/mesh.hpp"
#include "renderable/material_factory.hpp"
#include "utility.hpp"

#include <stb_image.h>
#include <tiny_gltf.h>
#include <glm/glm.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <fmt/core.h>
#include <quill/LogMacros.h>

#include <iostream>
#include <map>
#include <filesystem>


ResourceLoader ResourceLoader::global_resource_loader;

struct RawTextureDataSTB : public TextureData
{
	RawTextureDataSTB(stbi_uc* data) : data(reinterpret_cast<std::byte*>(data)) {}

	virtual ~RawTextureDataSTB() override
	{
		stbi_image_free(data); 
	}

	virtual std::byte* get() override 
	{ 
		return data; 
	}

private:
	std::byte* data;
};

struct RawTextureDataGLTF : public TextureData
{
	RawTextureDataGLTF(std::vector<unsigned char>&& data) :
		data(std::move(data))
	{
	}

	virtual std::byte* get() override 
	{ 
		return reinterpret_cast<std::byte*>(data.data()); 
	}

private:
	std::vector<unsigned char> data;
};

MaterialID ResourceLoader::fetch_texture(const std::string_view file)
{
	if (global_resource_loader.texture_name_to_mat_id.contains(file.data()))
	{
		return global_resource_loader.texture_name_to_mat_id[file.data()];
	}

	return global_resource_loader.load_texture(file);
}

std::vector<uint32_t> load_indices(const tinygltf::Accessor& index_accessor, 
								   const tinygltf::BufferView& index_buffer_view, 
								   const tinygltf::Buffer& index_buffer)
{
	std::vector<uint32_t> indices(index_accessor.count);
	if (index_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
	{
		const auto* index_data = 
			reinterpret_cast<const uint16_t*>(&index_buffer.data[index_accessor.byteOffset + index_buffer_view.byteOffset]);
		for (size_t i = 0; i < index_accessor.count; ++i)
		{
			indices[i] = index_data[i];
		}
	} else
	{
		const auto* index_data = 
			reinterpret_cast<const uint32_t*>(&index_buffer.data[index_accessor.byteOffset + index_buffer_view.byteOffset]);
		std::memcpy(indices.data(), index_data, indices.size() * sizeof(indices[0]));
	}

	return indices;
}

MaterialID ResourceLoader::load_material(const tinygltf::Primitive& primitive, tinygltf::Model& model)
{
	if (primitive.material >= 0) // if it contains a material
	{
		const auto& mat = model.materials[primitive.material];
		const auto& color_texture = mat.pbrMetallicRoughness.baseColorTexture;
		if (color_texture.index >= 0)
		{
			// has color texture
			tinygltf::Image& image = model.images[color_texture.index];
			TextureMaterial new_material;
			new_material.width = image.width;
			new_material.height = image.height;
			new_material.channels = image.component;
			new_material.data = std::make_unique<RawTextureDataGLTF>(std::move(image.image));

			return MaterialSystem::add(std::make_unique<TextureMaterial>(std::move(new_material)));
		} else
		{
			ColorMaterial new_material;
			new_material.data.diffuse = glm::vec3(
				mat.pbrMetallicRoughness.baseColorFactor[0],
				mat.pbrMetallicRoughness.baseColorFactor[1],
				mat.pbrMetallicRoughness.baseColorFactor[2]);
			new_material.data.ambient = new_material.data.diffuse;
			new_material.data.specular = (new_material.data.specular + new_material.data.diffuse)/2.0f;
			new_material.data.shininess = 1 - mat.pbrMetallicRoughness.roughnessFactor;

			return MaterialSystem::add(std::make_unique<ColorMaterial>(std::move(new_material)));
		}
	}

	return MaterialFactory::fetch_preset(EMaterialPreset::PLASTIC);
}

static std::vector<Bone> load_bones(const tinygltf::Model& model)
{
	if (model.skins.size() != 1)
	{
		throw std::runtime_error("ResourceLoader: only one skin is supported");
	}
	const auto& skin = model.skins[0];

	const auto& inverse_bind_matrices_accessor = model.accessors[skin.inverseBindMatrices];

	if (inverse_bind_matrices_accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || inverse_bind_matrices_accessor.type != TINYGLTF_TYPE_MAT4)
	{
		throw std::runtime_error("ResourceLoader: only float mat4 inverse bind matrices are supported");
	}

	const auto& inverse_bind_matrices_buffer_view = model.bufferViews[inverse_bind_matrices_accessor.bufferView];
	const auto& inverse_bind_matrices_buffer = model.buffers[inverse_bind_matrices_buffer_view.buffer];
	const auto* inverse_bind_matrices_data = reinterpret_cast<const float*>(
		&inverse_bind_matrices_buffer.data[inverse_bind_matrices_accessor.byteOffset + inverse_bind_matrices_buffer_view.byteOffset]);

	const std::map<int, int> node_to_joint = [&skin]()
	{
		std::map<int, int> retval;
		for (int i = 0; i < skin.joints.size(); i++)
		{
			retval[skin.joints[i]] = i;
		}
		return retval;
	}();
	std::vector<Bone> bones(skin.joints.size());
	for (int i = 0; i < bones.size(); i++)
	{
		const auto joint_idx = skin.joints[i];
		const auto& node = model.nodes[joint_idx];
		Bone& bone = bones[i];

		bone.name = node.name;
		bone.inverse_bind_pose.set_mat4(glm::make_mat4(&inverse_bind_matrices_data[i*sizeof(glm::mat4)/sizeof(float)]));

		for (int child : node.children)
		{
			bones[node_to_joint.at(child)].parent_node = i;
		}

		if (!node.translation.empty())
		{
			bone.relative_transform.set_pos(glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
		}

		if (!node.rotation.empty())
		{
			bone.relative_transform.set_orient(glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]));
		}

		if (!node.scale.empty())
		{
			bone.relative_transform.set_scale(glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
		}
		
		bone.original_transform = bone.relative_transform;
	}

	return bones;
}

ResourceLoader::LoadedModel ResourceLoader::load_model(const std::string_view file)
{
	std::filesystem::path file_path(file);
	tinygltf::Model model;
	std::string err;
	std::string warn;
	tinygltf::TinyGLTF loader;
	LoadedModel retval;

	if (file_path.extension().string() == ".gltf")
	{
		if (!loader.LoadASCIIFromFile(&model, &err, &warn, file_path.string()))
		{
			throw std::runtime_error(fmt::format(
				"ResourceLoader::load_model: failed to load model: {}, err {}, warn {}", 
				file,
				err,
				warn));
		}
	} else
	{
		if (!loader.LoadBinaryFromFile(&model, &err, &warn, file_path.string()))
		{
			throw std::runtime_error(fmt::format(
				"ResourceLoader::load_model: failed to load model: {}, err {}, warn {}", 
				file,
				err,
				warn));
		}
	}

	if (model.meshes.empty())
	{
		throw std::runtime_error("ResourceLoader::load_model: no meshes found");
	}

	if (model.scenes.size() != 1 && model.scenes[0].nodes.size() != 1)
	{
		throw std::runtime_error("ResourceLoader::load_model: only one scene with one node is supported");
	}

	const auto& scene = model.scenes[0];
	const auto& root_node = model.nodes[scene.nodes[0]];
	if (!root_node.matrix.empty())
	{
		retval.onload_transform.set_mat4(glm::make_mat4(root_node.matrix.data()));
	} else
	{
		if (!root_node.translation.empty())
		{
			retval.onload_transform.set_pos(glm::vec3(root_node.translation[0], root_node.translation[1], root_node.translation[2]));
		}

		if (!root_node.rotation.empty())
		{
			retval.onload_transform.set_orient(glm::quat(root_node.rotation[3], root_node.rotation[0], root_node.rotation[1], root_node.rotation[2]));
		}

		if (!root_node.scale.empty())
		{
			retval.onload_transform.set_scale(glm::vec3(root_node.scale[0], root_node.scale[1], root_node.scale[2]));
		}
	}

	std::vector<Bone> bones;

	const bool has_bones = [&model](){ return !model.skins.empty(); }();
	if (has_bones)
	{
		bones = load_bones(model);
	}

	for (auto& mesh : model.meshes)
	{
		if (mesh.primitives.size() != 1)
		{
			LOG_ERROR(Utility::get_logger(), "ResourceLoader::load_model: only one primitive per mesh is supported!");
		}

		auto& primitive = mesh.primitives[0];
		if (primitive.mode != TINYGLTF_MODE_TRIANGLES)
		{
			throw std::runtime_error("ResourceLoader::load_model: only triangles are supported");
		}

		if (primitive.indices < 0)
		{
			throw std::runtime_error("ResourceLoader::load_model: no indices found");
		}

		const auto& index_accessor = model.accessors[primitive.indices];
		const auto& index_buffer_view = model.bufferViews[index_accessor.bufferView];
		const auto& index_buffer = model.buffers[index_buffer_view.buffer];

		std::vector<uint32_t> indices = load_indices(index_accessor, index_buffer_view, index_buffer);

		const bool has_texture = [&primitive](){ return primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end(); }();

		Renderable renderable;
		MeshPtr new_mesh;
		if (has_bones)
		{
			new_mesh = std::make_unique<SkinnedMesh>(load_vertices<SkinnedVertices>(model, primitive), std::move(indices));
			if (!model.animations.empty())
			{
				retval.animations = load_animations(model, bones);
			}
			renderable.skeleton_id = ECS::get().add_skeleton(bones);
			renderable.pipeline_render_type = ERenderType::SKINNED;
		} else if (has_texture)
		{
			new_mesh = std::make_unique<TexMesh>(load_vertices<TexVertices>(model, primitive), std::move(indices));
			renderable.pipeline_render_type = ERenderType::STANDARD;
		} else 
		{
			new_mesh = std::make_unique<ColorMesh>(load_vertices<ColorVertices>(model, primitive), std::move(indices));
			renderable.pipeline_render_type = ERenderType::COLOR;
		}

		const auto mesh_id = MeshSystem::add(std::move(new_mesh));
		const auto mat_id = global_resource_loader.load_material(primitive, model);

		renderable.mesh_id = mesh_id;
		renderable.material_ids = { mat_id };
		retval.renderables.push_back(renderable);
	}

	return retval;
}

MaterialID ResourceLoader::load_texture(const std::string_view filename) 
{
	if (!std::filesystem::exists(filename))
	{
		throw std::runtime_error(fmt::format("ResourceLoader::load_texture: filename does not exist! {}", filename));
	}

	TextureMaterial material;
	material.data = std::make_unique<RawTextureDataSTB>(stbi_load(
		filename.data(), 
		(int*)(&material.width), 
		(int*)(&material.height), 
		(int*)(&material.channels), 
		STBI_rgb_alpha));
	
	// Since we are using STBI_rgb_alpha, to keep things simple we assume everything uses RGBA
	// Even if the actual image doesn't have an alpha channel
	assert(material.channels == 4 || material.channels == 3);
	material.channels = 4;

	if (!material.data.get())
	{
		throw std::runtime_error(fmt::format("failed to load texture image! {}", filename));
	}

	return MaterialSystem::add(std::make_unique<TextureMaterial>(std::move(material)));
}