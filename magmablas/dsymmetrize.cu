/*
    -- MAGMA (version 1.5.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date September 2014

       @generated from zsymmetrize.cu normal z -> d, Tue Sep  2 12:38:16 2014
       @author Mark Gates
*/
#include "common_magma.h"

#define NB 64

/*
    Matrix is m x m, and is divided into block rows, each NB x m.
    Each block has NB threads.
    Each thread copies one row, iterating across all columns below diagonal.
    The bottom block of rows may be partially outside the matrix;
    if so, rows outside the matrix (i >= m) are disabled.
*/
__global__ void
dsymmetrize_lower( int m, double *dA, int ldda )
{
    // dA iterates across row i and dAT iterates down column i.
    int i = blockIdx.x*NB + threadIdx.x;
    double *dAT = dA;
    if ( i < m ) {
        dA  += i;
        dAT += i*ldda;
        double *dAend = dA + i*ldda;
        while( dA < dAend ) {
            *dAT = (*dA);  // upper := lower
            dA  += ldda;
            dAT += 1;
        }
    }
}


// only difference with _lower version is direction dA=dAT instead of dAT=dA.
__global__ void
dsymmetrize_upper( int m, double *dA, int ldda )
{
    // dA iterates across row i and dAT iterates down column i.
    int i = blockIdx.x*NB + threadIdx.x;
    double *dAT = dA;
    if ( i < m ) {
        dA  += i;
        dAT += i*ldda;
        double *dAend = dA + i*ldda;
        while( dA < dAend ) {
            *dA = (*dAT);  // lower := upper
            dA  += ldda;
            dAT += 1;
        }
    }
}


/**
    Purpose
    -------
    
    DSYMMETRIZE copies lower triangle to upper triangle, or vice-versa,
    to make dA a general representation of a symmetric matrix.
    
    Arguments
    ---------
    
    @param[in]
    uplo    magma_uplo_t
            Specifies the part of the matrix dA that is valid on input.
      -     = MagmaUpper:      Upper triangular part
      -     = MagmaLower:      Lower triangular part
    
    @param[in]
    m       INTEGER
            The number of rows of the matrix dA.  M >= 0.
    
    @param[in,out]
    dA      DOUBLE_PRECISION array, dimension (LDDA,N)
            The m by m matrix dA.
    
    @param[in]
    ldda    INTEGER
            The leading dimension of the array dA.  LDDA >= max(1,M).
    
    @param[in]
    queue   magma_queue_t
            Queue to execute in.

    @ingroup magma_daux2
    ********************************************************************/
extern "C" void
magmablas_dsymmetrize_q(
    magma_uplo_t uplo, magma_int_t m, double *dA, magma_int_t ldda,
    magma_queue_t queue )
{
    magma_int_t info = 0;
    if ( uplo != MagmaLower && uplo != MagmaUpper )
        info = -1;
    else if ( m < 0 )
        info = -2;
    else if ( ldda < max(1,m) )
        info = -4;
    
    if ( info != 0 ) {
        magma_xerbla( __func__, -(info) );
        return;
    }
    
    if ( m == 0 )
        return;
    
    
    dim3 threads( NB );
    dim3 grid( (m + NB - 1)/NB );
    
    if ( uplo == MagmaUpper ) {
        dsymmetrize_upper<<< grid, threads, 0, queue >>>( m, dA, ldda );
    }
    else {
        dsymmetrize_lower<<< grid, threads, 0, queue >>>( m, dA, ldda );
    }
}


/**
    @see magmablas_dsymmetrize_q
    @ingroup magma_daux2
    ********************************************************************/
extern "C" void
magmablas_dsymmetrize(
    magma_uplo_t uplo, magma_int_t m, double *dA, magma_int_t ldda )
{
    magmablas_dsymmetrize_q( uplo, m, dA, ldda, magma_stream );
}
