#! /bin/csh -f

if ($#argv < 2) then
    echo "This script runs, verifies and delta-debugs IntelSAT (if there is a need). It returns 0 on success and other values on failure"
    echo "Parameters: the command-line (i.e., <topor_tool> <file> [Any additional parameters])"
    exit 140
endif

echo "The command-line $argv[1-]"

set my_dir = `dirname $0`
set run_and_verify_topor = ${my_dir}/run_and_verify_intel_sat.csh

set tmpdir = "/tmp/$USER/delta_debug_topor_`hostname`_$$"
mkdir -p $tmpdir
echo "The temporary directory: $tmpdir"

set out_file = $tmpdir/outt
echo "$run_and_verify_topor $argv[1-] > $out_file"
$run_and_verify_topor $argv[1-] > $out_file

# Fetch the input file
set f = $argv[2]
set is_incremental = `grep -c "^s " $f`

if ($is_incremental) then
	set uq = `grep -c "Result = UNSATISFIABLE" $out_file`
	set sq = `grep -c "Result = SATISFIABLE" $out_file`
	@ allq = $sq + $uq
	echo "All / SAT / UNSAT queries = ${allq} / ${sq} / ${uq}"
endif
tail -n 1 $out_file

set isok = `tail -n 1 $out_file | egrep -c "^Ok"`
if ($isok) then	
	# Ok --> exit peacefully
	rm -rf $tmpdir
	exit
endif 

#########################################
# ERROR --> delta-debug
#########################################

# Fetch topor_tool
set t = $argv[1]

set fcurr = ${out_file}.cnf
echo "The minimized CNF will be generated in $fcurr"
set forig = ${out_file}.orig.cnf
cp $f $forig
echo "The original CNF with the error is $forig"

set curr_query = `egrep "Query [0-9]+ out of [0-9]+" $out_file | tail -n 1 | awk '{print $2}'`
set queries = `egrep "Query [0-9]+ out of [0-9]+" $out_file | tail -n 1 | awk '{print $5}'`
echo "ERROR in query $curr_query out of $queries"

if ($is_incremental) then
	# Incremental: remove the rest of the queries (beyond curr_query) and put the result in fcurr

	# Find the line of the current query in the input CNF
	set curr_query_line_num_in_f = `grep -n -m$curr_query "^s " $f | tail -n 1 | cut -f1 -d:`
	echo "curr_query_line_num_in_f = $curr_query_line_num_in_f"
	# Put only the lines till the current query in fcurr
	head -n $curr_query_line_num_in_f $f > $fcurr
else
	# Non-incremental: reduce the problem to incremental
	cp $f $fcurr
	echo "s 0" >> $fcurr	
endif

set ispcnf = `grep -c "^p cnf" $f`

if ($ispcnf != 0) then
	# Get the number of variables from the CNF (the highest integer in the clauses and the assumption lists)
	set vars = `egrep "s -?[0-9]|^-?[0-9]" $f | grep -Eo '[0-9]+' | sort -rn | head -n 1`
endif

# Remove as many as possible queries

set removed_queries = 0
set already_tested_query = $curr_query
set latest_untested_query = 1

while ($latest_untested_query < $already_tested_query)
	echo "Can we remove query $latest_untested_query out of $already_tested_query and still get an error?"
	rm $out_file
	# Delete the query latest_untested_query
	set latest_untested_query_line_num_in_fcurr = `grep -n -m$latest_untested_query "^s " $fcurr | tail -n 1 | cut -f1 -d:`
	set fcurrtest = ${fcurr}.test
	sed "${latest_untested_query_line_num_in_fcurr}d" $fcurr > $fcurrtest
	set newcl = "$run_and_verify_topor $t $fcurrtest $argv[3-]"	
	echo "$newcl > $out_file"
	$newcl > $out_file
	tail -n 1 $out_file
	set isok = `tail -n 1 $out_file | egrep -c "^Ok"`
	if ($isok) then
		@ latest_untested_query = $latest_untested_query + 1 
		echo "The minimized CNF in $fcurr *not* updated"
	else
		@ removed_queries = $removed_queries + 1
		@ already_tested_query = $already_tested_query - 1 
		mv $fcurrtest $fcurr
		echo "The minimized CNF in $fcurr updated!"
	endif
end

# Reduce the number of clauses as much as possible
set removed_clauses = 0
set clss = `egrep -c "^-?[0-9]" $fcurr` 
set already_tested_cls = $clss
set latest_untested_cls = 1

while ($latest_untested_cls <= $already_tested_cls)
	echo "Can we remove clause $latest_untested_cls out of $already_tested_cls and still get an error?"
	rm $out_file
	# Delete the clause latest_untested_cls
	set latest_untested_cls_line_num_in_fcurr = `egrep -n -m$latest_untested_cls "^-?[0-9]" $fcurr | tail -n 1 | cut -f1 -d:`
	set fcurrtest = ${fcurr}.test
	sed "${latest_untested_cls_line_num_in_fcurr}d" $fcurr > $fcurrtest
	if ($ispcnf != 0) then
		# Replace the #clauses in "p cnf" by the correct one
		sed -i '/p cnf/d' $fcurrtest
		set clss_real = `egrep -c "^-?[0-9]" $fcurrtest` 
		sed -i "1i p cnf $vars $clss_real" $fcurrtest
	endif
	set newcl = "$run_and_verify_topor $t $fcurrtest $argv[3-]"	
	echo "$newcl > $out_file"
	$newcl > $out_file
	tail -n 1 $out_file
	set isok = `tail -n 1 $out_file | egrep -c "^Ok"`
	set curr_queries = `egrep -c "^s " $fcurrtest`
	if ($isok) then
		@ latest_untested_cls = $latest_untested_cls + 1 
		echo "The minimized CNF in $fcurr *not* updated. Still $curr_queries queries."
	else
		set err_query_number = `grep "ERROR query" $out_file | awk '{print $3}'`
		if ($err_query_number == "") then
			set err_query_number = $curr_queries
			echo "Couldn't grep err_query_number: has there been a crash?"
		endif
		if ($err_query_number != $curr_queries) then
			# Find the line of the current query in the current CNF
			set err_query_line_num_in_fcurrtest = `grep -n -m$err_query_number "^s " $fcurrtest | tail -n 1 | cut -f1 -d:`
			echo "err_query_line_num_in_fcurrtest = $err_query_line_num_in_fcurrtest"
			# Put only the lines till the current query in fcurr
			head -n $err_query_line_num_in_fcurrtest $fcurrtest > $fcurr
			echo "The minimized CNF in $fcurr updated: number of queries is down from ${curr_queries} to ${err_query_line_num_in_fcurrtest}"			
		else
			mv $fcurrtest $fcurr
			echo "The minimized CNF in $fcurr updated: number of queries is still ${curr_queries}"
		endif
		@ removed_clauses = $removed_clauses + 1
		@ already_tested_cls = $already_tested_cls - 1 		
	endif
end

# Removing all the comments
sed -i '/^c /d' $fcurr

@ removed = $removed_queries + $removed_clauses
echo "Info regarding changes: removed $removed queries (${removed_queries}) and clauses (${removed_clauses})"
# copy to Topor regression
set fcurr_regr = $my_dir/../regression_instances/ddtbug_`date "+%d_%m_%Y-%H_%M_%S"`_`hostname`_$$.cnf
cp $fcurr $fcurr_regr
set fcurr = $fcurr_regr
sed -i "1 i\c Topors command-line: $argv[1-]" ${fcurr} 

set gitsfile = ${out_file}.git_status
git status >& $gitsfile
set ifgitfailed = `grep -c fatal $gitsfile`
if ($ifgitfailed == 0) then
	sed -i "1 i\c git `git log -1 | head -n 1`" ${fcurr} 
	sed -i "1 i\c git current branch `git rev-parse --abbrev-ref HEAD`" ${fcurr} 
endif
rm $gitsfile

echo "The minimized CNF is ${fcurr} To reproduce the problem:"
echo "$run_and_verify_topor $t $fcurr $argv[3-]"
echo "ERROR"
