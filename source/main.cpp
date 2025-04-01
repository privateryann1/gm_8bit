#define NO_MALLOC_OVERRIDE

#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/FactoryLoader.hpp>
#include <scanning/symbolfinder.hpp>
#include <detouring/hook.hpp>
#include <iostream>
#include <iclient.h>
#include <unordered_map>
#include "ivoicecodec.h"
#include "net.h"
#include "thirdparty.h"
#include "steam_voice.h"
#include "eightbit_state.h"
#include <GarrysMod/Symbol.hpp>
#include <cstdint>
#include <cstring>
#include "opus_framedecoder.h"

#define STEAM_PCKT_SZ sizeof(uint64_t) + sizeof(CRC32_t)
#ifdef SYSTEM_WINDOWS
	#include <windows.h>

	const std::vector<Symbol> BroadcastVoiceSyms = {
#if defined ARCHITECTURE_X86
		Symbol::FromSignature("\x55\x8B\xEC\xA1****\x83\xEC\x50\x83\x78\x30\x00\x0F\x84****\x53\x8D\x4D\xD8\xC6\x45\xB4\x01\xC7\x45*****"),
		Symbol::FromSignature("\x55\x8B\xEC\x8B\x0D****\x83\xEC\x58\x81\xF9****"),
		Symbol::FromSignature("\x55\x8B\xEC\xA1****\x83\xEC\x50"),
#elif defined ARCHITECTURE_X86_64
		Symbol::FromSignature("\x48\x89\x5C\x24*\x56\x57\x41\x56\x48\x81\xEC****\x8B\xF2\x4C\x8B\xF1"),
#endif
	};
#endif

#ifdef SYSTEM_LINUX
	#include <dlfcn.h>
	const std::vector<Symbol> BroadcastVoiceSyms = {
		Symbol::FromName("_Z21SV_BroadcastVoiceDataP7IClientiPcx"),
		Symbol::FromSignature("\x55\x48\x8D\x05****\x48\x89\xE5\x41\x57\x41\x56\x41\x89\xF6\x41\x55\x49\x89\xFD\x41\x54\x49\x89\xD4\x53\x48\x89\xCB\x48\x81\xEC****\x48\x8B\x3D****\x48\x39\xC7\x74\x25"),
	};
#endif

static char decompressedBuffer[20 * 1024];
static char recompressBuffer[20 * 1024];

Net* net_handl = nullptr;
EightbitState* g_eightbit = nullptr;

typedef void (*SV_BroadcastVoiceData)(IClient* cl, int nBytes, char* data, int64 xuid);
Detouring::Hook detour_BroadcastVoiceData;

// V rot ebat tebya
lua_State* luaState = NULL;

void hook_BroadcastVoiceData(IClient* cl, uint nBytes, char* data, int64 xuid) {
	//Check if the player is in the set of enabled players.
	//This is (and needs to be) and O(1) operation for how often this function is called.
	//If not in the set, just hit the trampoline to ensure default behavior.
	int uid = cl->GetUserID();

#ifdef THIRDPARTY_LINK
	if(checkIfMuted(cl->GetPlayerSlot()+1)) {
		return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, nBytes, data, xuid);
	}
#endif

	auto& afflicted_players = g_eightbit->afflictedPlayers;
	if (g_eightbit->broadcastPackets && nBytes > sizeof(uint64_t)) {
		//Get the user's steamid64, put it at the beginning of the buffer.
		//Notice that we don't use the conveniently provided one in the voice packet. The client can manipulate that one.

#if defined ARCHITECTURE_X86
		uint64_t id64 = *(uint64_t*)((char*)cl + 181);
#else
		uint64_t id64 = *(uint64_t*)((char*)cl + 189);
#endif

		*(uint64_t*)decompressedBuffer = id64;

		//Transfer the packet data to our scratch buffer
		//This looks jank, but it's to prevent a theoretically malformed packet triggering a massive memcpy
		size_t toCopy = nBytes - sizeof(uint64_t);
		std::memcpy(decompressedBuffer + sizeof(uint64_t), data + sizeof(uint64_t), toCopy);

		//Finally we'll broadcast our new packet
 		net_handl->SendPacket(g_eightbit->ip.c_str(), g_eightbit->port, decompressedBuffer, nBytes);
	}

	if (afflicted_players.find(uid) != afflicted_players.end()) {
		IVoiceCodec* codec = std::get<0>(afflicted_players.at(uid));

		if(nBytes < STEAM_PCKT_SZ) {
			return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, nBytes, data, xuid);
		}

		int bytesDecompressed = SteamVoice::DecompressIntoBuffer(codec, data, nBytes, decompressedBuffer, sizeof(decompressedBuffer));
		int samples = bytesDecompressed / 2;
		if (bytesDecompressed <= 0) {
			//Just hit the trampoline at this point.
			return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, nBytes, data, xuid);
		}

		// Apply pitch shifting if enabled
		if (g_eightbit->pitchShift != 1.0f && g_eightbit->pitchShift > 0.1f) {
			// Create temporary buffer
			int16_t* tempBuffer = new int16_t[samples];
			int16_t* pcm = reinterpret_cast<int16_t*>(decompressedBuffer);

			if (g_eightbit->pitchShift > 1.0f) {
				// For higher pitch, use linear interpolation to avoid discontinuities
				float stretchFactor = g_eightbit->pitchShift;

				for (int i = 0; i < samples; i++) {
					// Calculate source position
					float srcPos = i * stretchFactor;

					// Wrap the source position if it exceeds buffer length
					srcPos = fmod(srcPos, static_cast<float>(samples));

					int idx1 = static_cast<int>(srcPos);
					int idx2 = (idx1 + 1) % samples; // Wrap idx2 if needed

					// Linear interpolation
					float frac = srcPos - idx1;
					tempBuffer[i] = static_cast<int16_t>(pcm[idx1] * (1.0f - frac) + pcm[idx2] * frac);
				}
			} else {
				// Lower pitch (original approach that works well)
				for (int i = 0; i < samples; i++) {
					float pos = i * g_eightbit->pitchShift;
					int idx1 = static_cast<int>(pos);
					int idx2 = idx1 + 1;

					if (idx2 >= samples) idx2 = samples - 1;

					float frac = pos - idx1;
					tempBuffer[i] = static_cast<int16_t>(pcm[idx1] * (1.0f - frac) + pcm[idx2] * frac);
				}
			}

			// Copy back to original buffer
			memcpy(decompressedBuffer, tempBuffer, samples * sizeof(int16_t));
			delete[] tempBuffer;
		}

		#ifdef _DEBUG
			std::cout << "Decompressed samples " << samples << std::endl;
		#endif

		GarrysMod::Lua::ILuaBase* LAU = luaState->luabase;
		LAU->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);  // +1
			LAU->GetField(-1, "hook");                      // +1
				LAU->GetField(-1, "Run");                       // +1
				LAU->PushString("ApplyVoiceEffect");            // +1

        			LAU->PushNumber(uid);
				LAU->CreateTable();
				for (int i = 0; i < samples; ++i) {
				    LAU->PushNumber(i + 1);
				    LAU->PushNumber(static_cast<double>(static_cast<int16_t>(reinterpret_cast<uint16_t*>(decompressedBuffer)[i]) - 32768));
				    LAU->SetTable(-3);
				}
				LAU->PushNumber(samples);

				if (LAU->PCall(4, 1, 0) != 0) {
					Warning("[eightbit_module error] %s\n", LAU->GetString());
					LAU->Pop();
				}

				if (LAU->GetType(-1) == GarrysMod::Lua::Type::Table) {
				    for (int i = 0; i < samples; ++i) {
				        LAU->PushNumber(i + 1);
				        LAU->GetTable(-2);

				        double luaValue = LAU->GetNumber(-1);
				        int16_t signedSample = static_cast<int16_t>(luaValue);
				        reinterpret_cast<uint16_t*>(decompressedBuffer)[i] = static_cast<uint16_t>(signedSample + 32768);

				        LAU->Pop();
				    }
				}

				LAU->Pop();
			LAU->Pop();
		LAU->Pop();

		//Recompress the stream
		uint64_t steamid = *(uint64_t*)data;
		int bytesWritten = SteamVoice::CompressIntoBuffer(steamid, codec, decompressedBuffer, samples*2, recompressBuffer, sizeof(recompressBuffer), g_eightbit->sample_rate);
		if (bytesWritten <= 0) {
			return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, nBytes, data, xuid);
		}

		#ifdef _DEBUG
			std::cout << "Retransmitted pckt size: " << bytesWritten << std::endl;
		#endif

		//Broadcast voice data with our updated compressed data.
		return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, bytesWritten, recompressBuffer, xuid);
	}
	else {
		return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, nBytes, data, xuid);
	}
}

LUA_FUNCTION_STATIC(eightbit_setbroadcastip) {
	g_eightbit->ip = std::string(LUA->GetString());
	return 0;
}

LUA_FUNCTION_STATIC(eightbit_setbroadcastport) {
	g_eightbit->port = (uint16_t)LUA->GetNumber(1);
	return 0;
}

LUA_FUNCTION_STATIC(eightbit_broadcast) {
	g_eightbit->broadcastPackets = LUA->GetBool(1);
	return 0;
}

LUA_FUNCTION_STATIC(eightbit_setsamplerate) {
	g_eightbit->sample_rate = LUA->GetNumber(1);
	return 0;
}

LUA_FUNCTION_STATIC(eightbit_enableEffects) {
	int id = LUA->GetNumber(1);
	int eff = LUA->GetNumber(2);

	auto& afflicted_players = g_eightbit->afflictedPlayers;
	if (afflicted_players.find(id) != afflicted_players.end()) {
		if (eff == 0) {
			IVoiceCodec* codec = std::get<0>(afflicted_players.at(id));
			delete codec;
			afflicted_players.erase(id);
		}
		else {
			std::get<1>(afflicted_players.at(id)) = eff;
		}
		return 0;
	}
	else if(eff != 0) {
		IVoiceCodec* codec = new SteamOpus::Opus_FrameDecoder();
		codec->Init(5, g_eightbit->sample_rate);
		afflicted_players.insert(std::pair<int, std::tuple<IVoiceCodec*, int>>(id, std::tuple<IVoiceCodec*, int>(codec, eff)));
	}
	return 0;
}

LUA_FUNCTION_STATIC(eightbit_setpitch) {
    g_eightbit->pitchShift = LUA->GetNumber(1);
    return 0;
}

GMOD_MODULE_OPEN()
{
	luaState = LUA->GetState();

	g_eightbit = new EightbitState();

	SourceSDK::ModuleLoader engine_loader("engine");
	SymbolFinder symfinder;

	void* sv_bcast = nullptr;

	for (const auto& sym : BroadcastVoiceSyms) {
		sv_bcast = symfinder.Resolve(engine_loader.GetModule(), sym.name.c_str(), sym.length);

		if (sv_bcast)
			break;
	}

	if (sv_bcast == nullptr) {
		LUA->ThrowError("Could not locate SV_BroadcastVoice symbol!");
	}

	detour_BroadcastVoiceData.Create(Detouring::Hook::Target(sv_bcast), reinterpret_cast<void*>(&hook_BroadcastVoiceData));
	detour_BroadcastVoiceData.Enable();

	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);

	LUA->PushString("eightbit");
	LUA->CreateTable();
		LUA->PushString("EnableEffects");
		LUA->PushCFunction(eightbit_enableEffects);
		LUA->SetTable(-3);

		LUA->PushString("EnableBroadcast");
		LUA->PushCFunction(eightbit_broadcast);
		LUA->SetTable(-3);

		LUA->PushString("SetBroadcastIP");
		LUA->PushCFunction(eightbit_setbroadcastip);
		LUA->SetTable(-3);

		LUA->PushString("SetBroadcastPort");
		LUA->PushCFunction(eightbit_setbroadcastport);
		LUA->SetTable(-3);

		LUA->PushString("SetSampleRate");
		LUA->PushCFunction(eightbit_setsamplerate);
		LUA->SetTable(-3);

		LUA->PushString("SetPitchShift");
		LUA->PushCFunction(eightbit_setpitch);
		LUA->SetTable(-3);
	LUA->SetTable(-3);
	LUA->Pop();

	net_handl = new Net();

#ifdef THIRDPARTY_LINK
	linkMutedFunc();
#endif

	return 0;
}

GMOD_MODULE_CLOSE()
{
	detour_BroadcastVoiceData.Disable();
	detour_BroadcastVoiceData.Destroy();

	for (auto& p : g_eightbit->afflictedPlayers) {
		IVoiceCodec* codec = std::get<0>(p.second);
		if (codec != nullptr) {
			delete codec;
		}
	}

	delete net_handl;
	delete g_eightbit;

	return 0;
}
