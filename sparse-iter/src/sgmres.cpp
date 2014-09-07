/*
    -- MAGMA (version 1.5.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date September 2014

       @author Stan Tomov
       @author Hartwig Anzt

       @generated from zgmres.cpp normal z -> s, Tue Sep  2 12:38:35 2014
*/
#include <sys/time.h>
#include <time.h>

#include "common_magma.h"
#include "magmasparse.h"


#define PRECISION_s

#define  q(i)     (q.val + (i)*dofs)
#define  H(i,j)  H[(i)   + (j)*(1+ldh)]
#define HH(i,j) HH[(i)   + (j)*ldh]
#define dH(i,j) dH[(i)   + (j)*(1+ldh)]


#define RTOLERANCE     lapackf77_slamch( "E" )
#define ATOLERANCE     lapackf77_slamch( "E" )


/**
    Purpose
    -------

    Solves a system of linear equations
       A * X = B
    where A is a real sparse matrix stored in the GPU memory.
    X and B are real vectors stored on the GPU memory. 
    This is a GPU implementation of the GMRES method.

    Arguments
    ---------

    @param
    A           magma_s_sparse_matrix
                descriptor for matrix A

    @param
    b           magma_s_vector
                RHS b vector

    @param
    x           magma_s_vector*
                solution approximation

    @param
    solver_par  magma_s_solver_par*
                solver parameters

    @ingroup magmasparse_sgesv
    ********************************************************************/

magma_int_t
magma_sgmres( magma_s_sparse_matrix A, magma_s_vector b, magma_s_vector *x,  
              magma_s_solver_par *solver_par ){

    // prepare solver feedback
    solver_par->solver = Magma_GMRES;
    solver_par->numiter = 0;
    solver_par->info = 0;

    // local variables
    float c_zero = MAGMA_S_ZERO, c_one = MAGMA_S_ONE, 
                                                c_mone = MAGMA_S_NEG_ONE;
    magma_int_t dofs = A.num_rows;
    magma_int_t i, j, k, m = 0;
    magma_int_t restart = min( dofs-1, solver_par->restart );
    magma_int_t ldh = restart+1;
    float nom, rNorm, RNorm, nom0, betanom, r0 = 0.;

    // CPU workspace
    float *H, *HH, *y, *h1;
    magma_smalloc_pinned( &H, (ldh+1)*ldh );
    magma_smalloc_pinned( &y, ldh );
    magma_smalloc_pinned( &HH, ldh*ldh );
    magma_smalloc_pinned( &h1, ldh );

    // GPU workspace
    magma_s_vector r, q, q_t;
    magma_s_vinit( &r, Magma_DEV, dofs, c_zero );
    magma_s_vinit( &q, Magma_DEV, dofs*(ldh+1), c_zero );
    q_t.memory_location = Magma_DEV; 
    q_t.val = NULL; 
    q_t.num_rows = q_t.nnz = dofs;

    float *dy, *dH = NULL;
    if (MAGMA_SUCCESS != magma_smalloc( &dy, ldh )) 
        return MAGMA_ERR_DEVICE_ALLOC;
    if (MAGMA_SUCCESS != magma_smalloc( &dH, (ldh+1)*ldh )) 
        return MAGMA_ERR_DEVICE_ALLOC;

    // GPU stream
    magma_queue_t stream[2];
    magma_event_t event[1];
    magma_queue_create( &stream[0] );
    magma_queue_create( &stream[1] );
    magma_event_create( &event[0] );
    magmablasSetKernelStream(stream[0]);

    magma_sscal( dofs, c_zero, x->val, 1 );              //  x = 0
    magma_scopy( dofs, b.val, 1, r.val, 1 );             //  r = b
    nom0 = betanom = magma_snrm2( dofs, r.val, 1 );     //  nom0= || r||
    nom = nom0  * nom0;
    solver_par->init_res = nom0;
    H(1,0) = MAGMA_S_MAKE( nom0, 0. ); 
    magma_ssetvector(1, &H(1,0), 1, &dH(1,0), 1);
    if ( (r0 = nom0 * RTOLERANCE ) < ATOLERANCE ) 
        r0 = solver_par->epsilon;
    if ( nom < r0 )
        return MAGMA_SUCCESS;

    //Chronometry
    real_Double_t tempo1, tempo2;
    magma_device_sync(); tempo1=magma_wtime();
    if( solver_par->verbose > 0 ){
        solver_par->res_vec[0] = nom0;
        solver_par->timing[0] = 0.0;
    }
    // start iteration
    for( solver_par->numiter= 1; solver_par->numiter<solver_par->maxiter; 
                                                    solver_par->numiter++ ){

        for(k=1; k<=restart; k++) {

        magma_scopy(dofs, r.val, 1, q(k-1), 1);       //  q[0]    = 1.0/||r||
        magma_sscal(dofs, 1./H(k,k-1), q(k-1), 1);    //  (to be fused)

            q_t.val = q(k-1);
            magma_s_spmv( c_one, A, q_t, c_zero, r ); //  r = A q[k] 
    //            if (solver_par->ortho == Magma_MGS ) {
                // modified Gram-Schmidt
                //magmablasSetKernelStream(stream[0]);
                for (i=1; i<=k; i++) {
                    H(i,k) =magma_sdot(dofs, q(i-1), 1, r.val, 1);            
                        //  H(i,k) = q[i] . r
                    magma_saxpy(dofs,-H(i,k), q(i-1), 1, r.val, 1);            
                       //  r = r - H(i,k) q[i]
                }
                H(k+1,k) = MAGMA_S_MAKE( magma_snrm2(dofs, r.val, 1), 0. ); // H(k+1,k) = ||r|| 

            /*}else if (solver_par->ortho == Magma_FUSED_CGS ) {
                // fusing sgemv with snrm2 in classical Gram-Schmidt
                magmablasSetKernelStream(stream[0]);
                magma_scopy(dofs, r.val, 1, q(k), 1);  
                    // dH(1:k+1,k) = q[0:k] . r
                magmablas_sgemv(MagmaTrans, dofs, k+1, c_one, q(0), 
                                dofs, r.val, 1, c_zero, &dH(1,k), 1);
                    // r = r - q[0:k-1] dH(1:k,k)
                magmablas_sgemv(MagmaNoTrans, dofs, k, c_mone, q(0), 
                                dofs, &dH(1,k), 1, c_one, r.val, 1);
                   // 1) dH(k+1,k) = sqrt( dH(k+1,k) - dH(1:k,k) )
                magma_scopyscale(  dofs, k, r.val, q(k), &dH(1,k) );  
                   // 2) q[k] = q[k] / dH(k+1,k) 

                magma_event_record( event[0], stream[0] );
                magma_queue_wait_event( stream[1], event[0] );
                magma_sgetvector_async(k+1, &dH(1,k), 1, &H(1,k), 1, stream[1]); 
                    // asynch copy dH(1:(k+1),k) to H(1:(k+1),k)
            } else {
                // classical Gram-Schmidt (default)
                // > explicitly calling magmabls
                magmablasSetKernelStream(stream[0]);                                                  
                magmablas_sgemv(MagmaTrans, dofs, k, c_one, q(0), 
                                dofs, r.val, 1, c_zero, &dH(1,k), 1); 
                                // dH(1:k,k) = q[0:k-1] . r
                #ifndef SNRM2SCALE 
                // start copying dH(1:k,k) to H(1:k,k)
                magma_event_record( event[0], stream[0] );
                magma_queue_wait_event( stream[1], event[0] );
                magma_sgetvector_async(k, &dH(1,k), 1, &H(1,k), 
                                                    1, stream[1]);
                #endif
                                  // r = r - q[0:k-1] dH(1:k,k)
                magmablas_sgemv(MagmaNoTrans, dofs, k, c_mone, q(0), 
                                    dofs, &dH(1,k), 1, c_one, r.val, 1);
                #ifdef SNRM2SCALE
                magma_scopy(dofs, r.val, 1, q(k), 1);                 
                    //  q[k] = r / H(k,k-1) 
                magma_snrm2scale(dofs, q(k), dofs, &dH(k+1,k) );     
                    //  dH(k+1,k) = sqrt(r . r) and r = r / dH(k+1,k)

                magma_event_record( event[0], stream[0] );            
                            // start sending dH(1:k,k) to H(1:k,k)
                magma_queue_wait_event( stream[1], event[0] );        
                            // can we keep H(k+1,k) on GPU and combine?
                magma_sgetvector_async(k+1, &dH(1,k), 1, &H(1,k), 1, stream[1]);
                #else
                H(k+1,k) = MAGMA_S_MAKE( magma_snrm2(dofs, r.val, 1), 0. );   
                            //  H(k+1,k) = sqrt(r . r) 
                if( k<solver_par->restart ){
                        magmablasSetKernelStream(stream[0]);
                        magma_scopy(dofs, r.val, 1, q(k), 1);                  
                            //  q[k]    = 1.0/H[k][k-1] r
                        magma_sscal(dofs, 1./H(k+1,k), q(k), 1);              
                            //  (to be fused)   
                 }
                #endif
            }*/
            /*     Minimization of  || b-Ax ||  in H_k       */ 
            for (i=1; i<=k; i++) {
                HH(k,i) = magma_cblas_sdot( i+1, &H(1,k), 1, &H(1,i), 1 );
            }
            h1[k] = H(1,k)*H(1,0); 
            if (k != 1){
                for (i=1; i<k; i++) {
                    HH(k,i) = HH(k,i)/HH(i,i);//
                    for (m=i+1; m<=k; m++){
                        HH(k,m) -= HH(k,i) * HH(m,i) * HH(i,i);
                    }
                    h1[k] -= h1[i] * HH(k,i);   
                }    
            }
            y[k] = h1[k]/HH(k,k); 
            if (k != 1)  
                for (i=k-1; i>=1; i--) {
                    y[i] = h1[i]/HH(i,i);
                    for (j=i+1; j<=k; j++)
                        y[i] -= y[j] * HH(j,i);
                }                    
            m = k;
            rNorm = fabs(MAGMA_S_REAL(H(k+1,k)));
        }/*     Minimization done       */ 
        // compute solution approximation
        magma_ssetmatrix(m, 1, y+1, m, dy, m );
        magma_sgemv(MagmaNoTrans, dofs, m, c_one, q(0), dofs, dy, 1, 
                                                    c_one, x->val, 1); 

        // compute residual
        magma_s_spmv( c_mone, A, *x, c_zero, r );      //  r = - A * x
        magma_saxpy(dofs, c_one, b.val, 1, r.val, 1);  //  r = r + b
        H(1,0) = MAGMA_S_MAKE( magma_snrm2(dofs, r.val, 1), 0. ); 
                                            //  RNorm = H[1][0] = || r ||
        RNorm = MAGMA_S_REAL( H(1,0) );
        betanom = fabs(RNorm);  

        if( solver_par->verbose > 0 ){
            magma_device_sync(); tempo2=magma_wtime();
            if( (solver_par->numiter)%solver_par->verbose==0 ) {
                solver_par->res_vec[(solver_par->numiter)/solver_par->verbose] 
                        = (real_Double_t) betanom;
                solver_par->timing[(solver_par->numiter)/solver_par->verbose] 
                        = (real_Double_t) tempo2-tempo1;
            }
        }

        if (  betanom  < r0 ) {
            break;
        } 
    }

    magma_device_sync(); tempo2=magma_wtime();
    solver_par->runtime = (real_Double_t) tempo2-tempo1;
    float residual;
    magma_sresidual( A, b, *x, &residual );
    solver_par->iter_res = betanom;
    solver_par->final_res = residual;

    if( solver_par->numiter < solver_par->maxiter){
        solver_par->info = 0;
    }else if( solver_par->init_res > solver_par->final_res ){
        if( solver_par->verbose > 0 ){
            if( (solver_par->numiter)%solver_par->verbose==0 ) {
                solver_par->res_vec[(solver_par->numiter)/solver_par->verbose] 
                        = (real_Double_t) betanom;
                solver_par->timing[(solver_par->numiter)/solver_par->verbose] 
                        = (real_Double_t) tempo2-tempo1;
            }
        }
        solver_par->info = -2;
    }
    else{
        if( solver_par->verbose > 0 ){
            if( (solver_par->numiter)%solver_par->verbose==0 ) {
                solver_par->res_vec[(solver_par->numiter)/solver_par->verbose] 
                        = (real_Double_t) betanom;
                solver_par->timing[(solver_par->numiter)/solver_par->verbose] 
                        = (real_Double_t) tempo2-tempo1;
            }
        }
        solver_par->info = -1;
    }
    // free pinned memory
    magma_free_pinned( H );
    magma_free_pinned( y );
    magma_free_pinned( HH );
    magma_free_pinned( h1 );
    // free GPU memory
    magma_free(dy); 
    if (dH != NULL ) magma_free(dH); 
    magma_s_vfree(&r);
    magma_s_vfree(&q);

    // free GPU streams and events
    magma_event_destroy( event[0] );
    magmablasSetKernelStream(NULL);

    return MAGMA_SUCCESS;
}   /* magma_sgmres */

