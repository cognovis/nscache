# $Header$

foreach cache {
    global-size-cache
    global-timeout-cache
    thread-size-cache
} {

    test "$cache: flush on nonexistent key succeeds" {
	ns_cache flush $cache no-such-key
    } 0 *

    test "$cache: flush on existing key removes entry" {
	ns_cache eval $cache flush-key {
	    return flush-first-value
	}
	ns_cache flush $cache flush-key
	ns_cache eval $cache flush-key {
	    return flush-second-value
	}
    } 0 flush-second-value

}
