#include "hotrod/RemoteCache.h"
#include "hotrod/Codec.h"
#include <stdexcept>

namespace hotrod {

RemoteCache::RemoteCache(const std::string& host, uint16_t port)
    : host_(host), port_(port), cacheName_(""), messageIdCounter_(0) {
    connection_ = std::make_unique<Connection>(host, port);
}

RemoteCache::RemoteCache(const std::string& host, uint16_t port, const std::string& cacheName)
    : host_(host), port_(port), cacheName_(cacheName), messageIdCounter_(0) {
    connection_ = std::make_unique<Connection>(host, port);
}

RemoteCache::~RemoteCache() {
    if (isConnected()) {
        disconnect();
    }
}

void RemoteCache::connect() {
    connection_->connect();
}

void RemoteCache::disconnect() {
    connection_->close();
}

bool RemoteCache::isConnected() const {
    return connection_ && connection_->isConnected();
}

uint64_t RemoteCache::nextMessageId() {
    return ++messageIdCounter_;
}

ByteArray RemoteCache::sendRequest(const ByteArray& request) {
    if (!isConnected()) {
        throw std::runtime_error("Not connected to server");
    }

    // Send request
    connection_->send(request);

    // Read response header (first 5 bytes minimum: magic + messageId + opcode + status + topologyChange)
    // Note: messageId is vLong, so we need to read byte by byte until we have complete header
    ByteArray responseBuffer = connection_->receive(5);

    // Verify response magic
    if (responseBuffer[0] != 0xA1) {
        throw std::runtime_error("Invalid response magic byte");
    }

    // For now, return just the header
    // TODO: Parse full response with topology updates
    return responseBuffer;
}

bool RemoteCache::ping() {
    // Build PING request header
    RequestHeader header;
    header.messageId = nextMessageId();
    header.version = 0x28;  // Protocol 4.0
    header.opcode = 0x17;   // PING_REQUEST
    header.cacheName = cacheName_;
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::BASIC;  // TODO: Use HASH_AWARE when topology is integrated
    header.topologyId = 0;  // TODO: Track topology ID
    header.keyMediaType = 0;
    header.valueMediaType = 0;
    // otherParams empty (count = 0)

    // Encode request
    ByteArray request;
    HeaderCodec::writeRequestHeader(request, header);

    // Send and receive response
    ByteArray response = sendRequest(request);

    // Parse response header
    size_t offset = 0;
    ResponseHeader respHeader = HeaderCodec::readResponseHeader(response, offset);

    // Verify response
    if (respHeader.opcode != 0x18) {  // PING_RESPONSE
        throw std::runtime_error("Unexpected response opcode: " + std::to_string(respHeader.opcode));
    }

    if (respHeader.messageId != header.messageId) {
        throw std::runtime_error("Message ID mismatch");
    }

    if (respHeader.status != 0x00) {  // NO_ERROR
        throw std::runtime_error("PING failed with status: " + std::to_string(respHeader.status));
    }

    return true;
}

} // namespace hotrod
