#!/bin/bash
#SBATCH --job-name=chinesechess
#SBATCH --account=fc_gamecrafters
#SBATCH --partition=savio3
#SBATCH --nodes=24
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=40
#SBATCH --time=72:00:00

cd bin
module load gcc openmpi
export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK
# Usage: ./solve <n-pieces> <n-threads> <memory-in-GiB>
mpirun ./solve 255 40 90
