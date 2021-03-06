/*
    -- MAGMA (version 1.5.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date September 2014

       @generated from zjacobisetup.cu normal z -> s, Tue Sep  2 12:38:33 2014
       @author Hartwig Anzt

*/
#include "common_magma.h"

#if (GPUSHMEM < 200)
   #define BLOCK_SIZE 128
#else
   #define BLOCK_SIZE 512
#endif



__global__ void 
svjacobisetup_gpu(  int num_rows, 
                    float *b, 
                    float *d, 
                    float *c,
                    float *x){

    int row = blockDim.x * blockIdx.x + threadIdx.x ;

    if(row < num_rows ){
        c[row] = b[row] / d[row];
        x[row] = c[row];
    }
}





/**
    Purpose
    -------

    Prepares the Jacobi Iteration according to
       x^(k+1) = D^(-1) * b - D^(-1) * (L+U) * x^k
       x^(k+1) =      c     -       M        * x^k.

    Returns the vector c. It calls a GPU kernel

    Arguments
    ---------

    @param
    num_rows    magma_int_t
                number of rows
                
    @param
    b           magma_s_vector
                RHS b

    @param
    d           magma_s_vector
                vector with diagonal entries

    @param
    c           magma_s_vector*
                c = D^(-1) * b

    @param
    x           magma_s_vector*
                iteration vector

    @ingroup magmasparse_sgegpuk
    ********************************************************************/

extern "C" magma_int_t
magma_sjacobisetup_vector_gpu(  int num_rows, 
                                float *b, 
                                float *d, 
                                float *c,
                                float *x ){


   dim3 grid( (num_rows+BLOCK_SIZE-1)/BLOCK_SIZE, 1, 1);

   svjacobisetup_gpu<<< grid, BLOCK_SIZE, 0 >>>( num_rows, b, d, c, x );

   return MAGMA_SUCCESS;
}






__global__ void 
sjacobidiagscal_kernel(  int num_rows, 
                    float *b, 
                    float *d, 
                    float *c){

    int row = blockDim.x * blockIdx.x + threadIdx.x ;

    if(row < num_rows ){
        c[row] = b[row] * d[row];
    }
}





/**
    Purpose
    -------

    Prepares the Jacobi Iteration according to
       x^(k+1) = D^(-1) * b - D^(-1) * (L+U) * x^k
       x^(k+1) =      c     -       M        * x^k.

    Returns the vector c. It calls a GPU kernel

    Arguments
    ---------

    @param
    num_rows    magma_int_t
                number of rows
                
    @param
    b           magma_s_vector
                RHS b

    @param
    d           magma_s_vector
                vector with diagonal entries

    @param
    c           magma_s_vector*
                c = D^(-1) * b

    @ingroup magmasparse_s
    ********************************************************************/

extern "C" magma_int_t
magma_sjacobi_diagscal(         int num_rows, 
                                float *b, 
                                float *d, 
                                float *c){


   dim3 grid( (num_rows+BLOCK_SIZE-1)/BLOCK_SIZE, 1, 1);

   sjacobidiagscal_kernel<<< grid, BLOCK_SIZE, 0 >>>( num_rows, b, d, c );

   return MAGMA_SUCCESS;
}



