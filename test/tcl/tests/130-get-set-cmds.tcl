# $Header$

foreach cache {
    global-size-cache
    global-timeout-cache
    thread-size-cache
} {

    test "$cache: get on non-existent entry returns error" {
	ns_cache get $cache non-existent-key
    } 1 "*no such key*"

    test "$cache: get w/ var on non-existent entry returns 0" {
	ns_cache get $cache non-existent-key var
    } 0 0

    test "$cache: set followed by get returns value" {
	ns_cache set $cache set-key set-value
	ns_cache get $cache set-key
    } 0 set-value

    test "$cache: set followed by get w/ var returns 1 and sets var" {
	ns_cache set $cache set-key set-value
	set code [ns_cache get $cache set-key var]
	list $code $var
    } 0 {1 set-value}

    test "$cache: second set overrides first set" {
	ns_cache set $cache set-key set-value-1
	ns_cache set $cache set-key set-value-2
	ns_cache get $cache set-key
    } 0 set-value-2

    test "$cache: flush removes set entry" {
	ns_cache set $cache flush-key flush-value
	ns_cache flush $cache flush-key
	ns_cache get $cache flush-key
    } 1 "*no such key*"

}
