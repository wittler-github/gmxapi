/*
 * $Id$
 * 
 *                This source code is part of
 * 
 *                 G   R   O   M   A   C   S
 * 
 *          GROningen MAchine for Chemical Simulations
 * 
 *                        VERSION 3.0
 * 
 * Copyright (c) 1991-2001
 * BIOSON Research Institute, Dept. of Biophysical Chemistry
 * University of Groningen, The Netherlands
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * If you want to redistribute modifications, please consider that
 * scientific software is very special. Version control is crucial -
 * bugs must be traceable. We will be happy to consider code for
 * inclusion in the official distribution, but derived work must not
 * be called official GROMACS. Details are found in the README & COPYING
 * files - if they are missing, get the official version at www.gromacs.org.
 * 
 * To help us fund GROMACS development, we humbly ask that you cite
 * the papers on the package - you can find them in the top README file.
 * 
 * Do check out http://www.gromacs.org , or mail us at gromacs@gromacs.org .
 * 
 * And Hey:
 * S  C  A  M  O  R  G
 */
 
static char *SRCID_fnbf_c = "$Id$";
#ifdef USE_THREADS  
#include <pthread.h>  /* must come first! */
#endif

#include <stdio.h>
#include "typedefs.h"
#include "txtdump.h"
#include "smalloc.h"
#include "ns.h"
#include "vec.h"
#include "maths.h"
#include "macros.h"
#include "force.h"
#include "names.h"
#include "main.h"
#include "xvgr.h"
#include "fatal.h"
#include "physics.h"
#include "force.h"
#include "inner.h"
#include "nrnb.h"
#include "smalloc.h"
#include "detectcpu.h"

#if (defined VECTORIZE_INVSQRT || defined VECTORIZE_INVSQRT_S || defined VECTORIZE_INVSQRT_W || defined VECTORIZE_INVSQRT_WW || defined VECTORIZE_RECIP)
#define USE_LOCAL_BUFFERS
#endif

#ifdef USE_VECTOR
static real *fbuf=NULL;
#  define FBUF_ARG  ,fbuf
#else
#  define FBUF_ARG
#endif

/* Fortran function macros are defined in inner.h */

/* These argument definitions are NOT very pretty...
 * Ideally, the calling sequence should change automatically,
 * i.e. be generated by mkinl. Oh well, later... /EL 000826
 */

#ifdef USE_THREADS
#  define THREAD_ARGS ,SCAL(cr->threadid), SCAL(cr->nthreads), \
                     &(nlist->count),nlist->mtx
#  define ASM_THREAD_ARGS ,cr->threadid, cr->nthreads, \
                           &(nlist->count),nlist->mtx
#else
#  define THREAD_ARGS
#  define ASM_THREAD_ARGS
#endif

#define COMMON_ARGS SCAL(nlist->nri),nlist->iinr,nlist->jindex, \
                    nlist->jjnr, nlist->shift,fr->shift_vec[0], \
                    fshift,nlist->gid ,x[0],f[0] \
                    FBUF_ARG THREAD_ARGS
#define ASM_COMMON_ARGS nlist->nri,nlist->iinr,nlist->jindex, \
                        nlist->jjnr, nlist->shift,fr->shift_vec[0], \
                        fshift,nlist->gid ,x[0],f[0] \
                        FBUF_ARG ASM_THREAD_ARGS

#ifdef VECTORIZE_RECIP
#  ifdef USE_VECTOR
#    define REC_BUF ,drbuf,buf1,buf2   
#  else /* no USE_VECTOR */
#    define REC_BUF ,drbuf,buf1
#  endif
#else  /* no VECTORIZE_RECIP */
#  define REC_BUF
#endif


#ifdef USE_VECTOR
#  define INVSQRT_BUF_TEMPL1 ,drbuf,buf1,buf2
#  define INVSQRT_BUF_TEMPL2
#else /* no USE_VECTOR */
#  define INVSQRT_BUF_TEMPL1 ,drbuf,buf1
#  define INVSQRT_BUF_TEMPL2 ,buf2
#endif


#ifdef VECTORIZE_INVSQRT
#  define INVSQRT_BUF1 INVSQRT_BUF_TEMPL1
#  define INVSQRT_BUF2 INVSQRT_BUF_TEMPL2
#else
#  define INVSQRT_BUF1
#  define INVSQRT_BUF2
#endif

#ifdef VECTORIZE_INVSQRT_S
#  define INVSQRT_S_BUF1 INVSQRT_BUF_TEMPL1
#  define INVSQRT_S_BUF2 INVSQRT_BUF_TEMPL2
#else
#  define INVSQRT_S_BUF1
#  define INVSQRT_S_BUF2
#endif

#ifdef VECTORIZE_INVSQRT_W
#  define INVSQRT_W_BUF1 INVSQRT_BUF_TEMPL1
#  define INVSQRT_W_BUF2 INVSQRT_BUF_TEMPL2
#else
#  define INVSQRT_W_BUF1
#  define INVSQRT_W_BUF2
#endif

#ifdef VECTORIZE_INVSQRT_WW
#  define INVSQRT_WW_BUF1 INVSQRT_BUF_TEMPL1
#  define INVSQRT_WW_BUF2 INVSQRT_BUF_TEMPL2
#else
#  define INVSQRT_WW_BUF1
#  define INVSQRT_WW_BUF2
#endif

#define LJ_ARGS         ,mdatoms->typeA,SCAL(fr->ntype),fr->nbfp,egnb
#define COUL_ARGS       ,mdatoms->chargeA,SCAL(fr->epsfac),egcoul
#define SOFTCORE_LJARGS ,mdatoms->typeA,SCAL(fr->ntype),fr->nbfp
#define RF_ARGS         ,SCAL(fr->k_rf),SCAL(fr->c_rf)
#define LJCTAB_ARGS     ,SCAL(fr->tabscale),fr->coulvdwtab
#define LJTAB_ARGS      ,SCAL(fr->tabscale),fr->vdwtab
#define COULTAB_ARGS    ,SCAL(fr->tabscale),fr->coultab
#define BHTAB_ARGS      ,SCAL(fr->tabscale_exp)
#define FREE_ARGS       ,SCAL(lambda),dvdlambda
#define FREE_CHARGEB    ,mdatoms->chargeB
#define FREE_TYPEB      ,mdatoms->typeB
#define SOFTCORE_ARGS   ,SCAL(fr->sc_alpha),SCAL(fr->sc_sigma6)
#define SOLMN_ARGS      ,nlist->nsatoms

#define ASM_LJ_ARGS      ,mdatoms->typeA,fr->ntype,fr->nbfp,egnb
#define ASM_COUL_ARGS    ,mdatoms->chargeA,fr->epsfac,egcoul
#define ASM_RF_ARGS      ,fr->k_rf,fr->c_rf
#define ASM_LJCTAB_ARGS  ,fr->tabscale,fr->coulvdwtab
#define ASM_LJTAB_ARGS   ,fr->tabscale,fr->vdwtab
#define ASM_COULTAB_ARGS ,fr->tabscale,fr->coultab


int cpu_capabilities = UNKNOWN_CPU;

void do_fnbf(FILE *log,t_commrec *cr,t_forcerec *fr,
	     rvec x[],rvec f[],t_mdatoms *mdatoms,
	     real egnb[],real egcoul[],rvec box_size,
	     t_nrnb *nrnb,real lambda,real *dvdlambda,
	     bool bLR,int eNL)
{
  t_nblist *nlist;
  real     *fshift,nav;
  int      i,i0,i1,nrnb_ind,sz;
  bool     bWater;
  FILE *fp;
#ifdef USE_LOCAL_BUFFERS
static int buflen=0;
static real *drbuf=NULL;
static real *buf1=NULL;
static real *buf2=NULL;
static real *_buf1=NULL;
static real *_buf2=NULL;
#endif

#ifdef USE_VECTOR
  if (fbuf == NULL)
    snew(fbuf,mdatoms->nr*3);
#endif
 

#ifdef USE_LOCAL_BUFFERS
  if (buflen==0) {
    buflen=VECTORIZATION_BUFLENGTH;
    snew(drbuf,3*buflen);
    snew(_buf1,buflen+31);
    snew(_buf2,buflen+31);
    /* use cache aligned buffer pointers */
    buf1=(real *) ( ( (unsigned long int)_buf1 + 31 ) & (~0x1f) );	 
    buf2=(real *) ( ( (unsigned long int)_buf2 + 31 ) & (~0x1f) );	 
    fprintf(log,"Using buffers of length %d for innerloop vectorization.\n",buflen);
  }
#endif

#if (defined USE_X86_ASM || defined USE_PPC_ALTIVEC)
  if(cpu_capabilities==UNKNOWN_CPU) 
    cpu_capabilities=detect_cpu(log);
#endif
  
  if (eNL >= 0) {
    i0 = eNL;
    i1 = i0+1;
  }
  else {
    i0 = 0;
    i1 = eNL_NR;
  }

  if (bLR)
    fshift = fr->fshift_twin[0];
  else
    fshift = fr->fshift[0];

  for(i=i0; (i<i1); i++) {
    if (bLR) 
      nlist  = &(fr->nlist_lr[i]);
    else
      nlist = &(fr->nlist_sr[i]);

    if (nlist->nri > 0) {
      nrnb_ind = nlist->il_code;

#ifdef USE_LOCAL_BUFFERS
      /* make sure buffers can hold the longest neighbourlist */
      if (nlist->solvent==esolWATERWATER)			
	sz = 9*nlist->maxlen;
      else if (nlist->solvent==esolWATER) 
	sz = 3*nlist->maxlen;
      else	
        sz = nlist->maxlen;

      if (sz>buflen) {
	buflen=(sz+100); /* use some extra size to avoid reallocating next step */
    	srenew(drbuf,3*buflen);
    	srenew(_buf1,buflen+31);
    	srenew(_buf2,buflen+31);
        /* make cache aligned buffer pointers */
        buf1=(real *) ( ( (unsigned long int)_buf1 + 31 ) & (~0x1f) );	 
        buf2=(real *) ( ( (unsigned long int)_buf2 + 31 ) & (~0x1f) );	 
      }	
#endif

      switch (nrnb_ind) { 
	case eNR_INL0100:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl0100_sse(ASM_COMMON_ARGS ASM_LJ_ARGS);	
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl0100_3dnow(ASM_COMMON_ARGS ASM_LJ_ARGS);
	  else
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl0100_altivec(ASM_COMMON_ARGS ASM_LJ_ARGS);	
	    else
#endif
	      FUNC(inl0100,INL0100)(COMMON_ARGS REC_BUF LJ_ARGS);
	break;
        case eNR_INL0110:
#if (defined USE_X86_ASM && !defined DOUBLE)
          if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl0110_sse(ASM_COMMON_ARGS ASM_LJ_ARGS SOLMN_ARGS);
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl0110_3dnow(ASM_COMMON_ARGS ASM_LJ_ARGS SOLMN_ARGS);
	  else
#endif
            FUNC(inl0110,INL0110)(COMMON_ARGS REC_BUF LJ_ARGS SOLMN_ARGS);
        break;
	case eNR_INL0200: 
	  FUNC(inl0200,INL0200)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 LJ_ARGS);
	break;
	case eNR_INL0210:
          FUNC(inl0210,INL0210)(COMMON_ARGS INVSQRT_S_BUF1 INVSQRT_S_BUF2 LJ_ARGS SOLMN_ARGS);
        break;
	case eNR_INL0300:
#if (defined USE_X86_ASM && !defined DOUBLE)
          if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl0300_sse(ASM_COMMON_ARGS ASM_LJ_ARGS ASM_LJTAB_ARGS);
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl0300_3dnow(ASM_COMMON_ARGS ASM_LJ_ARGS ASM_LJTAB_ARGS);	
	  else
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl0300_altivec(ASM_COMMON_ARGS ASM_LJ_ARGS ASM_LJTAB_ARGS);
	    else
#endif
	      FUNC(inl0300,INL0300)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 LJ_ARGS LJTAB_ARGS);
	break;
	case eNR_INL0310:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
            inl0310_sse(ASM_COMMON_ARGS ASM_LJ_ARGS ASM_LJTAB_ARGS SOLMN_ARGS);
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
            inl0310_3dnow(ASM_COMMON_ARGS ASM_LJ_ARGS ASM_LJTAB_ARGS SOLMN_ARGS);
	  else 
#endif
            FUNC(inl0310,INL0310)(COMMON_ARGS INVSQRT_S_BUF1 INVSQRT_S_BUF2 LJ_ARGS LJTAB_ARGS SOLMN_ARGS);
	break;
	case eNR_INL0301:
	  FUNC(inl0301,INL0301)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 LJ_ARGS LJTAB_ARGS FREE_ARGS FREE_TYPEB);
	break;
	case eNR_INL0302:
	  FUNC(inl0302,INL0302)(COMMON_ARGS LJ_ARGS LJTAB_ARGS FREE_ARGS FREE_TYPEB SOFTCORE_ARGS);
	break;
	case eNR_INL0400:
	  FUNC(inl0400,INL0400)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 LJ_ARGS LJTAB_ARGS BHTAB_ARGS);
	break;
	case eNR_INL0410:
          FUNC(inl0410,INL0410)(COMMON_ARGS INVSQRT_S_BUF1 INVSQRT_S_BUF2 LJ_ARGS LJTAB_ARGS BHTAB_ARGS SOLMN_ARGS);
	break;
	case eNR_INL0401:
	  FUNC(inl0401,INL0401)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 LJ_ARGS LJTAB_ARGS BHTAB_ARGS FREE_ARGS FREE_TYPEB);
	break;
	case eNR_INL0402:
	  FUNC(inl0402,INL0402)(COMMON_ARGS LJ_ARGS LJTAB_ARGS BHTAB_ARGS FREE_ARGS FREE_TYPEB SOFTCORE_ARGS);
	break;
	case eNR_INL1000:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl1000_sse(ASM_COMMON_ARGS ASM_COUL_ARGS);	
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl1000_3dnow(ASM_COMMON_ARGS ASM_COUL_ARGS);	
	  else
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl1000_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS);	
	    else
#endif
	      FUNC(inl1000,INL1000)(COMMON_ARGS INVSQRT_BUF1 COUL_ARGS);
	break;
	case eNR_INL1010:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl1010_sse(ASM_COMMON_ARGS ASM_COUL_ARGS SOLMN_ARGS);	
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl1010_3dnow(ASM_COMMON_ARGS ASM_COUL_ARGS SOLMN_ARGS);	
	  else 
#endif
	    FUNC(inl1010,INL1010)(COMMON_ARGS INVSQRT_S_BUF1 COUL_ARGS SOLMN_ARGS);
 	break;
	case eNR_INL1020:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl1020_sse(ASM_COMMON_ARGS ASM_COUL_ARGS);
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl1020_3dnow(ASM_COMMON_ARGS ASM_COUL_ARGS);
	  else 
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl1020_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS);	
	    else
#endif
	      FUNC(inl1020,INL1020)(COMMON_ARGS INVSQRT_W_BUF1 COUL_ARGS);
	break;
	case eNR_INL1030:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl1030_sse(ASM_COMMON_ARGS ASM_COUL_ARGS);
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl1030_3dnow(ASM_COMMON_ARGS ASM_COUL_ARGS);
	  else 
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl1030_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS);	
	    else
#endif
	    FUNC(inl1030,INL1030)(COMMON_ARGS INVSQRT_WW_BUF1 COUL_ARGS);
	break;
	case eNR_INL1100:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl1100_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS);
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl1100_3dnow(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS);
	  else 
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl1100_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS);
	    else
#endif
	      FUNC(inl1100,INL1100)(COMMON_ARGS INVSQRT_BUF1 COUL_ARGS LJ_ARGS);
	break;
	case eNR_INL1110:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl1110_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS SOLMN_ARGS);
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl1110_3dnow(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS SOLMN_ARGS);
	  else 
#endif
    	    FUNC(inl1110,INL1110)(COMMON_ARGS INVSQRT_BUF1 COUL_ARGS LJ_ARGS SOLMN_ARGS);
	break;
	case eNR_INL1120:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl1120_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS);
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl1120_3dnow(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS);
	  else 
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl1120_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS);
	    else
#endif
	      FUNC(inl1120,INL1120)(COMMON_ARGS INVSQRT_W_BUF1 COUL_ARGS LJ_ARGS);
	break;
	case eNR_INL1130:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl1130_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS);		
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl1130_3dnow(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS);		
	  else 
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl1130_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS);
	    else
#endif
	      FUNC(inl1130,INL1130)(COMMON_ARGS INVSQRT_WW_BUF1 COUL_ARGS LJ_ARGS);
	break;
	case eNR_INL1200:
	  FUNC(inl1200,INL1200)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 COUL_ARGS LJ_ARGS);
	break;
	case eNR_INL1210:
	  FUNC(inl1210,INL1210)(COMMON_ARGS INVSQRT_S_BUF1 INVSQRT_S_BUF2 COUL_ARGS LJ_ARGS SOLMN_ARGS);
	break;
	case eNR_INL1220:
	  FUNC(inl1220,INL1220)(COMMON_ARGS INVSQRT_W_BUF1 INVSQRT_W_BUF2 COUL_ARGS LJ_ARGS);
	break;
	case eNR_INL1230:
	  FUNC(inl1230,INL1230)(COMMON_ARGS INVSQRT_WW_BUF1 INVSQRT_WW_BUF2 COUL_ARGS LJ_ARGS);
	break;
	case eNR_INL1300:
	  FUNC(inl1300,INL1300)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 COUL_ARGS LJ_ARGS LJTAB_ARGS);
	break;
	case eNR_INL1310:
	  FUNC(inl1310,INL1310)(COMMON_ARGS INVSQRT_S_BUF1 INVSQRT_S_BUF2 COUL_ARGS LJ_ARGS LJTAB_ARGS SOLMN_ARGS);
	break;
	case eNR_INL1320:
	  FUNC(inl1320,INL1320)(COMMON_ARGS INVSQRT_W_BUF1 INVSQRT_W_BUF2 COUL_ARGS LJ_ARGS LJTAB_ARGS);
	break;
	case eNR_INL1330:
	  FUNC(inl1330,INL1330)(COMMON_ARGS INVSQRT_WW_BUF1 INVSQRT_WW_BUF2 COUL_ARGS LJ_ARGS LJTAB_ARGS);
	break;
	case eNR_INL1400:
	  FUNC(inl1400,INL1400)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 COUL_ARGS LJ_ARGS LJTAB_ARGS BHTAB_ARGS);
	break;
	case eNR_INL1410:
	  FUNC(inl1410,INL1410)(COMMON_ARGS INVSQRT_S_BUF1 INVSQRT_S_BUF2 COUL_ARGS LJ_ARGS LJTAB_ARGS BHTAB_ARGS SOLMN_ARGS);
	break;
	case eNR_INL1420:
	  FUNC(inl1420,INL1420)(COMMON_ARGS INVSQRT_W_BUF1 INVSQRT_W_BUF2 COUL_ARGS LJ_ARGS LJTAB_ARGS BHTAB_ARGS);
	break;
	case eNR_INL1430:
	  FUNC(inl1430,INL1430)(COMMON_ARGS INVSQRT_WW_BUF1 INVSQRT_WW_BUF2 COUL_ARGS LJ_ARGS LJTAB_ARGS BHTAB_ARGS);
	break;
	case eNR_INL2000:
#if (defined USE_X86_ASM && !defined DOUBLE)
          if(cpu_capabilities & X86_SSE_SUPPORT)	
	    inl2000_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_RF_ARGS);	
	  else	
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl2000_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_RF_ARGS);	
	    else
#endif
	      FUNC(inl2000,INL2000)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 COUL_ARGS RF_ARGS);
	break;
	case eNR_INL2010:
	  FUNC(inl2010,INL2010)(COMMON_ARGS INVSQRT_S_BUF1 INVSQRT_S_BUF2 COUL_ARGS RF_ARGS SOLMN_ARGS);
	break;
	case eNR_INL2020:
#if (defined USE_X86_ASM && !defined DOUBLE)
          if(cpu_capabilities & X86_SSE_SUPPORT)	
	    inl2020_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_RF_ARGS);	
	  else	
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl2020_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_RF_ARGS);	
	    else
#endif
	    FUNC(inl2020,INL2020)(COMMON_ARGS INVSQRT_W_BUF1 INVSQRT_W_BUF2 COUL_ARGS RF_ARGS);
	break;
	case eNR_INL2030:
#if (defined USE_X86_ASM && !defined DOUBLE)
          if(cpu_capabilities & X86_SSE_SUPPORT)	
	    inl2030_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_RF_ARGS);	
	  else	
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl2030_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_RF_ARGS);	
	    else
#endif
	    FUNC(inl2030,INL2030)(COMMON_ARGS INVSQRT_WW_BUF1 INVSQRT_WW_BUF2 COUL_ARGS RF_ARGS);
	break;
	case eNR_INL2100:
#if (defined USE_X86_ASM && !defined DOUBLE)
          if(cpu_capabilities & X86_SSE_SUPPORT)	
	    inl2100_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_RF_ARGS ASM_LJ_ARGS);	
	  else	
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl2100_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_RF_ARGS ASM_LJ_ARGS);	
	    else
#endif
	      FUNC(inl2100,INL2100)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 COUL_ARGS RF_ARGS LJ_ARGS);
	break;
	case eNR_INL2110:
	  FUNC(inl2110,INL2110)(COMMON_ARGS INVSQRT_S_BUF1 INVSQRT_S_BUF2 COUL_ARGS RF_ARGS LJ_ARGS SOLMN_ARGS);
	break;
	case eNR_INL2120:
#if (defined USE_X86_ASM && !defined DOUBLE)
          if(cpu_capabilities & X86_SSE_SUPPORT)	
	    inl2120_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_RF_ARGS ASM_LJ_ARGS);	
	  else	
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl2120_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_RF_ARGS ASM_LJ_ARGS);	
	    else
#endif
	      FUNC(inl2120,INL2120)(COMMON_ARGS INVSQRT_W_BUF1 INVSQRT_W_BUF2 COUL_ARGS RF_ARGS LJ_ARGS);
	break;
	case eNR_INL2130:
#if (defined USE_X86_ASM && !defined DOUBLE)
          if(cpu_capabilities & X86_SSE_SUPPORT)	
	    inl2130_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_RF_ARGS ASM_LJ_ARGS);	
	  else	
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl2130_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_RF_ARGS ASM_LJ_ARGS);	
	    else
#endif
	      FUNC(inl2130,INL2130)(COMMON_ARGS INVSQRT_WW_BUF1 INVSQRT_WW_BUF2 COUL_ARGS RF_ARGS LJ_ARGS);
	break;
	case eNR_INL2200:
	  FUNC(inl2200,INL2200)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 COUL_ARGS RF_ARGS LJ_ARGS);
	break;
	case eNR_INL2210:
	  FUNC(inl2210,INL2210)(COMMON_ARGS INVSQRT_S_BUF1 INVSQRT_S_BUF2 COUL_ARGS RF_ARGS LJ_ARGS SOLMN_ARGS);
	break;
	case eNR_INL2220:
	  FUNC(inl2220,INL2220)(COMMON_ARGS INVSQRT_W_BUF1 INVSQRT_W_BUF2 COUL_ARGS RF_ARGS LJ_ARGS);
	break;
	case eNR_INL2230:
	  FUNC(inl2230,INL2230)(COMMON_ARGS INVSQRT_WW_BUF1 INVSQRT_WW_BUF2 COUL_ARGS RF_ARGS LJ_ARGS);
	break;
	case eNR_INL2300:
	  FUNC(inl2300,INL2300)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 COUL_ARGS RF_ARGS LJ_ARGS LJTAB_ARGS);
	break;
	case eNR_INL2310:
	  FUNC(inl2310,INL2310)(COMMON_ARGS INVSQRT_S_BUF1 INVSQRT_S_BUF2 COUL_ARGS RF_ARGS LJ_ARGS LJTAB_ARGS SOLMN_ARGS);
	break;
	case eNR_INL2320:
	  FUNC(inl2320,INL2320)(COMMON_ARGS INVSQRT_W_BUF1 INVSQRT_W_BUF2 COUL_ARGS RF_ARGS LJ_ARGS LJTAB_ARGS);
	break;
	case eNR_INL2330:
	  FUNC(inl2330,INL2330)(COMMON_ARGS INVSQRT_WW_BUF1 INVSQRT_WW_BUF2 COUL_ARGS RF_ARGS LJ_ARGS LJTAB_ARGS);
	break;
	case eNR_INL2400:
	  FUNC(inl2400,INL2400)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 COUL_ARGS RF_ARGS LJ_ARGS LJTAB_ARGS BHTAB_ARGS);
	break;
	case eNR_INL2410:
	  FUNC(inl2410,INL2410)(COMMON_ARGS INVSQRT_S_BUF1 INVSQRT_S_BUF2 COUL_ARGS RF_ARGS LJ_ARGS LJTAB_ARGS BHTAB_ARGS SOLMN_ARGS);
	break;
	case eNR_INL2420:
	  FUNC(inl2420,INL2420)(COMMON_ARGS INVSQRT_W_BUF1 INVSQRT_W_BUF2 COUL_ARGS RF_ARGS LJ_ARGS LJTAB_ARGS BHTAB_ARGS);
	break;
	case eNR_INL2430:
	  FUNC(inl2430,INL2430)(COMMON_ARGS INVSQRT_WW_BUF1 INVSQRT_WW_BUF2 COUL_ARGS RF_ARGS LJ_ARGS LJTAB_ARGS BHTAB_ARGS);
	break;
	case eNR_INL3000:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl3000_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_COULTAB_ARGS);
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl3000_3dnow(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_COULTAB_ARGS);
	  else 
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl3000_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_COULTAB_ARGS);
	    else
#endif
	      FUNC(inl3000,INL3000)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 COUL_ARGS COULTAB_ARGS);
	break;
	case eNR_INL3001:
	  FUNC(inl3001,INL3001)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 COUL_ARGS COULTAB_ARGS FREE_ARGS FREE_CHARGEB);
	break;
	case eNR_INL3002:
	  FUNC(inl3002,INL3002)(COMMON_ARGS COUL_ARGS SOFTCORE_LJARGS COULTAB_ARGS FREE_ARGS FREE_CHARGEB FREE_TYPEB SOFTCORE_ARGS);
	break;
	case eNR_INL3010:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl3010_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_COULTAB_ARGS SOLMN_ARGS);		
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl3010_3dnow(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_COULTAB_ARGS SOLMN_ARGS);		
	  else 
#endif
    	    FUNC(inl3010,INL3010)(COMMON_ARGS INVSQRT_S_BUF1 INVSQRT_S_BUF2 COUL_ARGS COULTAB_ARGS SOLMN_ARGS);
	break;
	case eNR_INL3020:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl3020_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_COULTAB_ARGS);		
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl3020_3dnow(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_COULTAB_ARGS);		
	  else 
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl3020_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_COULTAB_ARGS);
	    else
#endif
	      FUNC(inl3020,INL3020)(COMMON_ARGS INVSQRT_W_BUF1 INVSQRT_W_BUF2 COUL_ARGS COULTAB_ARGS);
	break;
	case eNR_INL3030:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl3030_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_COULTAB_ARGS);		
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl3030_3dnow(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_COULTAB_ARGS);		
	  else 
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl3030_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_COULTAB_ARGS);
	    else
#endif
	      FUNC(inl3030,INL3030)(COMMON_ARGS INVSQRT_WW_BUF1 INVSQRT_WW_BUF2 COUL_ARGS COULTAB_ARGS);
	break;
	case eNR_INL3100:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT) 
	    inl3100_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_COULTAB_ARGS);
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl3100_3dnow(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_COULTAB_ARGS);
	  else 
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl3100_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_COULTAB_ARGS);
	    else
#endif
	      FUNC(inl3100,INL3100)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 COUL_ARGS LJ_ARGS COULTAB_ARGS);
	break;
	case eNR_INL3110:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl3110_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_COULTAB_ARGS SOLMN_ARGS);		
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl3110_3dnow(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_COULTAB_ARGS SOLMN_ARGS);		
	  else 
#endif
    	    FUNC(inl3110,INL3110)(COMMON_ARGS INVSQRT_S_BUF1 INVSQRT_S_BUF2 COUL_ARGS LJ_ARGS COULTAB_ARGS SOLMN_ARGS);
	break;
	case eNR_INL3120:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl3120_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_COULTAB_ARGS);		
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl3120_3dnow(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_COULTAB_ARGS);		
	  else 
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl3120_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_COULTAB_ARGS);
	    else
#endif
	      FUNC(inl3120,INL3120)(COMMON_ARGS INVSQRT_W_BUF1 INVSQRT_W_BUF2 COUL_ARGS LJ_ARGS COULTAB_ARGS);
	break;
	case eNR_INL3130:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl3130_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_COULTAB_ARGS);	
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl3130_3dnow(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_COULTAB_ARGS);	
	  else 
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl3130_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_COULTAB_ARGS);
	    else
#endif
	      FUNC(inl3130,INL3130)(COMMON_ARGS INVSQRT_WW_BUF1 INVSQRT_WW_BUF2 COUL_ARGS LJ_ARGS COULTAB_ARGS);
	break;
	case eNR_INL3200:
	  FUNC(inl3200,INL3200)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 COUL_ARGS LJ_ARGS COULTAB_ARGS);
	break;
	case eNR_INL3210:
	  FUNC(inl3210,INL3210)(COMMON_ARGS INVSQRT_S_BUF1 INVSQRT_S_BUF2 COUL_ARGS LJ_ARGS COULTAB_ARGS SOLMN_ARGS);
	break;
	case eNR_INL3220:
	  FUNC(inl3220,INL3220)(COMMON_ARGS INVSQRT_W_BUF1 INVSQRT_W_BUF2 COUL_ARGS LJ_ARGS COULTAB_ARGS);
	break;
	case eNR_INL3230:
	  FUNC(inl3230,INL3230)(COMMON_ARGS INVSQRT_WW_BUF1 INVSQRT_WW_BUF2 COUL_ARGS LJ_ARGS COULTAB_ARGS);
	break;
	case eNR_INL3300:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl3300_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_LJCTAB_ARGS);
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl3300_3dnow(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_LJCTAB_ARGS);
	  else 
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl3300_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_LJCTAB_ARGS);
	    else
#endif
	      FUNC(inl3300,INL3300)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 COUL_ARGS LJ_ARGS LJCTAB_ARGS);
	break;
	case eNR_INL3301:
	  FUNC(inl3301,INL3301)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 COUL_ARGS LJ_ARGS LJCTAB_ARGS FREE_ARGS FREE_CHARGEB FREE_TYPEB);
	break;
	case eNR_INL3302:
	  FUNC(inl3302,INL3302)(COMMON_ARGS COUL_ARGS LJ_ARGS LJCTAB_ARGS FREE_ARGS FREE_CHARGEB FREE_TYPEB SOFTCORE_ARGS);
	break;
	case eNR_INL3310:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl3310_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_LJCTAB_ARGS SOLMN_ARGS);		
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl3310_3dnow(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_LJCTAB_ARGS SOLMN_ARGS);		
	  else 
#endif
    	    FUNC(inl3310,INL3310)(COMMON_ARGS INVSQRT_S_BUF1 INVSQRT_S_BUF2 COUL_ARGS LJ_ARGS LJCTAB_ARGS SOLMN_ARGS);
	break;
	case eNR_INL3320:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl3320_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_LJCTAB_ARGS);
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl3320_3dnow(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_LJCTAB_ARGS);
	  else 
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl3320_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_LJCTAB_ARGS);
	    else
#endif
	      FUNC(inl3320,INL3320)(COMMON_ARGS INVSQRT_W_BUF1 INVSQRT_W_BUF2 COUL_ARGS LJ_ARGS LJCTAB_ARGS);
	break;
	case eNR_INL3330:
#if (defined USE_X86_ASM && !defined DOUBLE)
	  if(cpu_capabilities & X86_SSE_SUPPORT)
	    inl3330_sse(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_LJCTAB_ARGS);		
	  else if(cpu_capabilities & X86_3DNOW_SUPPORT)
	    inl3330_3dnow(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_LJCTAB_ARGS);		
	  else 
#elif defined USE_PPC_ALTIVEC
	    if(cpu_capabilities & PPC_ALTIVEC_SUPPORT)
	      inl3330_altivec(ASM_COMMON_ARGS ASM_COUL_ARGS ASM_LJ_ARGS ASM_LJCTAB_ARGS);
	    else
#endif
	      FUNC(inl3330,INL3330)(COMMON_ARGS INVSQRT_WW_BUF1 INVSQRT_WW_BUF2 COUL_ARGS LJ_ARGS LJCTAB_ARGS);
	break;
	case eNR_INL3400:
	  FUNC(inl3400,INL3400)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 COUL_ARGS LJ_ARGS LJCTAB_ARGS BHTAB_ARGS);
	break;
	case eNR_INL3401:
	  FUNC(inl3401,INL3401)(COMMON_ARGS INVSQRT_BUF1 INVSQRT_BUF2 COUL_ARGS LJ_ARGS LJCTAB_ARGS BHTAB_ARGS FREE_ARGS FREE_CHARGEB FREE_TYPEB);
	break;
	case eNR_INL3402:
	  FUNC(inl3402,INL3402)(COMMON_ARGS COUL_ARGS LJ_ARGS LJCTAB_ARGS BHTAB_ARGS FREE_ARGS FREE_CHARGEB FREE_TYPEB SOFTCORE_ARGS);
	break;
	case eNR_INL3410:
	  FUNC(inl3410,INL3410)(COMMON_ARGS INVSQRT_S_BUF1 INVSQRT_S_BUF2 COUL_ARGS LJ_ARGS LJCTAB_ARGS BHTAB_ARGS SOLMN_ARGS);
	break;
	case eNR_INL3420:
	  FUNC(inl3420,INL3420)(COMMON_ARGS INVSQRT_W_BUF1 INVSQRT_W_BUF2 COUL_ARGS LJ_ARGS LJCTAB_ARGS BHTAB_ARGS);
	break;
	case eNR_INL3430:
	  FUNC(inl3430,INL3430)(COMMON_ARGS INVSQRT_WW_BUF1 INVSQRT_WW_BUF2 COUL_ARGS LJ_ARGS LJCTAB_ARGS BHTAB_ARGS);
	break;
	case eNR_INLNONE:
	fatal_error(0,"nrnb_ind is \"NONE\", bad selection made in ns.c");
      default:
	fatal_error(0,"No function corresponding to %s in %s `line' %d",
		    nrnb_str(nrnb_ind),__FILE__,__LINE__);
      }
      /* Mega flops accounting */
      if (nlist->solvent==esolWATER) 
        inc_nrnb(nrnb,eNR_INL_IATOM,3*nlist->nri);
      else if (nlist->solvent==esolWATERWATER)
        inc_nrnb(nrnb,eNR_INL_IATOM,9*nlist->nri);
      else if (nlist->solvent!=esolMNO)
       inc_nrnb(nrnb,eNR_INL_IATOM,nlist->nri);


      if (nlist->solvent==esolMNO) {
	switch(nrnb_ind) {
	case eNR_INL1110:
	case eNR_INL1210:
	case eNR_INL1310:
	case eNR_INL1410:
	case eNR_INL2110:
	case eNR_INL2210:
	case eNR_INL2310:
	case eNR_INL2410:
	case eNR_INL3110:
	case eNR_INL3210:
	case eNR_INL3310:
	case eNR_INL3410:
	  /* vdwc loops */
	  nav = fr->nMNOav[0];
	  inc_nrnb(nrnb,eNR_INL_IATOM,nav*nlist->nri);
	  inc_nrnb(nrnb,nrnb_ind,nav*nlist->nrj);
          break;
	case eNR_INL1010:
	case eNR_INL2010:
	case eNR_INL3010:
	  /* coul loops */
	  nav = fr->nMNOav[0];
	  inc_nrnb(nrnb,eNR_INL_IATOM,nav*nlist->nri);
	  inc_nrnb(nrnb,nrnb_ind,nav*nlist->nrj);
          break;
	case eNR_INL0110:
	case eNR_INL0210:
	case eNR_INL0310:
	case eNR_INL0410:
	  /* vdw loops */
	  nav = fr->nMNOav[0];
	  inc_nrnb(nrnb,eNR_INL_IATOM,nav*nlist->nri);
	  inc_nrnb(nrnb,nrnb_ind,nav*nlist->nrj);
          break;
	default:
	  fatal_error(0,"MFlops accounting wrong in %s, %d, nrnb_ind = %d",
		      __FILE__,__LINE__,nrnb_ind);
	}	
      } 
      else 
        inc_nrnb(nrnb,nrnb_ind,nlist->nrj);	
    }
  }
}

static real dist2(rvec x,rvec y)
{
  rvec dx;
  
  rvec_sub(x,y,dx);
  
  return iprod(dx,dx);
}

static real *mk_14parm(int ntype,int nbonds,t_iatom iatoms[],
		       t_iparams *iparams,int type[])
{
  /* This routine fills a matrix with interaction parameters for
   * 1-4 interaction. It is assumed that these are atomtype dependent
   * only... (but this is checked for...)
   */
  real *nbfp,c6sav,c12sav;
  int  i,ip,ti,tj;
  
  snew(nbfp,2*ntype*ntype);
  for(i=0; (i<nbonds); i+= 3) {
    ip = iatoms[i];
    ti = type[iatoms[i+1]];
    tj = type[iatoms[i+2]];
    c6sav  = C6(nbfp,ntype,ti,tj);
    c12sav = C12(nbfp,ntype,ti,tj);
    C6(nbfp,ntype,ti,tj)  = iparams[ip].lj14.c6A;
    C12(nbfp,ntype,ti,tj) = iparams[ip].lj14.c12A;
    if ((c6sav != 0) || (c12sav != 0)) {
      if ((c6sav  !=  C6(nbfp,ntype,ti,tj)) || 
	  (c12sav != C12(nbfp,ntype,ti,tj))) {
	fatal_error(0,"Force field inconsistency: 1-4 interaction parameters "
		    "for atoms %d-%d not the same as for other atoms "
		    "with the same atom type",iatoms[i+1],iatoms[i+2]);
      }
    }
  }
	


  return nbfp;
}



real do_14(int nbonds,t_iatom iatoms[],t_iparams *iparams,
	   rvec x[],rvec f[],t_forcerec *fr,t_graph *g,
	   matrix box,real lambda,real *dvdlambda,
	   t_mdatoms *md,int ngrp,real egnb[],real egcoul[])
{
  static    real *nbfp14=NULL;
  static    bool bWarn=FALSE;
  real      eps;
  real      r2,rtab2;
  rvec      fi,fj;
  int       ai,aj,itype;
  t_iatom   *ia0,*iatom;
  int       gid,shift14;
  int       j_index[] = { 0, 1 };
  int       i1=1,i3=3,si,i0;
  ivec      dt;
#ifdef USE_VECTOR
  if (fbuf == NULL)
    snew(fbuf,md->nr*3);
#endif  

/* We don't do SSE or altivec here, due to large overhead for 4-fold unrolling on short lists */
#if (defined USE_X86_ASM && !defined DOUBLE)
 if(cpu_capabilities==UNKNOWN_CPU) 
	cpu_capabilities=check_x86cpu(NULL);
#endif

  if (nbfp14 == NULL) {
    nbfp14 = mk_14parm(fr->ntype,nbonds,iatoms,iparams,md->typeA);
    if (debug)
      pr_rvec(debug,0,"nbfp14",nbfp14,sqr(fr->ntype));
  }
  shift14 = CENTRAL;
  
  /* Reaction field stuff */  
  eps    = fr->epsfac*fr->fudgeQQ;
  
  rtab2 = sqr(fr->rtab);
    
  ia0=iatoms;

  for(iatom=ia0; (iatom<ia0+nbonds); iatom+=3) {
    itype = iatom[0];
    ai    = iatom[1];
    aj    = iatom[2];
   
    r2    = distance2(x[ai],x[aj]);
    copy_rvec(f[ai],fi);
    copy_rvec(f[aj],fj);

    
/* We do not check if the neighbourlists fit in the buffer here, since I cant imagine
 * a particle having so many 1-4 interactions :-) /EL 
 */
    
    if (r2 >= rtab2) {
      if (!bWarn) {
        fprintf(stderr,"Warning: 1-4 interaction at distance larger than %g\n",
	        rtab2);
	fprintf(stderr,"These are ignored for the rest of the simulation\n"
	        "turn on -debug for more information\n");
	bWarn = TRUE;
      }
      if (debug) 
        fprintf(debug,"%8f %8f %8f\n%8f %8f %8f\n1-4 (%d,%d) interaction not within cut-off! r=%g. Ignored",
	      	x[ai][XX],x[ai][YY],x[ai][ZZ],
		x[aj][XX],x[aj][YY],x[aj][ZZ],
		(int)ai+1,(int)aj+1,sqrt(r2));
    }
    else {
      gid  = GID(md->cENER[ai],md->cENER[aj],ngrp);
#ifdef DEBUG
      fprintf(debug,"LJ14: grp-i=%2d, grp-j=%2d, ngrp=%2d, GID=%d\n",
	      md->cENER[ai],md->cENER[aj],ngrp,gid);
#endif
      
      if (md->bPerturbed[ai] || md->bPerturbed[aj]) {
	int  tiA,tiB,tjA,tjB;
	real nbfp[18];
	
	/* Save old types */
	tiA = md->typeA[ai];
	tiB = md->typeB[ai];
	tjA = md->typeA[aj];
	tjB = md->typeB[aj];
	md->typeA[ai] = 0;
	md->typeB[ai] = 1;
	md->typeA[aj] = 2;
	md->typeB[aj] = 3;
	
	/* Set nonbonded params */
	C6(nbfp,4,0,2)  = iparams[itype].lj14.c6A;
	C6(nbfp,4,1,2)  = iparams[itype].lj14.c6B;
	C12(nbfp,4,0,2) = iparams[itype].lj14.c12A;
	C12(nbfp,4,1,2) = iparams[itype].lj14.c12B;
	
#undef COMMON_ARGS
#define COMMON_ARGS SCAL(i1),&ai,j_index,&aj,&shift14,fr->shift_vec[0],fr->fshift[0],&gid ,x[0],f[0]
	
	if(fr->sc_alpha>0)
#if (defined VECTORIZE_INVSQRT || defined VECTORIZE_INVSQRT_S || defined VECTORIZE_INVSQRT_W || defined VECTORIZE_INVSQRT_WW || defined USE_THREADS)
	  FUNC(inl3302n,INL3302N)(COMMON_ARGS FBUF_ARG /* special version without some optimizations */
#else
	  FUNC(inl3302,INL3302)(COMMON_ARGS FBUF_ARG /* use normal innerloop */
#endif
				,md->chargeA,SCAL(eps),egcoul
				,md->typeA,SCAL(i3),nbfp,egnb,
				SCAL(fr->tabscale),fr->coulvdw14tab,
				SCAL(lambda),dvdlambda,md->chargeB,md->typeB,SCAL(fr->sc_alpha),SCAL(fr->sc_sigma6));
	else
#if (defined VECTORIZE_INVSQRT || defined VECTORIZE_INVSQRT_S || defined VECTORIZE_INVSQRT_W || defined VECTORIZE_INVSQRT_WW || defined USE_THREADS)
	FUNC(inl3301n,INL3301N)(COMMON_ARGS FBUF_ARG /* special version without some optimizations */
#else	
	FUNC(inl3301,INL3301)(COMMON_ARGS FBUF_ARG /* use normal innerloop */
#endif
			      ,md->chargeA,SCAL(eps),egcoul
			      ,md->typeA,SCAL(i3),nbfp,egnb,
			      SCAL(fr->tabscale),fr->coulvdw14tab,
			      SCAL(lambda),dvdlambda,md->chargeB,md->typeB);
		 /* Restore old types */
	md->typeA[ai] = tiA;
	md->typeB[ai] = tiB;
	md->typeA[aj] = tjA;
	md->typeB[aj] = tjB;
	}
    else 
#if (defined VECTORIZE_INVSQRT || defined VECTORIZE_INVSQRT_S || defined VECTORIZE_INVSQRT_W || defined VECTORIZE_INVSQRT_WW || defined USE_THREADS)
	FUNC(inl3300n,INL3300N)(COMMON_ARGS FBUF_ARG /* special version without some optimizations */
#else	
#if (defined USE_X86_ASM && !defined DOUBLE)
				if(cpu_capabilities & X86_3DNOW_SUPPORT)
	inl3300_3dnow(i1,&ai,j_index,&aj,&shift14,fr->shift_vec[0],fr->fshift[0],
		      &gid ,x[0],f[0] FBUF_ARG
		      ,md->chargeA,eps,egcoul,md->typeA,fr->ntype,
		     nbfp14,egnb,fr->tabscale,fr->coulvdw14tab);
        else
#endif /* 3dnow */
 	  FUNC(inl3300,INL3300)(COMMON_ARGS FBUF_ARG  /* use normal innerloop */
#endif
                     ,md->chargeA,SCAL(eps),egcoul,md->typeA,SCAL(fr->ntype),
		     nbfp14,egnb,SCAL(fr->tabscale),fr->coulvdw14tab);

    /* Now determine the 1-4 force in order to add it to the fshift array 
     * Actually we are first computing minus the force.
     */

    rvec_sub(f[ai],fi,fi);
    /*rvec_sub(f[aj],fj,fj);  */

    ivec_sub(SHIFT_IVEC(g,ai),SHIFT_IVEC(g,aj),dt);    
    si=IVEC2IS(dt);	

    rvec_inc(fr->fshift[si],fi);
    rvec_dec(fr->fshift[CENTRAL],fi);
  }	
  }
  return 0.0;
}

