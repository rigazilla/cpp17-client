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

    // Read response header byte by byte
    // Format: magic(1) + messageId(vLong) + opcode(1) + status(1) + topologyChange(1)

    ByteArray responseBuffer;

    // Read magic byte
    ByteArray magic = connection_->receive(1);

    // DEBUG: Log magic byte value
    fprintf(stderr, "[DEBUG] Response magic byte: 0x%02X (expected 0xA1)\n", magic[0]);

    if (magic[0] != 0xA1) {
        fprintf(stderr, "[ERROR] Invalid magic byte received: 0x%02X\n", magic[0]);
        throw std::runtime_error("Invalid response magic byte");
    }
    responseBuffer.push_back(magic[0]);

    // Read vLong message ID (variable length)
    int msgIdBytes = 0;
    while (true) {
        ByteArray byte = connection_->receive(1);
        responseBuffer.push_back(byte[0]);
        msgIdBytes++;
        // vLong continues while high bit is set
        if ((byte[0] & 0x80) == 0) {
            break;
        }
    }

    // Read opcode, status, topology change marker (3 bytes)
    ByteArray tail = connection_->receive(3);
    responseBuffer.insert(responseBuffer.end(), tail.begin(), tail.end());

    // DEBUG: Log response info
    fprintf(stderr, "[DEBUG] Total response bytes read: %zu (magic:1 + msgId:%d + tail:3)\n",
            responseBuffer.size(), msgIdBytes);
    fprintf(stderr, "[DEBUG] Response bytes (hex): ");
    for (size_t i = 0; i < responseBuffer.size(); i++) {
        fprintf(stderr, "%02X ", responseBuffer[i]);
    }
    fprintf(stderr, "\n");

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

    // PING response body (Protocol 3.0+):
    // - key_type (media_type)
    // - value_type (media_type)
    // - server_version (u1)
    // - op_count (vint)
    // - supported_opcodes (u2 array)
    //
    // We need to consume the ENTIRE response body to clear the socket buffer!

    // Read key_type media_type (at minimum: type_indicator=1 byte)
    ByteArray keyType = connection_->receive(1);
    uint8_t keyTypeIndicator = keyType[0];

    // If not NONE (0), read param_count and params
    if (keyTypeIndicator != 0) {
        // Read predefined_id or custom_string based on indicator
        if (keyTypeIndicator == 1) {  // predefined
            // Read vint predefined_id
            while (true) {
                ByteArray b = connection_->receive(1);
                if ((b[0] & 0x80) == 0) break;
            }
        } else if (keyTypeIndicator == 2) {  // custom
            // Read lp_string (vint length + bytes)
            VInt len = 0;
            while (true) {
                ByteArray b = connection_->receive(1);
                if ((b[0] & 0x80) == 0) {
                    len = b[0]; // simplified
                    break;
                }
            }
            if (len > 0) connection_->receive(len);
        }
        // Read param_count (vint)
        VInt paramCount = 0;
        while (true) {
            ByteArray b = connection_->receive(1);
            if ((b[0] & 0x80) == 0) {
                paramCount = b[0]; // simplified
                break;
            }
        }
        (void)paramCount; // TODO: Read params if paramCount > 0
    }

    // Read value_type media_type (same structure as key_type)
    ByteArray valType = connection_->receive(1);
    uint8_t valTypeIndicator = valType[0];
    if (valTypeIndicator != 0) {
        if (valTypeIndicator == 1) {
            while (true) {
                ByteArray b = connection_->receive(1);
                if ((b[0] & 0x80) == 0) break;
            }
        } else if (valTypeIndicator == 2) {
            VInt len = 0;
            while (true) {
                ByteArray b = connection_->receive(1);
                if ((b[0] & 0x80) == 0) {
                    len = b[0];
                    break;
                }
            }
            if (len > 0) connection_->receive(len);
        }
        VInt paramCount = 0;
        while (true) {
            ByteArray b = connection_->receive(1);
            if ((b[0] & 0x80) == 0) {
                paramCount = b[0];
                break;
            }
        }
        (void)paramCount; // TODO: Read params if paramCount > 0
    }

    // Read server_version (1 byte)
    connection_->receive(1);

    // Read op_count (vint)
    VInt opCount = 0;
    while (true) {
        ByteArray b = connection_->receive(1);
        opCount = (opCount << 7) | (b[0] & 0x7F);
        if ((b[0] & 0x80) == 0) break;
    }

    // Read supported_opcodes array (opCount * 2 bytes each = u2)
    if (opCount > 0) {
        connection_->receive(opCount * 2);
    }

    fprintf(stderr, "[DEBUG] PING response body consumed successfully\n");

    return true;
}

} // namespace hotrod
