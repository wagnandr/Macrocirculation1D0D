#!/bin/bash
MY_PWD=$(pwd)

## Fixed variable which should be changed less often
TUMOR_MODEL_EXEC_DIR="/home/wagneran/tumor-model-stack/TumorModels/build_new_model_dev_tobias/bin"

## libmesh options
TUMOR_MODEL_LIBMESH_OPTS=${TUMOR_MODEL_LIBMESH_OPTS:-" -ksp_type gmres -pc_type bjacobi -sub_pc_type ilu -sub_pc_factor_levels 0"}

model_name="$1"
sim_dir="$2"
n_mpi="$3"
run_screen="$4"
pp_tag=$5

(

# if sim dir exist, clean it first
if [[ -d $sim_dir ]]; then
	rm $sim_dir/*vtu > /dev/null 2>&1
	rm $sim_dir/*txt > /dev/null 2>&1
	rm $sim_dir/*vtk > /dev/null 2>&1
	rm $sim_dir/*log > /dev/null 2>&1
fi

cd $sim_dir

echo "$pwd"

if test -f "$TUMOR_MODEL_EXEC_DIR/$model_name"; then
   if [[ $n_mpi == "0" ]]; then
       if [[ $run_screen == "0" ]]; then
           "$TUMOR_MODEL_EXEC_DIR/$model_name" input.in $TUMOR_MODEL_LIBMESH_OPTS
       else
           screen -dmS $pp_tag-$model_name "$TUMOR_MODEL_EXEC_DIR/$model_name" input.in $TUMOR_MODEL_LIBMESH_OPTS
       fi
   else

       if [[ $run_screen == "0" ]]; then
           mpiexec -np $n_mpi "$TUMOR_MODEL_EXEC_DIR/$model_name" input.in $TUMOR_MODEL_LIBMESH_OPTS
       else
           screen -dmS $pp_tag-$model_name mpiexec -np $n_mpi "$TUMOR_MODEL_EXEC_DIR/$model_name" input.in $TUMOR_MODEL_LIBMESH_OPTS
       fi
   fi
else
   echo "Could not find binary $TUMOR_MODEL_EXEC_DIR/$model_name."
   echo "Possibly the TUMOR_MODEL_EXEC_DIR variable in run.sh is not up to date."
fi


) |& tee "$sim_dir/sim.log"
