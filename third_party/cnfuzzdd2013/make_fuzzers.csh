#! /bin/csh -f

foreach c (cnfuzz cnfuzz_incr)
	echo "g++ -O3 ${c}.c -o ${c}"
	g++ -O3 ${c}.c -o ${c}
	echo "DONE: g++ -O3 ${c}.c -o ${c}"
end
