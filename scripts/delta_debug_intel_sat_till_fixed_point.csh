#! /bin/csh -f

if ($#argv < 2) then
    echo "This script runs, verifies and delta-debugs IntelSAT till fix-point (if there is a need). It returns 0 on success and other values on failure"
    echo "Parameters: the command-line (i.e., <topor_tool> <file> [Any additional parameters])"
    exit 140
endif

echo "The command-line $argv[1-]"

set my_dir = `dirname $0`
set delta_debug_topor = ${my_dir}/delta_debug_intel_sat.csh

set tmpdir = "/tmp/$USER/delta_debug_topor_till_fixed_point_`hostname`_$$"
mkdir -p $tmpdir
echo "The temporary directory: $tmpdir"

set out_file = $tmpdir/outt
echo "$delta_debug_topor $argv[1-] |& tee $out_file"
$delta_debug_topor $argv[1-] |& tee $out_file

set isok = `tail -n 1 $out_file | egrep -c "^Ok"`
if ($isok) then	
	# Ok --> exit peacefully
	echo "Ok"
	rm -rf $tmpdir
	exit
endif 

set tool = $argv[1]

set newf = `egrep "The minimized CNF is.*To reproduce the problem" $out_file | awk '{print $5}'`
set anychanges = `grep "Info regarding changes" $out_file | awk '{print $5}'`
grep "Info regarding changes" $out_file
echo "The minimized CNF is ${newf}"

set oldminimized = ${newf}

while ($anychanges) 	
	echo "$delta_debug_topor $tool $newf $argv[3-] >& $out_file"
	$delta_debug_topor $tool $newf $argv[3-] >& $out_file
	
	set newf = `egrep "The minimized CNF is.*To reproduce the problem" $out_file | awk '{print $5}'`
	set anychanges = `grep "Info regarding changes" $out_file | awk '{print $5}'`
	
	grep "Info regarding changes" $out_file
	echo "The minimized CNF is ${newf}"
	rm $oldminimized
	set oldminimized = ${newf}
	
	if ($anychanges == 0) then
		echo "No changes --> existing. Recall that the minimized CNF is ${newf}"		
	endif
end
