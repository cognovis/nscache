# $Header$

proc test {name body expectedCode expectedResultPattern} {
    ns_log notice "starting test \"$name\""

    set code [catch {uplevel $body} result]

    set outcome [list "test outcome" $name]

    if {
	$code == $expectedCode
	&& [string match $expectedResultPattern $result]
    } {
	ns_log Notice [list "test outcome" success $name]
    } else {
	ns_log Notice [list "test outcome" failure $name $code $result]
    }
}

proc runtests {} {
    foreach testfile [lsort [glob "[ns_info tcllib]/tests/*"]] {
	if {[file isfile $testfile]} {
	    ns_log notice "Sourcing $testfile"
	    source $testfile
	}
    }
}

set mutex [ns_mutex create cache-test-mutex]
ns_log notice "mutex = $mutex"
proc Mutex {} "return $mutex"

set cond [ns_cond create]
ns_log notice "cond = $cond"
proc Cond {} "return $cond"

# We have to wait until server startup to run the tests, because
# some of the tests require spawning more threads, and you can't
# do that until after server startup.

ns_schedule_proc -thread -once 1 runtests

