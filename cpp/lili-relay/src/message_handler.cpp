#include "lili-relay/message_handler.hpp"

namespace lili {

nlohmann::json MessageHandler::handle_message(const nlohmann::json& message) {
    std::string type = message["type"].get<std::string>();

    if (type == "EVENT") {
        return handle_event(message["event"]);
    } else if (type == "REQ") {
        return handle_req(message);
    } else if (type == "CLOSE") {
        return handle_close(message);
    }

    return {{"type", "ERROR"}, {"message", "Unknown message type"}};
}

nlohmann::json MessageHandler::handle_event(const nlohmann::json& event) {
    return {{"type", "OK"}, {"event_id", event["id"]}};
}

nlohmann::json MessageHandler::handle_req(const nlohmann::json&) {
    return {{"type", "EOSE"}};
}

nlohmann::json MessageHandler::handle_close(const nlohmann::json&) {
    return {{"type", "OK"}};
}

}
