#pragma once

extern "C" {
#include <tox/tox.h>
//#include <tox.h>
#include <sodium.h>
}

#include <stdexcept>
#include <exception>
#include <thread>
#include <cassert>
#include <vector>
#include <optional>
#include <iostream>

inline std::vector<uint8_t> hex2bin(const std::string& str) {
	std::vector<uint8_t> bin{};
	bin.resize(str.size()/2, 0);

	sodium_hex2bin(bin.data(), bin.size(), str.c_str(), str.length(), nullptr, nullptr, nullptr);

	return bin;
}

inline std::string bin2hex(const std::vector<uint8_t>& bin) {
	std::string str{};
	str.resize(bin.size()*2, '?');

	// HECK, std is 1 larger than size returns ('\0')
	sodium_bin2hex(str.data(), str.size()+1, bin.data(), bin.size());

	return str;
}

inline std::string tox_get_own_address(const Tox *tox) {
	std::vector<uint8_t> self_addr{};
	self_addr.resize(TOX_ADDRESS_SIZE);

	tox_self_get_address(tox, self_addr.data());

	return bin2hex(self_addr);
}

// callbacks
inline void log_cb(Tox*, TOX_LOG_LEVEL level, const char *file, uint32_t line, const char *func, const char *message, void *);
inline void self_connection_status_cb(Tox*, TOX_CONNECTION connection_status, void *);
inline void friend_connection_status_cb(Tox *tox, uint32_t friend_number, TOX_CONNECTION connection_status, void *);
inline void friend_request_cb(Tox *tox, const uint8_t *public_key, const uint8_t *message, size_t length, void *);
inline void friend_lossy_packet_cb(Tox *tox, uint32_t friend_number, const uint8_t *data, size_t length, void*);

class ToxService {
	protected:
		const bool _tcp_only;

		Tox* _tox = nullptr;
		std::optional<uint32_t> _friend_number;

		ToxService(void) = delete;

	public:
		ToxService(bool tcp_only) : _tcp_only(tcp_only) {
			TOX_ERR_OPTIONS_NEW err_opt_new;
			Tox_Options* options = tox_options_new(&err_opt_new);
			assert(err_opt_new == TOX_ERR_OPTIONS_NEW::TOX_ERR_OPTIONS_NEW_OK);
			tox_options_set_log_callback(options, log_cb);
#ifndef USE_TEST_NETWORK
			tox_options_set_local_discovery_enabled(options, true);
#endif
			tox_options_set_udp_enabled(options, !_tcp_only);
			tox_options_set_hole_punching_enabled(options, true);
			tox_options_set_tcp_port(options, 0);

			TOX_ERR_NEW err_new;
			_tox = tox_new(options, &err_new);
			tox_options_free(options);
			if (err_new != TOX_ERR_NEW_OK) {
				throw std::runtime_error{"tox_new failed with error code " + std::to_string(err_new)};
			}

			std::cout << "created tox instance with addr:" << tox_get_own_address(_tox) << "\n";

#define CALLBACK_REG(x) tox_callback_##x(_tox, x##_cb)
			CALLBACK_REG(self_connection_status);

			CALLBACK_REG(friend_connection_status);
			CALLBACK_REG(friend_request);

			CALLBACK_REG(friend_lossy_packet);
#undef CALLBACK_REG

#if 1 // enable and fill for bootstrapping and tcp relays
			// dht bootstrap
			{
				struct DHT_node {
					const char *ip;
					uint16_t port;
					const char key_hex[TOX_PUBLIC_KEY_SIZE*2 + 1]; // 1 for null terminator
					unsigned char key_bin[TOX_PUBLIC_KEY_SIZE];
				};

				DHT_node nodes[] =
				{
					// own bootsrap node, to reduce load
					//{"tox.plastiras.org",					33445,	"8E8B63299B3D520FB377FE5100E65E3322F7AE5B20A0ACED2981769FC5B43725", {}}, // 14
					{"tox2.plastiras.org",					33445,	"B6626D386BE7E3ACA107B46F48A5C4D522D29281750D44A0CBA6A2721E79C951", {}}, // 14
				};

				for (size_t i = 0; i < sizeof(nodes)/sizeof(DHT_node); i ++) {
					sodium_hex2bin(
						nodes[i].key_bin, sizeof(nodes[i].key_bin),
						nodes[i].key_hex, sizeof(nodes[i].key_hex)-1,
						NULL, NULL, NULL
					);
					tox_bootstrap(_tox, nodes[i].ip, nodes[i].port, nodes[i].key_bin, NULL);
					// TODO: use extra tcp option to avoid error msgs
					// ... this is hardcore
					tox_add_tcp_relay(_tox, nodes[i].ip, nodes[i].port, nodes[i].key_bin, NULL);
				}
			}
#endif

		}

		~ToxService(void) {
			tox_kill(_tox);
		}

		bool add_friend(const std::string& addr) {
			auto addr_bin = hex2bin(addr);
			if (addr_bin.size() != TOX_ADDRESS_SIZE) {
				return false;
			}

			Tox_Err_Friend_Add e_fa {TOX_ERR_FRIEND_ADD_NULL};
			tox_friend_add(_tox, addr_bin.data(), reinterpret_cast<const uint8_t*>("nope"), 4, &e_fa);

			return e_fa == TOX_ERR_FRIEND_ADD_OK;
		}

		void friend_online(uint32_t friend_number) {
			_friend_number = friend_number;
		}

		virtual void handle_lossy_packet(uint32_t friend_number, const uint8_t *data, size_t length) = 0;
};

inline void log_cb(Tox*, TOX_LOG_LEVEL level, const char *file, uint32_t line, const char *func, const char *message, void *) {
	std::cerr << "TOX " << level << " " << file << ":" << line << "(" << func << ") " << message << "\n";
}

inline void self_connection_status_cb(Tox*, TOX_CONNECTION connection_status, void *) {
	std::cout << "self_connection_status_cb " << connection_status << "\n";
}

// friend
inline void friend_connection_status_cb(Tox* /*tox*/, uint32_t friend_number, TOX_CONNECTION connection_status, void* user_data) {
	std::cout << "friend_connection_status_cb " << connection_status << "\n";
	if (connection_status != TOX_CONNECTION_NONE) {
		static_cast<ToxService*>(user_data)->friend_online(friend_number);
	}
}

inline void friend_request_cb(Tox *tox, const uint8_t *public_key, const uint8_t *message, size_t length, void *) {
	std::cout << "friend_request_cb\n";

	Tox_Err_Friend_Add e_fa = TOX_ERR_FRIEND_ADD::TOX_ERR_FRIEND_ADD_OK;
	tox_friend_add_norequest(tox, public_key, &e_fa);
}

// custom packets
inline void friend_lossy_packet_cb(Tox* /*tox*/, uint32_t friend_number, const uint8_t *data, size_t length, void* user_data) {
//#ifndef NDEBUG
	//std::cout << "friend_lossy_packet_cb " << length << "\n";
//#endif
	static_cast<ToxService*>(user_data)->handle_lossy_packet(friend_number, data, length);
}

inline uint32_t get_milliseconds(void) {
	auto time_raw = std::chrono::steady_clock::now();
	using milli = std::ratio<1, 1000>;
	auto time = std::chrono::duration_cast<std::chrono::duration<int64_t, milli>>(time_raw.time_since_epoch());
	return static_cast<uint32_t>(time.count());
}

inline uint32_t get_microseconds(void) {
	auto time_raw = std::chrono::steady_clock::now();
	using micro = std::ratio<1, 1000000>;
	auto time = std::chrono::duration_cast<std::chrono::duration<int64_t, micro>>(time_raw.time_since_epoch());
	return static_cast<uint32_t>(time.count());
}

