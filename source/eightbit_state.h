#pragma once
#include <string>
#include <unordered_map>

struct EightbitState {
	bool broadcastPackets = false;
	bool sample_rate = 24000;

	uint16_t port = 4000;
	std::string ip = "127.0.0.1";
	std::unordered_map<int, std::tuple<IVoiceCodec*, int>> afflictedPlayers;
};
