#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace lili {

class MessageHandler {
public:
    static nlohmann::json handle_message(const nlohmann::json& message);
    static nlohmann::json handle_event(const nlohmann::json& event);
    static nlohmann::json handle_req(const nlohmann::json& req);
    static nlohmann::json handle_close(const nlohmann::json& close);
};

}
