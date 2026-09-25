#pragma once

#include "RemoteCache.h"
#include "RetryContext.h"
#include "Types.h"
#include <future>
#include <optional>
#include <cstdint>
#include <utility>

namespace hotrod {

/**
 * A lightweight, non-owning view over a RemoteCache that carries a RetryContext
 * (currently an exclusion set). Returned by RemoteCache::excluding(); it exposes
 * the same key-routed operations, each dispatched avoiding the excluded nodes.
 *
 * Base RemoteCache operations stay retry-free — a caller opts into exclusion
 * only on the retry path, seeding it from a caught exception:
 *
 *   try { return cache.get(key).get(); }
 *   catch (const HotRodClientException& e) {
 *       if (!isTransient(e)) throw;                  // + your idempotency call
 *       return cache.excluding(e).get(key).get();    // avoids e.triedNodes
 *   }
 *
 * Non-owning: a RetryView must not outlive the RemoteCache it views (the same
 * lifetime rule as calling the cache directly). It holds no connection or other
 * client internals — only a pointer to the cache and a copied context — so it is
 * cheap to create per attempt.
 *
 * See docs/ERROR_HANDLING_DESIGN.md §3.2 / D3.
 */
class RetryView {
public:
    RetryView(RemoteCache& cache, RetryContext ctx)
        : cache_(&cache), ctx_(std::move(ctx)) {}

    std::future<void> ping() {
        return cache_->pingImpl(ctx_);
    }

    std::future<std::optional<ByteArray>> get(const ByteArray& key) {
        return cache_->getImpl(key, ctx_);
    }

    std::future<std::optional<EntryWithMetadata>> getWithMetadata(const ByteArray& key) {
        return cache_->getWithMetadataImpl(key, ctx_);
    }

    std::future<std::optional<EntryWithMetadata>> put(const ByteArray& key, const ByteArray& value,
                                                      uint64_t lifespan = 0, uint64_t maxIdle = 0,
                                                      bool previousValue = false) {
        return cache_->putImpl(key, value, lifespan, maxIdle, previousValue, ctx_);
    }

    std::future<std::optional<EntryWithMetadata>> putIfAbsent(const ByteArray& key, const ByteArray& value,
                                                              uint64_t lifespan = 0, uint64_t maxIdle = 0,
                                                              bool previousValue = false) {
        return cache_->putIfAbsentImpl(key, value, lifespan, maxIdle, previousValue, ctx_);
    }

    std::future<std::optional<EntryWithMetadata>> replace(const ByteArray& key, const ByteArray& value,
                                                          uint64_t lifespan = 0, uint64_t maxIdle = 0,
                                                          bool previousValue = false) {
        return cache_->replaceImpl(key, value, lifespan, maxIdle, previousValue, ctx_);
    }

    std::future<bool> containsKey(const ByteArray& key) {
        return cache_->containsKeyImpl(key, ctx_);
    }

    std::future<std::optional<EntryWithMetadata>> remove(const ByteArray& key, bool previousValue = false) {
        return cache_->removeImpl(key, previousValue, ctx_);
    }

    std::future<bool> removeWithVersion(const ByteArray& key, int64_t version) {
        return cache_->removeWithVersionImpl(key, version, ctx_);
    }

    std::future<bool> replaceWithVersion(const ByteArray& key, const ByteArray& value,
                                         int64_t version, uint64_t lifespan = 0, uint64_t maxIdle = 0) {
        return cache_->replaceWithVersionImpl(key, value, version, lifespan, maxIdle, ctx_);
    }

private:
    RemoteCache* cache_;   // non-owning; must outlive this view
    RetryContext ctx_;     // bound exclusion set
};

} // namespace hotrod
