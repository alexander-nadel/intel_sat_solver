#! /bin/csh -f

if ($#argv < 2) then
    echo "This script run and verifies IntelSAT (that is, its executable, such as, intel_sat_solver_static). It returns 0 on success and other values on failure. If the command-line contains /topor_tool/print_ucore 1, it will also verify the unsatisfiable core. Features:"
    echo "- Incremental instances in IntelSAT format (DIMACS + \"s assump1 ... assumpn 0\" lines) are supported, while still using the standard tools drat-trim and precochk for verification using a new methodology."
    echo "- Every generated clause is verified using drat-trim."
    echo "- Every solution is verified using precochk."
    echo "Parameters: the command-line (i.e., <intel_sat_solver_static> <file> [Any additional parameters])"
    exit 140
endif

echo "The command-line $argv[1-]"

set tmpdir = "/tmp/$USER/run_and_verify_topor_`hostname`_$$"
mkdir -p $tmpdir
echo "The temporary directory: $tmpdir"
# The output file of topor_tool
set out_file = $tmpdir/outt
set drat_file = ${out_file}.drat

echo "$argv[1-] /topor_tool/text_drat_file $drat_file /topor_tool/print_model 1 >& $out_file"
$argv[1-] /topor_tool/text_drat_file $drat_file /topor_tool/print_model 1 >& $out_file
echo "Invocation completed"
set issat = `grep -c "s SATISFIABLE" $out_file` 
set isunsat = `grep -c "s UNSATISFIABLE" $out_file` 
if ($issat == 0 && $isunsat == 0) then
	echo "ERROR: unknown result"
	exit 120
endif

set f = $argv[2]
set incremental_queries = `egrep -c "^s " $f`

set print_ucore = `echo $argv[1-] | grep -c "/topor_tool/print_ucore 1"`
echo "print_ucore = $print_ucore"

# script_dir is used only to get the verifiers: precochk and drat
set script_dir = `dirname $0`
set cs = $script_dir/../third_party/dimocheck
set cu = $script_dir/../third_party/drat-trim
# drat-trim params for both SAT & UNSAT: forward; verbose
set cupparams = "-f -v -U"
# Forward RUP UNSAT
set cupunsat = "$cupparams"
# Forward RUP SAT
set cupsat = "-S $cupparams"

# The output file of the verifier
set verify_out = ${out_file}.verify
# Get the number of variables from the CNF (the highest integer in the clauses and the assumption lists)
set vars = `egrep "s -?[0-9]|^-?[0-9]" $f | grep -Eo '[0-9]+' | sort -rn | head -n 1`

if ($incremental_queries) then
	echo "Type Incremental: $incremental_queries queries"
	set curr_query = 1
	
	while ($curr_query <= $incremental_queries)
		echo "====================================================="
		echo "Query $curr_query out of $incremental_queries"
		# fcurr_query: the would-be non-incremental input CNF of the current query only we're going to construct
		set fcurr_query = ${out_file}.cnf
		# Find the line of the current query in the input CNF
		set curr_query_line_num_in_f = `grep -n -m$curr_query "^s " $f | tail -n 1 | cut -f1 -d:`
		echo "curr_query_line_num_in_f = $curr_query_line_num_in_f"
		# Getting the result of the curent query from the incremental output
		set curr_res = `grep -m$curr_query "^s " $out_file | tail -n 1`
		set r = `echo $curr_res | awk '{print $2}'`
		echo "Result = $r"
		# Put only the clauses from $f till the current query in fcurr_query
		head -n $curr_query_line_num_in_f $f | egrep "^-?[0-9]" > $fcurr_query
		if ($print_ucore == 0 || $r != "UNSATISFIABLE") then
			# Append unit clauses for every assumption (if any) to fcurr_query
			grep -m$curr_query "^s " $f | tail -n 1 | tr ' ' "\n" | egrep -v "^(s|0)" | awk '{print $0, "0"}' >> $fcurr_query
		else
			echo "Appended only the assumptions in the ucore"
			# Append unit clauses for assumptions in the ucore (if any) to fcurr_query
			grep -m$curr_query -A 1 "^s " $out_file | tail -n 1 | tr ' ' "\n" | egrep -v "^(v|0)" | awk '{print $0, "0"}' >> $fcurr_query
			
		endif
		# The number of clauses in fcurr_query
		set clss = `egrep -c "^-?[0-9]" $fcurr_query` 		
		# Prepend p cnf vars clss to fcurr_query
		sed -i "1i p cnf $vars $clss" $fcurr_query
		echo "fcurr_query = $fcurr_query completed after adding p cnf $vars $clss"
		
		# fcurr_query_res: the would-be result file for the current query only
		set fcurr_query_res = ${out_file}.fcurr_query_res
		# Emitting the result to fcurr_query_res
		echo $curr_res > $fcurr_query_res
		echo "Created $fcurr_query_res file by emitting the current result $curr_res there"
		
		if ($r == "SATISFIABLE") then
			if ($clss == 0) then
				echo "SATISFIABLE empty query $curr_query completed succesfully!"
			else
				# The result is SAT
				
				# fcurr_query_drat: the would be drat for the current query
				set fcurr_query_drat = ${out_file}.fcurr_query_drat
				# get the line number of the "c query completed $curr_query" corresponding to the current query from the drat
				set curr_query_end_line_num_in_orig_drat = `egrep -w -n "c query completed $curr_query" $drat_file | tr ':' ' ' | awk '{print $1}'`
				if ($curr_query_end_line_num_in_orig_drat == "") then
					echo "ERROR query ${curr_query} (SATISFIABLE): DRAT cut off (was there an assertion failure, a segmentation fault or something other unexpected event?)"
					exit 173
				endif
				echo "curr_query_end_line_num_in_orig_drat = $curr_query_end_line_num_in_orig_drat"
				# Put only the lines till the current query from the drat in fcurr_query_drat, but without the "0" and the comment lines
				head -n $curr_query_end_line_num_in_orig_drat $drat_file | egrep -v "^(0|c)" > $fcurr_query_drat
				# Verifying with drat
				echo "SAT -- running $cu"
				echo "$cu $fcurr_query $fcurr_query_drat $cupsat"
				$cu $fcurr_query $fcurr_query_drat  $cupsat > $verify_out
				set ok = `grep -c "s DERIVATION" $verify_out`
				if ($ok == 0) then
					echo "ERROR query ${curr_query} SAT derivation verification failed!"
					exit 175
				endif
				
				# Get the "pline" with the solution
				set pline = `grep -A 1 -m$curr_query "^s " $out_file | tail -n 1`
				# and emit it to fcurr_query_res
				echo $pline >> $fcurr_query_res
				# Verifying with precochk
				echo "SAT -- running $cs"
				echo "$cs $fcurr_query $fcurr_query_res"
				$cs $fcurr_query $fcurr_query_res > $verify_out
				set ok = `egrep -c "satisfiable and solution correct|s MODEL_SATISFIES_FORMULA" $verify_out`
				if ($ok == 0) then
					echo "ERROR query ${curr_query} SAT verification failed!"
					exit 160
				endif
				
				echo "SATISFIABLE query $curr_query completed succesfully!"
			endif
		endif
		if ($r == "UNSATISFIABLE") then
			# The result is UNSAT
			# fcurr_query_drat: the would be drat for the current query
			set fcurr_query_drat = ${out_file}.fcurr_query_drat
			# get the line number of the "c query completed $curr_query" corresponding to the current query from the drat
			set curr_query_end_line_num_in_orig_drat = `egrep -w -n "c query completed $curr_query" $drat_file | tr ':' ' ' | awk '{print $1}'`
			echo "curr_query_end_line_num_in_orig_drat = $curr_query_end_line_num_in_orig_drat"
			if ($curr_query_end_line_num_in_orig_drat == "") then
				echo "ERROR query ${curr_query} (UNSATISFIABLE): DRAT cut off (was there an assertion failure, a segmentation fault or something other unexpected event?)"
				exit 174
			endif
			# Put only the lines till the current query from the drat in fcurr_query_drat, but without the "0" lines
			head -n $curr_query_end_line_num_in_orig_drat $drat_file | egrep -v "^(0|c)" > $fcurr_query_drat
			# Need the number of lines in the current drat for an adjustment, described below
			set lines_in_drat = `wc -l $fcurr_query_drat | awk '{print $1}'`
			if ($lines_in_drat == 0) then
				# an empty proof in text format doesn't work for some reason in the drat verification tool, whereas "a" is interpreted as a binary "0"
				echo "a" > $fcurr_query_drat
			else
				# Restore the last 0
				echo "0" >> $fcurr_query_drat
			endif
			# Verifying with drat
			echo "UNSAT -- running $cu"
			echo "$cu $fcurr_query $fcurr_query_drat $cupunsat"
			$cu $fcurr_query $fcurr_query_drat  $cupunsat > $verify_out
			set ok = `grep -c "s VERIFIED" $verify_out`
			if ($ok == 0) then
				echo "ERROR query ${curr_query} UNSAT verification failed!"
				exit 170
			endif
			echo "UNSATISFIABLE query $curr_query completed succesfully!"
		endif
		if ($r != "SATISFIABLE" && $r != "UNSATISFIABLE") then
			echo "ERROR query ${curr_query} couldn't parse current result $r"
			exit 150
		endif
		@ curr_query = $curr_query + 1		
	end
else
	# The non-incremental case
	echo "Type Not Incremental"
	set ispcnf = `grep -c "^p cnf" $f` 
	set newf = ${out_file}.cnf
	cp $f $newf
	if ($ispcnf == 0) then
		set clss = `egrep -c "^-?[0-9]" $f` 
		sed -i "1i p cnf $vars $clss" $newf
		echo "Added p cnf $vars $clss to $newf"
	endif
	set clss = `grep "^p cnf" $newf | awk '{print $4}'`
	echo "clss = $clss"
	if ($issat) then
		if ($clss == 0) then
			echo "SAT -- empty"
		else
			echo "SAT -- running $cs"
			echo "$cs $newf $out_file"
			$cs $newf $out_file > $verify_out
			set ok = `grep -c "satisfiable and solution correct" $verify_out`
			if ($ok == 0) then
				echo "ERROR: SAT verification failed!"
				exit 130
			endif
			
			echo "SAT -- running $cu"
			echo "$cu $newf $drat_file $cupsat"
			$cu $newf $drat_file $cupsat > $verify_out
			set ok = `grep -c "s DERIVATION" $verify_out`
			if ($ok == 0) then
				echo "ERROR: SAT derivation verification failed!"
				exit 135
			endif
		endif
	endif
	
	if ($isunsat) then
		echo "UNSAT -- running $cu"
		# If the proof is empty, the text format doesn't work for some reason in the drat verification tool, whereas "a" is interpreted as a binary "0"
		set lines_in_drat = `wc -l $drat_file | awk '{print $1}'`
		set empty_proof = `grep -c '^0$' $drat_file`
		if ($lines_in_drat == 1 && $empty_proof == 1) then
			echo "a" > $drat_file		
		endif
		echo "$cu $newf $drat_file $cupunsat"
		$cu $newf $drat_file $cupunsat > $verify_out
		set ok = `grep -c "s VERIFIED" $verify_out`
		if ($ok == 0) then
			echo "ERROR: UNSAT verification failed!"
			exit 140
		endif
	endif
endif

rm -rf $tmpdir
echo "Ok"

