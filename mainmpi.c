#include "solvermpi.h"
#include <mpi.h>
#include <stdio.h>

int main(int argc, char **argv) {
    int processID, clusterSize;

    /* Initialize the MPI environment. All code between MPI_Init
       and MPI_Finalize gets run by all nodes. */
    MPI_Init(&argc, &argv);

    MPI_Comm_size(MPI_COMM_WORLD, &clusterSize);
    MPI_Comm_rank(MPI_COMM_WORLD, &processID);
    if (clusterSize <= 1) {
		printf("main: not enough nodes for MPI.\n");
		MPI_Finalize();
		return 1;
	}

    if (processID == 0) {
        /* Manager node. */
        solve_mpi_manager(3, 32ULL);
    } else {
        /* Worker node. */
        solve_mpi_worker(2ULL << 30, false);
    }

    /* Terminates MPI execution environment. */
    MPI_Finalize();
    return 0;
}
