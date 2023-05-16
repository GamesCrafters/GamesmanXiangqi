#include "solver.h"
#include "solvermpi.h"
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

void init_multi(char **argv, int processID) {
    if (processID == 0) {
        /* Manager node. */
        solve_mpi_manager(atoi(argv[1]), atoi(argv[2]));
    } else {
        /* Worker node. */
        solve_mpi_worker((uint64_t)atoi(argv[3]) << 30, false);
    }
}

void init_single(char **argv) {
    uint8_t nPiecesMax = atoi(argv[1]);
    uint64_t nthread = atoi(argv[2]);
    uint64_t mem = ((uint64_t)atoi(argv[3])) << 30;
    printf("main: solving %d pieces on a single node with %zd thread(s) and %zd bytes of memory.\n",
           nPiecesMax, nthread, mem);
    solve_local(atoi(argv[1]), atoi(argv[2]), (uint64_t)atoi(argv[3]) << 30, false);
}

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

    if (clusterSize < 1) {
        printf("main: (fatal) clusterSize is less than 1.\n");
		MPI_Finalize();
		return 1;
    } else if (clusterSize == 1) {
        init_single(argv);
	} else {
        init_multi(argv, processID);
    }

    /* Terminates MPI execution environment. */
    MPI_Finalize();
    return 0;
}
