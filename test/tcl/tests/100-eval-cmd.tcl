# $Header$

foreach cache {
    global-size-cache
    global-timeout-cache
    thread-size-cache
} {

    test "$cache: eval once returns value" {
	ns_cache eval $cache eval-once-key {
	    return eval-once-value
	}
    } 0 eval-once-value

    test "$cache: eval twice returns first cached value" {
	ns_cache eval $cache eval-twice-key {
	    return eval-twice-first-value
	}
	ns_cache eval $cache eval-twice-key {
	    return eval-twice-second-value
	}
    } 0 eval-twice-first-value

    test "$cache: eval twice does not eval second code block" {
	ns_cache eval $cache eval-twice-key {
	    return eval-twice-first-value
	}
	set flag 0
	ns_cache eval $cache eval-twice-key {
	    set flag 1
	    return eval-twice-second-value
	}
	set flag
    } 0 0

    test "$cache: eval propagates error" {
	ns_cache eval $cache error-key {
	    error "error-value"
	}
    } 1 error-value

    test "$cache: error in eval does not create entry" {
	catch {ns_cache eval $cache error-key {
	    error "error-value"
	}}
	ns_cache get $cache error-key
    } 1 "*no such key*"

    test "$cache: eval propagates break" {
	ns_cache eval $cache break-key {
	    break
	}
    } 3 *

    test "$cache: break in eval does not create entry" {
	catch {ns_cache eval $cache break-key {
	    break
	}}
	ns_cache get $cache break-key
    } 1 "*no such key*"

    test "$cache: eval propagates continue" {
	ns_cache eval $cache continue-key {
	    continue
	}
    } 4 *

    test "$cache: error in eval does not create entry" {
	catch {ns_cache eval $cache continue-key {
	    error "continue-value"
	}}
	ns_cache get $cache continue-key
    } 1 "*no such key*"

}
