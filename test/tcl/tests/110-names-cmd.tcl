# $Header$

foreach cache {
    global-size-cache
    global-timeout-cache
    thread-size-cache
} {

    test "$cache: names returns all entries" {
	set names [ns_cache names $cache]
	foreach name $names {
	    ns_cache flush $cache $name
	}

	ns_cache eval $cache abc { return 1 }
	ns_cache eval $cache xyz { return 2 }
	ns_cache eval $cache robWasHere { return 3 }

	lsort [ns_cache names $cache]
    } 0 {abc robWasHere xyz}

}
