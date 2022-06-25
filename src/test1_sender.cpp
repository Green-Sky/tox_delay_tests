#include "./test1_common.hpp"

#include <limits>
#include <chrono>

class ToxServiceSender : public ToxService {
	uint16_t _seq_id {0};
	public:

	using ToxService::ToxService;

	// blocks
	void run(void) {
		while (true) {
			using namespace std::literals;
			std::this_thread::sleep_for(1ms);

			tox_iterate(_tox, this);

			if (!_friend_number) {
				continue;
			}

			if (_seq_id == std::numeric_limits<uint16_t>::max()) {
				std::cout << "reached max seq, quitting\n";
				break;
			}

			if (true) { // can send
				// 192-254 for lossy
				const size_t max_pkg_size = 1024 + 1;
				uint8_t buffer[max_pkg_size] {200}; // fist byte is tox pkg id
				size_t pkg_size {1};

				// seq_id
				*reinterpret_cast<uint16_t*>(buffer+pkg_size) = _seq_id++;
				pkg_size += sizeof(uint16_t);

				{ // time stamp
					// TODO: C
					//std::chrono::time_point<std::chrono::steady_clock, std::chrono::duration<int64_t, std::ratio<1, 1000>>> time = std::chrono::steady_clock::now();
					auto time_raw = std::chrono::steady_clock::now();
					auto time = std::chrono::duration_cast<std::chrono::duration<int64_t, std::ratio<1, 1000>>>(time_raw.time_since_epoch());

					*reinterpret_cast<uint32_t*>(buffer+pkg_size) = time.count();
					pkg_size += sizeof(uint32_t);
				}

				Tox_Err_Friend_Custom_Packet e_fcp = TOX_ERR_FRIEND_CUSTOM_PACKET_OK;
				tox_friend_send_lossy_packet(_tox, *_friend_number, buffer, pkg_size, &e_fcp);
				if (e_fcp != TOX_ERR_FRIEND_CUSTOM_PACKET_OK) {
					std::cerr << "error sending lossy pkg " << e_fcp << "\n";
				}
			}
		}
	}

	void handle_lossy_packet(const uint8_t *data, size_t length) override {
		if (length < 2) {
			return;
		}

		if (data[0] != 200) {
			return; // invalid channel
			std::cerr << "invalid channel " << (int) data[0] << "\n";
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

