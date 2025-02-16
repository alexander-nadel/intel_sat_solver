#! /bin/csh -f

if ($#argv < 1) then
	echo "Fuzz and verify IntelSAT (that is, its executable, such as, intel_sat_solver_static) forever, given the optional parameters on the input"
	echo "Parameters: intel_sat_executable [Any additional parameters (optional)]"
    exit
endif


set topor = $1
if (! -e $topor) then
	echo "intel_sat_executable doesn't exist at $topor. Exiting"
	echo "ERROR"
	exit 130
endif

set script_dir = `dirname $0`
set delta_debug_topor_till_fixed_point = ${script_dir}/delta_debug_intel_sat_till_fixed_point.csh
set cnfuzz_incr = "$script_dir/../third_party/cnfuzzdd2013/cnfuzz_incr"

set isok = 1
set i = 1
set currm = 0

set tmpdir = "/tmp/$USER/fuzz_and_verify_`hostname`_$$"
mkdir -p $tmpdir

while ($isok)
	echo "i : $i"
	set currf = "$tmpdir/incrfuzz_$$.cnf"
	echo "$cnfuzz_incr > $currf"
	$cnfuzz_incr > $currf
	set ddout = "$tmpdir/ddout_$$"
	echo "$delta_debug_topor_till_fixed_point $topor $currf /topor_tool/solver_mode $currm $argv[2-] |& tee $ddout"
	$delta_debug_topor_till_fixed_point $topor $currf /topor_tool/solver_mode $currm $argv[2-] |& tee $ddout
	set isok = `tail -n 1 $ddout | grep -c Ok`
	@ i = $i + 1
	if ($currm == 0) then
		set currm = 1
	else
		set currm = 0
	endif
end
