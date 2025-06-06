#pragma once

#include "audio_source.hpp"
#include "SoundDevice.hpp"
#include "SoundBuffer.hpp"

#include <unordered_map>


class AudioEngine
{
public:
	AudioEngine();

	uint32_t get_buffer(const std::string& filename);

private:
	SoundDevice device;
	// std::unordered_map<uint32_t, AudioSource> sources;
	std::unordered_map<std::string, SoundBuffer> buffers;
};