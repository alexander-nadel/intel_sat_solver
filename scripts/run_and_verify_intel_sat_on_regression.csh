#! /bin/csh -f

if ($#argv < 1) then
	echo "Verify the given IntelSAT executable on regression instances given optional parameters on the input"
	echo "Parameters: intel_sat_executable [Any additional parameters (optional)]"
	exit 140
endif

set script_dir = `dirname $0`

set run_and_verify_topor = "$script_dir/run_and_verify_intel_sat.csh"
set regr = "$script_dir/../regression_instances"
set topor = $1
if (! -e $topor) then
	echo "intel_sat_executable doesn't exist at $topor. Exiting"
	echo "ERROR"
	exit 130
endif
echo "intel_sat_executable was set to $topor"

set inputparams = "$argv[2-]"
echo "Parameters: $inputparams"

foreach f (`ls -rS $regr/ddtbug*.cnf`)
	set insideparams = `sed -nr "s/c Topors command-line.*cnf (.*)/\1/p" $f`
	echo "insideparams: $insideparams"
	echo "$run_and_verify_topor $topor $f $insideparams $inputparams > outt"
	$run_and_verify_topor $topor $f $insideparams $inputparams > outt
	
	set r = `grep -c ERROR outt`
	if ($r != 0) then
		echo "ERROR!"
		exit
	else
		echo "Ok"
		rm outt		
	endif
end

foreach f (`ls -rS $regr/regr*.cnf`)
	foreach m (0 1)
		foreach v (0 1 2 3 4 5 6 7 8)
			set modified_params = "/topor_tool/solver_mode $m /mode/value $v"
			echo "$run_and_verify_topor $topor $f $modified_params $inputparams > outt"
			$run_and_verify_topor $topor $f $modified_params $inputparams > outt
			set r = `grep -c ERROR outt`
			if ($r != 0) then
				echo "ERROR!"
				exit
			else
				echo "Ok"
				rm outt		
			endif
		end
	end
end
