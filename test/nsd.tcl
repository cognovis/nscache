# $Header$
#
# nsd.tcl for nscache test cases
#
# To run the nscache test cases:
#
# /path/to/bin/nsd8x -ft /path/to/this/nsd.tcl
#

set home [file dirname [file dirname [ns_info argv0]]]
set mydir [file dirname [ns_info config]]

ns_section ns/parameters
    ns_param home $home

ns_section ns/servers
    ns_param server1 server1

ns_section ns/server/server1/tcl
    ns_param library "$mydir/tcl"

ns_section ns/server/server1/modules
    ns_param nscache "[file dirname [ns_info config]]/../nscache.so"

