#!/bin/bash
MYPWD=$(pwd)

# locate executible
execsrc="../../../bin/NetFV"

# libmesh options
libmesh_opts=" -ksp_type gmres -pc_type bjacobi -sub_pc_type ilu -sub_pc_factor_levels 10"

# go to output directory
if [[ ! -d "out" ]]; then
	mkdir out 
else
	rm -r out
	mkdir out
fi

# clear old files
# rm ./* &> /dev/null

python -B problem_setup.py input

mv input.in out/.
mv tum_ic_data.csv out/.
mv test_single_line.dgf out/.

cd "out"

#
# run test
#
# mpiexec -np 1 "$execsrc" input.in "$libmesh_opts" 2>&1 | tee output.txt
"$execsrc" input.in "$libmesh_opts"  2>&1 | tee output.txt
