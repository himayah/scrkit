#pragma once
// Message envelopes of docs/SCRAPI_SPEC.md §4: building and parsing the JSON Lines
// requests, responses and events. No transport, no application logic.

#include <cstdint>
#include <string>

#include "Json.h"

namespace scrapi {

constexpr const char* kApiVersion = "1.0";
constexpr size_t kMaxMessageBytes = 1024 * 1024;

// A request as seen by the saver.
struct Request {
    bool hasId = false;
    int64_t id = 0;
    std::string op;
    JsonValue body; // the whole request object (so ops read their own fields from it)
};
bool ParseRequest(const std::string& line, Request& out, std::string* error = nullptr);

// Something the viewer receives: a response to one of its requests, or an event.
struct Incoming {
    enum class Kind { Invalid, Response, Event };
    Kind kind = Kind::Invalid;
    int64_t id = 0;         // Response
    bool ok = false;        // Response
    std::string errorCode;  // Response, when !ok
    std::string errorMessage;
    std::string ev;         // Event
    JsonValue body;         // the whole message object
};
Incoming ParseIncoming(const std::string& line);

// Builders return one line WITHOUT the trailing newline (the transport adds it).
// `body` members (must be an object, or null for none) are merged into the top level.
std::string MakeRequestLine(int64_t id, const std::string& op, const JsonValue& body = JsonValue::Null());
std::string MakeOkLine(int64_t id, const JsonValue& body = JsonValue::Null());
std::string MakeErrorLine(int64_t id, const std::string& code, const std::string& message);
std::string MakeEventLine(const std::string& ev, const JsonValue& body = JsonValue::Null());

// Major version of "M.m", or -1 if malformed.
int ApiMajorVersion(const std::string& version);

} // namespace scrapi
