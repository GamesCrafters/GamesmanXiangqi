#!/bin/bash
#SBATCH --job-name=chinesechess
#SBATCH --account=fc_gamecrafters
#SBATCH --partition=savio3
#SBATCH --nodes=30
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=32
#SBATCH --time=02:00:00

cd bin
module load gcc openmpi
export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK
# Usage: ./solve <n-pieces> <n-threads> <memory-in-GiB>
mpirun ./solve 3 32 90
