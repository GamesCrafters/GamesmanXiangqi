#!/bin/bash
#SBATCH --job-name=chinesechess
#SBATCH --account=fc_gamecrafters
#SBATCH --partition=savio3
#SBATCH --nodes=5
#SBATCH --ntasks-per-node=2
#SBATCH --cpus-per-task=16
#SBATCH --time=00:05:00

module load gcc openmpi
export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK
mpirun ./solve
