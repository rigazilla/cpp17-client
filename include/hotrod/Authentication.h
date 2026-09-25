#pragma once

#include <string>

namespace hotrod {

/**
 * SASL authentication configuration for a client connection.
 *
 * Pure data, applied before connect(). When `enabled` is true the client runs
 * the SASL handshake (AUTH_MECH_LIST + AUTH) on every connection it opens — the
 * seed connection and every topology-discovered cluster member — before the
 * connection is used for operations.
 *
 * Only the SCRAM family is implemented (SCRAM-SHA-1 / SCRAM-SHA-256 /
 * SCRAM-SHA-512); any other configured mechanism throws a HotRodClientException
 * at connect time. For the SCRAM mechanisms only `username` and `password`
 * actually enter the exchange; `realm` and `serverName` are carried for parity
 * with the Java client (and future mechanisms) but are unused by SCRAM.
 *
 * Reference:
 * - Java: org.infinispan.client.hotrod.configuration.AuthenticationConfiguration
 *   (defaults: mechanism "SCRAM-SHA-256", realm "default", serverName "infinispan")
 */
struct Authentication {
    bool        enabled     = false;
    std::string username;
    std::string password;
    std::string realm       = "default";
    std::string serverName  = "infinispan";
    std::string mechanism   = "SCRAM-SHA-256";
};

} // namespace hotrod
