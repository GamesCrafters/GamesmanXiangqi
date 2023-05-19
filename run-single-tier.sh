#!/bin/bash
#SBATCH --job-name=101011
#SBATCH --account=fc_gamecrafters
#SBATCH --partition=savio3_htc
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=40
#SBATCH --time=48:00:00

cd bin
export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK
# Usage: ./solve <tier-to-solve> <memory-in-GiB>
/usr/bin/time ./solve 000000101011__ 380
