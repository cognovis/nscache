# $Header$

test "size-based eviction" {
    ns_cache set global-size-cache first-key [string repeat x 90]
    ns_cache set global-size-cache second-key [string repeat x 90]
    ns_cache get global-size-cache first-key
} 1 "*no such key*"

