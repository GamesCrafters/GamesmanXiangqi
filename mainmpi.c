#include "solvermpi.h"
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 4) {
		printf("Usage: %s <n-pieces> <n-threads> <memory-in-GiB>\n",
               argv[0]);
		return 1;
    }

    /* Initialize the MPI environment. All code between MPI_Init
       and MPI_Finalize gets run by all nodes. */
    MPI_Init(&argc, &argv);

    int processID, clusterSize;
    MPI_Comm_size(MPI_COMM_WORLD, &clusterSize);
    MPI_Comm_rank(MPI_COMM_WORLD, &processID);
    if (clusterSize <= 1) {
		printf("main: not enough nodes for MPI.\n");
		MPI_Finalize();
		return 1;
	}

    if (processID == 0) {
        /* Manager node. */
        solve_mpi_manager(atoi(argv[1]), atoi(argv[2]));
    } else {
        /* Worker node. */
        solve_mpi_worker((uint64_t)atoi(argv[3]) << 30, false);
    }

    /* Terminates MPI execution environment. */
    MPI_Finalize();
    return 0;
}
