#pragma once

#include "HotRodClientException.h"  // ServerAddress
#include <vector>

namespace hotrod {

/**
 * User→library input that drives an exclusion-aware retry of an operation
 * (Step 11b). Defaulted on every operation, so existing call sites are
 * untouched; a retrying caller builds one from a caught HotRodClientException.
 *
 * Note this is deliberately *not* where proxy-to-non-owner lives: that is
 * client-level routing policy (see RemoteCache, D5), not per-call input.
 *
 * Typical loop:
 *   RetryContext ctx;
 *   for (;;) {
 *       try { return cache.get(key, ctx).get(); }
 *       catch (const HotRodClientException& e) {
 *           if (!isTransient(e)) throw;
 *           // caller decides, from e.phase / outcomeUncertain(e), whether THIS
 *           // op is safe to retry, then avoids the nodes already tried:
 *           ctx.excludeNodes = e.triedNodes;
 *       }
 *   }
 *
 * See docs/ERROR_HANDLING_DESIGN.md §3.2.
 */
struct RetryContext {
    std::vector<ServerAddress> excludeNodes;   // don't dispatch here again
    // (room to grow: deadline, attempt count, ...)
};

} // namespace hotrod
