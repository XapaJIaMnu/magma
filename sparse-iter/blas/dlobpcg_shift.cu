/*
    -- MAGMA (version 1.5.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date September 2014

       @generated from zlobpcg_shift.cu normal z -> d, Tue Sep  2 12:38:33 2014

*/

#include "common_magma.h"

__global__ void 
magma_dlobpcg_shift_kernel( magma_int_t num_rows, magma_int_t num_vecs, 
        magma_int_t shift, double *x ){

    int idx = threadIdx.x ;     // thread in row
    int row = blockIdx.y * gridDim.x + blockIdx.x; // global block index

    if( row<num_rows){
        double tmp = x[idx];
        __syncthreads();

        if( idx > shift-1 ){
            idx-=shift;
            x[idx] = tmp;
            __syncthreads();
        }

    }
}




/**
    Purpose
    -------
    
    For a Block-LOBPCG, the set of residuals (entries consecutive in memory)  
    shrinks and the vectors are shifted in case shift residuals drop below 
    threshold. The memory layout of x is:

        / x1[0] x2[0] x3[0] \
        | x1[1] x2[1] x3[1] |
    x = | x1[2] x2[2] x3[2] | = x1[0] x2[0] x3[0] x1[1] x2[1] x3[1] x1[2] .
        | x1[3] x2[3] x3[3] |
        \ x1[4] x2[4] x3[4] /
    
    Arguments
    ---------

    @param
    num_rows    magma_int_t
                number of rows

    @param
    num_vecs    magma_int_t
                number of vectors

    @param
    shift       magma_int_t
                shift number

    @param
    x           double*
                input/output vector x


    @ingroup magmasparse_daux
    ********************************************************************/

extern "C" magma_int_t
magma_dlobpcg_shift(    magma_int_t num_rows,
                        magma_int_t num_vecs, 
                        magma_int_t shift,
                        double *x ){

    magma_int_t num_threads = num_vecs;
    // every thread handles one row containing the 
    if (  num_threads > 1024 )
        printf("error: too many threads requested.\n");

    int Ms = num_threads * sizeof( double );
    if (  Ms > 1024*8 )
        printf("error: too much shared memory requested.\n");

    dim3 block( num_threads, 1, 1 );

    int dimgrid1 = sqrt(num_rows);
    int dimgrid2 = (num_rows + dimgrid1 -1 ) / dimgrid1;

    dim3 grid( dimgrid1, dimgrid2, 1);

    magma_dlobpcg_shift_kernel<<< grid, block, Ms, magma_stream >>>
            ( num_rows, num_vecs, shift, x );


    return MAGMA_SUCCESS;
}



