# $Header$

test "timeout-based eviction" {
    ns_cache set global-timeout-cache key [string repeat x 90]
    ns_sleep 3
    ns_cache get global-timeout-cache key
} 1 "*no such key*"

