# $Header$

test "global cache entries are visible across threads" {
    ns_cache flush global-size-cache global-key

    set tid [ns_thread begin {
	ns_cache eval global-size-cache global-key {
	    return other-thread-value
	}
    }]

    ns_thread wait $tid

    ns_cache eval global-size-cache global-key {
	return my-thread-value
    }
} 0 other-thread-value

test "global eval waits if other thread is in eval" {
    ns_cache flush global-size-cache global-key

    ns_mutex lock [Mutex]

    ns_thread begindetached {
	ns_mutex lock [Mutex]
	ns_cache eval global-size-cache global-key {
	    ns_cond broadcast [Cond]
	    ns_mutex unlock [Mutex]
	    # Let other thread call eval
	    ns_sleep 1
	    return other-thread-value
	}
    }

    # Wait for other thread to be in eval
    ns_cond wait [Cond] [Mutex]
    ns_mutex unlock [Mutex]

    ns_cache eval global-size-cache global-key {
	return my-thread-value
    }
} 0 other-thread-value

test "global flush works while other thread is in eval" {
    ns_cache flush global-size-cache global-key

    ns_mutex lock [Mutex]

    set tid [ns_thread begin {
	ns_mutex lock [Mutex]
	ns_cache eval global-size-cache global-key {
	    ns_cond broadcast [Cond]

	    # Wait for other thread to call flush
	    ns_cond wait [Cond] [Mutex]
	    ns_mutex unlock [Mutex]

	    return other-thread-value
	}
    }]

    ns_cond wait [Cond] [Mutex]

    ns_cache flush global-size-cache global-key

    ns_cond broadcast [Cond]
    ns_mutex unlock [Mutex]

    set result [ns_cache get global-size-cache global-key var]

    ns_thread wait $tid

    return $result
} 2 0

test "global get waits while other thread is in eval" {
    ns_cache flush global-size-cache global-key

    ns_mutex lock [Mutex]

    set tid [ns_thread begin {
	ns_mutex lock [Mutex]
	ns_cache eval global-size-cache global-key {
	    ns_cond broadcast [Cond] [Mutex]
	    ns_mutex unlock [Mutex]
	    ns_sleep 1
	    return other-thread-value
	}
    }]

    ns_cond wait [Cond] [Mutex]
    ns_mutex unlock [Mutex]

    set value [ns_cache get global-size-cache global-key]

    ns_thread wait $tid

    return $value
} 2 other-thread-value

test "global set overrides other thread in eval" {
    ns_cache flush global-size-cache global-key

    ns_mutex lock [Mutex]

    set tid [ns_thread begin {
	ns_mutex lock [Mutex]
	ns_cache eval global-size-cache global-key {
	    ns_cond broadcast [Cond]
	    ns_mutex unlock [Mutex]
	    ns_sleep 1
	    return other-thread-value
	}
    }]

    ns_cond wait [Cond] [Mutex]
    ns_mutex unlock [Mutex]

    ns_cache set global-size-cache global-key my-thread-value

    ns_thread wait $tid

    ns_cache get global-size-cache global-key
} 0 my-thread-value

test "global eval w/ error lets other thread run eval" {
    ns_cache flush global-size-cache global-key

    ns_mutex lock [Mutex]

    set tid [ns_thread begin {
	ns_mutex lock [Mutex]
	catch {
	    ns_cache eval global-size-cache global-key {
		ns_cond broadcast [Cond]
		ns_mutex unlock [Mutex]
		ns_sleep 1
		error other-thread-error
	    }
	}
    }]

    ns_cond wait [Cond] [Mutex]
    ns_mutex unlock [Mutex]

    ns_cache eval global-size-cache global-key {
	return my-thread-value
    }

    ns_thread wait $tid

    ns_cache get global-size-cache global-key
} 0 my-thread-value

