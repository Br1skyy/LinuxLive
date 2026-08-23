#include "lili-relay/relay_server.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <cstdlib>

namespace {
    lili::RelayServer* server = nullptr;
}

void signal_handler(int) {
    if (server) {
        server->print_stats();
        server->stop();
    }
}

void print_usage(const char* prog) {
    std::cout << "LinuxLive Relay Server\n\n"
              << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  -p, --port PORT           Listen port (default: 8080)\n"
              << "  -r, --rate-limit N        Max events per pubkey per window (default: 30)\n"
              << "  -w, --window SEC          Rate limit window in seconds (default: 60)\n"
              << "  -m, --max-events N        Max total events on relay (default: 100000)\n"
              << "  -k, --max-per-pubkey N    Max events per pubkey (default: 100)\n"
              << "  -s, --max-per-kind N      Max events per kind (default: 10000)\n"
              << "  -z, --max-size BYTES      Max event size in bytes (default: 65536)\n"
              << "  -t, --ttl SEC             Default event TTL in seconds (default: 604800 = 7 days)\n"
              << "  --kind-ttl KIND:SEC       Per-kind TTL (e.g. 30079:0 for achievements = forever)\n"
              << "  --ban PUBKEY              Ban a pubkey and delete all their events\n"
              << "  -h, --help                Show this help\n\n"
              << "Event Kind TTLs (default):\n"
              << "  Kind 30079 (achievement): forever\n"
              << "  Kind 30080 (achievement proof): forever\n"
              << "  Kind 30019 (node): 90 days\n"
              << "  Kind 0 (profile): 30 days\n"
              << "  Kind 42 (chat): 7 days\n";
}

int main(int argc, char* argv[]) {
    lili::RelayConfig config;
    uint16_t port = 8080;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if ((arg == "-r" || arg == "--rate-limit") && i + 1 < argc) {
            config.rate_limit_events = std::stoi(argv[++i]);
        } else if ((arg == "-w" || arg == "--window") && i + 1 < argc) {
            config.rate_limit_window_seconds = std::stoi(argv[++i]);
        } else if ((arg == "-m" || arg == "--max-events") && i + 1 < argc) {
            config.max_total_events = std::stoul(argv[++i]);
        } else if ((arg == "-k" || arg == "--max-per-pubkey") && i + 1 < argc) {
            config.max_events_per_pubkey = std::stoul(argv[++i]);
        } else if ((arg == "-s" || arg == "--max-per-kind") && i + 1 < argc) {
            config.max_events_per_kind = std::stoul(argv[++i]);
        } else if ((arg == "-z" || arg == "--max-size") && i + 1 < argc) {
            config.max_event_size_bytes = std::stoul(argv[++i]);
        } else if ((arg == "-t" || arg == "--ttl") && i + 1 < argc) {
            config.event_ttl_seconds = std::stoi(argv[++i]);
        } else if (arg == "--kind-ttl" && i + 1 < argc) {
            std::string spec = argv[++i];
            size_t colon = spec.find(':');
            if (colon != std::string::npos) {
                uint16_t kind = static_cast<uint16_t>(std::stoi(spec.substr(0, colon)));
                int ttl = std::stoi(spec.substr(colon + 1));
                config.kind_ttl[kind] = ttl;
            }
        } else if (arg == "--ban" && i + 1 < argc) {
            config.banned_pubkeys.insert(argv[++i]);
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    std::cout << "Starting LinuxLive relay on port " << port << std::endl;

    lili::RelayServer server(port, config);

    for (const auto& pubkey : config.banned_pubkeys) {
        server.delete_events_by_pubkey(pubkey);
    }

    ::server = &server;
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    server.start();

    // start() only spawns worker threads; give the accept thread a moment to
    // bind, then bail out with a clear error if the port is unavailable
    for (int i = 0; i < 20 && !server.is_listening(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!server.is_listening()) {
        std::cerr << "Relay could not listen on port " << port
                  << " - is another instance already using it?" << std::endl;
        server.stop();
        return 1;
    }

    // block here until signalled
    while (server.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    server.stop();
    return 0;
}
