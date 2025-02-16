#! /bin/csh -f

set sleeps = 60

if ($#argv < 1) then
    echo "Fuzz and verify IntelSAT in parallel, given (1) the IntelSAT executable, (2) the number of threads, and (3-) optional input parameters."
    echo "Usage: <intel_sat_executable> <threads_num> [additional parameters]"
    echo "It is recommended to run the script with at least 9 threads to test all (current) 9 modes of IntelSAT."
    echo "Additionally, running it in parallel for both the release and debug versions of IntelSAT is recommended for robustness."
    echo "The script periodically (every minute) prints the latest outputs of every executed thread. If everything is Ok, you should see no errors, and the numbers indicating the current invocation number of every thread are expected to increase."
    echo "To halt the script by killing all the relevant process, one can use the following command (exercise with great caution!): killall fuzz_and_verify_parallel.csh fuzz_and_verify.csh drat-trim run_and_verify_intel_sat.csh delta_debug_intel_sat.csh intel_sat_solver_static"
    exit 140
endif

set topor = $1
echo "IntelSAT executable: $topor"
if (! -e $topor) then
	echo "intel_sat_executable doesn't exist at $topor. Exiting"
	echo "ERROR"
	exit 130
endif


set threads = $2
echo "Threads: $2"
echo "Parameters: $argv[3-]"
set my_dir = `dirname $0`
set fuzz_and_verify = ${my_dir}/fuzz_and_verify.csh

set tmpdir = "/tmp/$USER/fuzz_and_verify_parallel_`hostname`_$$"
mkdir -p $tmpdir

set i = 1
# Topor mode
set m = 0
while ($i <= $threads)
	set outt = "$tmpdir/fav.$$.$i"
	echo "$fuzz_and_verify $topor /mode/value $m $argv[3-] > $outt &"
	$fuzz_and_verify $topor /mode/value $m $argv[3-] > $outt &
	@ i = $i + 1
	@ m = $m + 1
	if ($m == 9) then
		set m = 0
	endif
end

while (1)
	tail $tmpdir/fav.$$.* | egrep "fav.*|i :" ; grep ERROR $tmpdir/fav.$$.*
	sleep $sleeps
end
