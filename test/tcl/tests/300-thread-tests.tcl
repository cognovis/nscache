# $Header$

test "thread set not seen by other thread" {
    ns_cache flush thread-size-cache thread-key

    set tid [ns_thread begin {
	ns_cache set thread-size-cache thread-key thread-value
    }]

    ns_thread wait $tid

    ns_cache get thread-size-cache thread-key
} 1 "*no such key*"

