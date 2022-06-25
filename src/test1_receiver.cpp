#include "./test1_common.hpp"

class ToxServiceReceiver : public ToxService {
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
		}
	}

	void handle_lossy_packet(uint32_t friend_number, const uint8_t *data, size_t length) override {
		const size_t min_packet_len =
			1 // tox_pkg_id
			+ sizeof(uint16_t) // seq id
			+ sizeof(uint32_t) // sender time
			+ 0; // payload minimum, 0 for testing

		if (length < min_packet_len) {
			// TODO: warn
			return;
		}

		if (data[0] != 200) {
			return; // invalid channel
			std::cerr << "invalid channel " << (int) data[0] << "\n";
		}

		// packet is:
		// - 16bit sequence id
		// - 32bit sender timestamp (in ms?)
		// - payload

		// we assume little endian
		uint16_t pk_seq_id = *reinterpret_cast<const uint16_t*>(data+1);
		uint32_t pk_sender_time = *reinterpret_cast<const uint32_t*>(data+3);

		int32_t time_delta = static_cast<int64_t>(get_microseconds()) - pk_sender_time;

		std::cout << "got packet " << pk_seq_id << " t:" << pk_sender_time << " d:" << time_delta << "\n";

		{ // tmp send ack directly
			uint8_t buffer[1 + sizeof(uint16_t) + sizeof(int32_t)] {200};
			size_t pkg_size {1};

			*reinterpret_cast<uint16_t*>(buffer+pkg_size) = pk_seq_id;
			pkg_size += sizeof(uint16_t);

			*reinterpret_cast<int32_t*>(buffer+pkg_size) = time_delta;
			pkg_size += sizeof(int32_t);

			tox_friend_send_lossy_packet(_tox, friend_number, buffer, pkg_size, nullptr);
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

	ToxServiceReceiver ts{tcp_only};

	if (!friend_sv.empty()) {
		if (!ts.add_friend(std::string(friend_sv))) {
			std::cerr << "error adding friend!\n";
			return -1;
		}
	}

	ts.run();

	return 0;
}

