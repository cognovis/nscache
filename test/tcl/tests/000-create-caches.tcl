# $Header$

test "create global-size-cache" {
    ns_cache create global-size-cache -size 100
} 0 *

test "create global-timeout-cache" {
    ns_cache create global-timeout-cache -timeout 1
} 0 *

test "create thread-size-cache" {
    ns_cache create thread-size-cache -size 100 -thread 1
} 0 *

test "create thread-timeout-cache" {
    ns_cache create thread-timeout-cache -timeout 1 -thread 1
} 1 *

