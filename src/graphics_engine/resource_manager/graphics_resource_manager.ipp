#pragma once

#include "graphics_resource_manager.hpp"

#include "graphics_engine/graphics_engine.hpp"
#include "graphics_engine/queues.hpp"
#include "shared_data_structures.hpp"

#include <fmt/core.h>


GraphicsResourceManager::GraphicsResourceManager(GraphicsEngine& engine) :
	GraphicsBufferManager(engine),
	GraphicsDescriptorManager(engine, static_cast<GraphicsBufferManager&>(*this))
{
	create_command_pool();
}

GraphicsResourceManager::~GraphicsResourceManager()
{
	vkDestroyCommandPool(GraphicsBufferManager::get_logical_device(), command_pool, nullptr);
}

void GraphicsResourceManager::create_command_pool()
{
	QueueFamilyIndices queue_family_indices = GraphicsBufferManager::get_graphics_engine().findQueueFamilies(GraphicsBufferManager::get_graphics_engine().get_physical_device());
	VkCommandPoolCreateInfo command_pool_create_info{};
	command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	command_pool_create_info.queueFamilyIndex = queue_family_indices.graphicsFamily.value();
	command_pool_create_info.flags = VkCommandPoolCreateFlagBits::VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	if (vkCreateCommandPool(GraphicsBufferManager::get_logical_device(), &command_pool_create_info, nullptr, &command_pool) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create command pool!");
	}
}

VkCommandBuffer GraphicsResourceManager::create_command_buffer()
{
	VkCommandBufferAllocateInfo allocation_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
	allocation_info.commandPool = command_pool;
	// specifies if allocated command buffers are primary or secondary command buffers, secondary can reuse primary
	allocation_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocation_info.commandBufferCount = 1;

	VkCommandBuffer command_buffer;
	if (vkAllocateCommandBuffers(GraphicsBufferManager::get_logical_device(), &allocation_info, &command_buffer) != VK_SUCCESS)
	{
		throw std::runtime_error("GraphicsResourceManager: failed to allocate command buffers!");
	}

	return command_buffer;
}