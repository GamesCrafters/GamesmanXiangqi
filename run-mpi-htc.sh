#!/bin/bash
#SBATCH --job-name=chinesechess
#SBATCH --account=fc_gamecrafters
#SBATCH --partition=savio3_htc
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=40
#SBATCH --time=48:00:00

cd bin
module load gcc openmpi
export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK
# Usage: ./solve <n-pieces> <n-threads> <memory-in-GiB>
mpirun ./solve 4 40 380
