#include "./test1_common.hpp"
#include "toxcore/tox.h"

#include <fstream>
#include <limits>
#include <chrono>
#include <unordered_map>

class ToxServiceSender : public ToxService {
	std::ofstream _out_csv;

	uint16_t _seq_id {0};

	//const uint16_t _window_max {100};
	uint16_t _window {200};
	//size_t _max_pkgs_per_iteration {1};
	//size_t _window_increase_counter {0};

	//const uint16_t _payload_size_min {128};
	const uint16_t _payload_size_max {1024};

	uint16_t _payload_size {1};
	size_t _payload_increase_counter {0};

	struct PKGData {
		uint32_t time_stamp {0};
		uint16_t payload_size {0};
		uint16_t window_size {0}; // window size at point of send
	};
	std::unordered_map<uint16_t, PKGData> _pkg_info;

	public:

	//using ToxService::ToxService;
	ToxServiceSender(bool tcp_only) : ToxService(tcp_only) {
		_out_csv.open(
			"test1_delays_" +
			std::to_string(std::time(nullptr)) +
			(tcp_only ? "_tcp" : "_mixed") +
			".csv"
		);

		// header
		_out_csv << "time_stamp, seq_id, time_delta, window, payload_size, connection_type\n";
		_out_csv.flush();
	}

	~ToxServiceSender(void) {
		_out_csv.close();

		tox_kill(_tox);
		_tox = nullptr;
	}

	// blocks
	void run(void) {
		while (true) {
			using namespace std::literals;
			std::this_thread::sleep_for(1ms);

			tox_iterate(_tox, this);

			if (!_friend_number) {
				continue;
			}

			{ // do timeout
				const uint32_t timeout {1000000}; // 1sec
				auto time_stamp_now = get_microseconds();

				for (auto it = _pkg_info.begin(); it != _pkg_info.end();) {
					if (time_stamp_now - it->second.time_stamp >= timeout) {
						std::cout << "pkg " << it->first << " timed out!\n";
						it = _pkg_info.erase(it);
					} else {
						it++;
					}
				}
			}

			if (_window > _pkg_info.size()) { // can send
				// 192-254 for lossy
				const size_t max_pkg_size = 1024 + 1;
				uint8_t buffer[max_pkg_size] {200}; // fist byte is tox pkg id
				size_t pkg_size {1};

				//_window_increase_counter++;
				_payload_increase_counter++;

				// seq_id
				*reinterpret_cast<uint16_t*>(buffer+pkg_size) = _seq_id++;
				pkg_size += sizeof(uint16_t);

				// time stamp
				auto time_stamp = get_microseconds();
				*reinterpret_cast<uint32_t*>(buffer+pkg_size) = time_stamp;
				pkg_size += sizeof(uint32_t);

				// TODO: actually fill with random data, rn its uninit mem
				pkg_size += _payload_size;

				Tox_Err_Friend_Custom_Packet e_fcp = TOX_ERR_FRIEND_CUSTOM_PACKET_OK;
				tox_friend_send_lossy_packet(_tox, *_friend_number, buffer, pkg_size, &e_fcp);
				if (e_fcp != TOX_ERR_FRIEND_CUSTOM_PACKET_OK) {
					std::cerr << "error sending lossy pkg " << e_fcp << "\n";
				} else {
					_pkg_info[_seq_id - 1] = {
						time_stamp,
						_payload_size,
						_window
					};
				}
			}
if (_seq_id == std::numeric_limits<uint16_t>::max()) {
				std::cout << "reached max seq, quitting\n";
				break;
			}

#if 0
			// every 100pkgs increase window by 1
			if (_window_increase_counter >= 100) {
				_window_increase_counter = 0;
				_window++;
			}
#endif
			// every 100pkgs increase payload by 1
			if (_payload_increase_counter >= 25 && _payload_size < _payload_size_max) {
				_payload_increase_counter = 0;
				_payload_size++;
			}
		}
	}

	void handle_lossy_packet(uint32_t friend_number, const uint8_t *data, size_t length) override {
		if (length < 2) {
			return;
		}

		if (data[0] != 200) {
			return; // invalid channel
			std::cerr << "invalid channel " << (int) data[0] << "\n";
		}

		auto tc = tox_friend_get_connection_status(_tox, friend_number, nullptr);

		// ack pkg
		// list of:
		// - uint16_t seq_id
		// if microseconds:
		// - int32_t time delta
		// else if milliseconds:
		// - int16_t time_delta
		// time_delta IS SIGNED! different computers clocks might not be sync
		size_t curr_i = 1;
		while (curr_i < length) {
			uint16_t seq_id = *reinterpret_cast<const uint16_t*>(data+curr_i);
			curr_i += sizeof(uint16_t);

#if 1
			int32_t time_delta = *reinterpret_cast<const int32_t*>(data+curr_i);
			curr_i += sizeof(int32_t);
#else
			int16_t time_delta = *reinterpret_cast<const int16_t*>(data+curr_i);
			curr_i += sizeof(int16_t);
#endif

			if (!_pkg_info.count(seq_id)) {
				std::cout << "unk pkg of id " << seq_id << ", ignoring..\n";
			} else {
				const auto& pkg_info = _pkg_info[seq_id];
				std::cout << "mesurement:"
					" ts: " << pkg_info.time_stamp <<
					" id: " << seq_id <<
					" d: " << time_delta <<
					" w: " << pkg_info.window_size <<
					" ps: " << pkg_info.payload_size <<
					" s: " << tc <<
					"\n";
				_out_csv
					<< pkg_info.time_stamp << ", "
					<< seq_id << ", "
					<< time_delta << ", "
					<< pkg_info.window_size << ", "
					<< pkg_info.payload_size << ", "
					<< tc << "\n";
				_out_csv.flush();
				_pkg_info.erase(seq_id);
			}
		}
	}
};

// command line :
// <tcp|mixed>
// [friend_tox_addr]
int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "not enough params, usage:\n$ " << argv[0] << " <tcp|mixed> [friend_tox_addr]\n";
		return -1;
	}

	// TODO: just use some arg lib

	// contype
	bool tcp_only = false;
	std::string_view type_sv{argv[1]};
	if (type_sv == "tcp") {
		tcp_only = true;
	} else if (type_sv == "mixed") {
		tcp_only = false;
	} else {
		std::cerr << "error: invalid type " << type_sv << ", must be either tcp or mixed.\n";
		return -1;
	}
	std::cout << "set type to " << type_sv << "\n";

	std::string_view friend_sv{};
	if (argc == 3) { // friend?
		friend_sv = argv[2];
	}

	ToxServiceSender ts{tcp_only};

	if (!friend_sv.empty()) {
		if (!ts.add_friend(std::string(friend_sv))) {
			std::cerr << "error adding friend!\n";
			return -1;
		}
	}

	ts.run();

	return 0;
}

