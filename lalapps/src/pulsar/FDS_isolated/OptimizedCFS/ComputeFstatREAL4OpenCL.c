/*
 * Copyright (C) 2009 Reinhard Prix
 * Copyright (C) 2007 Chris Messenger
 * Copyright (C) 2006 John T. Whelan, Badri Krishnan
 * Copyright (C) 2005, 2006, 2007 Reinhard Prix
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with with program; see the file COPYING. If not, write to the
 *  Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 *  MA  02111-1307  USA
 */

/** \author R. Prix, J. T. Whelan
 * \ingroup pulsarCoherent
 * \file
 * \brief
 * Functions to calculate the so-called F-statistic for a given point in parameter-space,
 * following the equations in \ref JKS98.
 *
 * This code is partly a descendant of an earlier implementation found in
 * LALDemod.[ch] by Jolien Creighton, Maria Alessandra Papa, Reinhard Prix, Steve Berukoff, Xavier Siemens, Bruce Allen
 * ComputSky.[ch] by Jolien Creighton, Reinhard Prix, Steve Berukoff
 * LALComputeAM.[ch] by Jolien Creighton, Maria Alessandra Papa, Reinhard Prix, Steve Berukoff, Xavier Siemens
 *
 * NOTE: this file contains specialized versions of the central CFSv2 routines, that are aimed at GPU optimization.
 * At the moment this only means they are internally using only single precision, but still aggree to within 1%
 * for Tobs ~ 1day and fmax ~ 1kHz.
 *
 */

/*---------- INCLUDES ----------*/
#define __USE_ISOC99 1
#include <math.h>

#include <lal/AVFactories.h>
#include <lal/ComputeFstat.h>

#include "ComputeFstatREAL4.h"
#include "../hough/src2/HierarchicalSearch.h"

NRCSID( COMPUTEFSTATC, "$Id$");

/*---------- local DEFINES ----------*/
#define LD_SMALL4       ((REAL4)2.0e-4)		/**< "small" number for REAL4*/

#define TWOPI_FLOAT     6.28318530717958f  	/**< single-precision 2*pi */
#define OOTWOPI_FLOAT   (1.0f / TWOPI_FLOAT)	/**< single-precision 1 / (2pi) */

/*----- Macros ----- */
#define SQ(x) ( (x) * (x) )
#define REM(x) ( (x) - (INT4)(x) )

/*----- SWITCHES -----*/

/*---------- internal types ----------*/

/*---------- Global variables ----------*/
static const REAL4 inv_fact[PULSAR_MAX_SPINS] = { 1.0, 1.0, (1.0/2.0), (1.0/6.0), (1.0/24.0), (1.0/120.0), (1.0/720.0) };

/* global sin-cos lookup table */
#define LUT_RES         	64      /* resolution of lookup-table */
#define OO_LUT_RES		(1.0f / LUT_RES )

static REAL4 sinVal[LUT_RES+1], cosVal[LUT_RES+1];

/* empty initializers  */
static const LALStatus empty_LALStatus;
static const AMCoeffs empty_AMCoeffs;

const SSBtimes empty_SSBtimes;
const MultiSSBtimes empty_MultiSSBtimes;
const AntennaPatternMatrix empty_AntennaPatternMatrix;
const MultiAMCoeffs empty_MultiAMCoeffs;
const Fcomponents empty_Fcomponents;
const ComputeFBuffer empty_ComputeFBuffer;
const PulsarSpinsREAL4 empty_PulsarSpinsREAL4;
const ComputeFBufferREAL4 empty_ComputeFBufferREAL4;
const ComputeFBufferREAL4V empty_ComputeFBufferREAL4V;
const FcomponentsREAL4 empty_FcomponentsREAL4;
const CLWorkspace empty_CLWorkspace;

/*---------- internal prototypes ----------*/
int finite(double x);
void sin_cos_2PI_LUT_REAL4 (REAL4 *sin2pix, REAL4 *cos2pix, REAL4 x);
void sin_cos_LUT_REAL4 (REAL4 *sinx, REAL4 *cosx, REAL4 x);
void init_sin_cos_LUT_REAL4 (void);

#if USE_OPENCL_KERNEL_CPU
#  include "kernel.cl"
#endif
/*==================== FUNCTION DEFINITIONS ====================*/

/** REAL4 and GPU-ready version of ComputeFStatFreqBand(), extended to loop over segments as well.
 *
 * Computes a vector of Fstatistic values for a number of frequency bins, for each segment
 */
int
XLALComputeFStatFreqBandVector (   REAL4FrequencySeriesVector *fstatBandV, 		/**< [out] Vector of Fstat frequency-bands */
                                   const PulsarDopplerParams *doppler,			/**< parameter-space point to compute F for */
                                   const MultiSFTVectorSequence *multiSFTsV, 		/**< normalized (by DOUBLE-sided Sn!) data-SFTs of all IFOs */
                                   const MultiNoiseWeightsSequence *multiWeightsV,	/**< noise-weights of all SFTs */
                                   const MultiDetectorStateSeriesSequence *multiDetStatesV,/**< 'trajectories' of the different IFOs */
                                   UINT4 Dterms,					/**< number of Dirichlet kernel Dterms to use */
                                   ComputeFBufferREAL4V *cfvBuffer,			/**< buffer quantities that don't need to be recomputed */
                                   CLWorkspace *clW                                     /**< OpenCL workspace */
                                   )
{
  static const char *fn = "XLALComputeFStatFreqBandVector()";

  const UINT4 CALL_XLALCoreFstatREAL4 = 0;
#if USE_OPENCL_KERNEL
  cl_int err, err_total;
#endif
  static int call_count = 0;

  UINT4 numBins, k;
  UINT4 numSegments, n;
  REAL8 f0, deltaF;
  REAL4 Fstat;
  REAL8 Freq0, Freq;

  REAL8 constSftsDataDeltaF = multiSFTsV->data[0]->data[0]->data->deltaF;
  REAL8 constSftsDataF0 = multiSFTsV->data[0]->data[0]->data->f0;

  REAL4 constTsft = (REAL4)(1.0 / constSftsDataDeltaF);                       /* length of SFTs in seconds */
  REAL4 constDFreq = (REAL4)constSftsDataDeltaF;
  INT4 constFreqIndex0 = (UINT4)(constSftsDataF0 / constDFreq + 0.5);         /* index of first frequency-bin in SFTs */

  /* increment the function call counter */
  ++call_count;

  /* report which flavour of function is called */
  if (call_count == 1) {
    if (USE_OPENCL_KERNEL) {
      LogPrintf (LOG_DEBUG, "%s: using OpenCL call on GPUs. ", fn);
    } else if (USE_OPENCL_KERNEL_CPU) {
      LogPrintf (LOG_DEBUG, "%s: using OpenCL kernel as a regular function on CPU. ", fn);
    }
    if (CALL_XLALCoreFstatREAL4) {
      LogPrintf (LOG_DEBUG, "calling function XLALCoreFstatREAL4", fn);
    }
    LogPrintf (LOG_DEBUG, "\n");
  }

  /* check input consistency */
  if ( !doppler || !multiSFTsV || !multiWeightsV || !multiDetStatesV || !cfvBuffer ) {
    XLALPrintError ("%s: illegal NULL input pointer.\n", fn );
    XLAL_ERROR ( fn, XLAL_EINVAL );
  }

  if ( !fstatBandV || !fstatBandV->length ) {
    XLALPrintError ("%s: illegal NULL or empty output pointer 'fstatBandV'.\n", fn );
    XLAL_ERROR ( fn, XLAL_EINVAL );
  }

  numSegments = fstatBandV->length;

  if ( (multiSFTsV->length != numSegments) || (multiDetStatesV->length != numSegments ) ) {
    XLALPrintError ("%s: inconsistent number of segments between fstatBandV (%d), multiSFTsV(%d) and multiDetStatesV (%d)\n",
                    fn, numSegments, multiSFTsV->length, multiDetStatesV->length );
    XLAL_ERROR ( fn, XLAL_EINVAL );
  }
  if ( multiWeightsV->length != numSegments ) {
    XLALPrintError ("%s: inconsistent number of segments between fstatBandV (%d) and multiWeightsV (%d)\n",
                    fn, numSegments, multiWeightsV->length );
    XLAL_ERROR ( fn, XLAL_EINVAL );
  }

  numBins = fstatBandV->data[0].data->length;
  f0      = fstatBandV->data[0].f0;
  deltaF  = fstatBandV->data[0].deltaF;

  /* a check that the f0 values from thisPoint and fstatVector are
   * at least close to each other -- this is only meant to catch
   * stupid errors but not subtle ones */
  if ( fabs(f0 - doppler->fkdot[0]) >= deltaF ) {
    XLALPrintError ("%s: fstatVector->f0 = %f differs from doppler->fkdot[0] = %f by more than deltaF = %g\n", fn, f0, doppler->fkdot[0], deltaF );
    XLAL_ERROR ( fn, XLAL_EINVAL );
  }

  /* ---------- prepare REAL4 version of PulsarSpins to be passed into core functions */
  PulsarSpinsREAL4 fkdot4 = empty_PulsarSpinsREAL4;
  UINT4 s;
  Freq = Freq0 = doppler->fkdot[0];
  fkdot4.FreqMain = (INT4)Freq0;
  /* fkdot[0] now only carries the *remainder* of Freq wrt FreqMain */
  fkdot4.fkdot[0] = (REAL4)( Freq0 - (REAL8)fkdot4.FreqMain );
  /* the remaining spins are simply down-cast to REAL4 */
  for ( s=1; s < PULSAR_MAX_SPINS; s ++ )
    fkdot4.fkdot[s] = (REAL4) doppler->fkdot[s];
  UINT4 maxs;
  /* find highest non-zero spindown-entry */
  for ( maxs = PULSAR_MAX_SPINS - 1;  maxs > 0 ; maxs --  )
    if ( fkdot4.fkdot[maxs] != 0 )
      break;
  fkdot4.spdnOrder = maxs;

  PulsarSpins16 fkdot16;
  fkdot16.s[0] = fkdot4.spdnOrder;
  for (k=1; k<=fkdot4.spdnOrder; k++) fkdot16.s[k] = fkdot4.fkdot[k];


  /* make sure sin/cos lookup-tables are initialized */
  init_sin_cos_LUT_REAL4();

  /* ---------- Buffering quantities that don't need to be recomputed ---------- */

  /* check if for this skyposition and data, the SSB+AMcoefs were already buffered */
  if ( cfvBuffer->Alpha != doppler->Alpha ||
       cfvBuffer->Delta != doppler->Delta ||
       cfvBuffer->multiDetStatesV != multiDetStatesV ||
       cfvBuffer->numSegments != numSegments
       )
    { /* no ==> compute and buffer */

      LogPrintf(LOG_DEBUG, "In function %s: buffering quantities that don't need to be recomputed...\n", fn);

      SkyPosition skypos;
      skypos.system =   COORDINATESYSTEM_EQUATORIAL;
      skypos.longitude = doppler->Alpha;
      skypos.latitude  = doppler->Delta;

      /* prepare buffer */
      XLALEmptyComputeFBufferREAL4V ( cfvBuffer );

      /* store new precomputed quantities in buffer */
      cfvBuffer->Alpha = doppler->Alpha;
      cfvBuffer->Delta = doppler->Delta;
      cfvBuffer->multiDetStatesV = multiDetStatesV ;
      cfvBuffer->numSegments = numSegments;

      if ( (cfvBuffer->multiSSB4V = XLALCalloc ( numSegments, sizeof(*cfvBuffer->multiSSB4V) )) == NULL ) {
        XLALPrintError ("%s: XLALCalloc ( %d, %d) failed.\n", fn, numSegments, sizeof(*cfvBuffer->multiSSB4V) );
        XLAL_ERROR ( fn, XLAL_ENOMEM );
      }
      if ( (cfvBuffer->multiAMcoefV = XLALCalloc ( numSegments, sizeof(*cfvBuffer->multiAMcoefV) )) == NULL ) {
        XLALEmptyComputeFBufferREAL4V ( cfvBuffer );
        XLALPrintError ("%s: XLALCalloc ( %d, %d) failed.\n", fn, numSegments, sizeof(*cfvBuffer->multiAMcoefV) );
        XLAL_ERROR ( fn, XLAL_ENOMEM );
      }

      for ( n=0; n < numSegments; n ++ )
        {
          /* compute new SSB timings over all segments */
          if ( (cfvBuffer->multiSSB4V[n] = XLALGetMultiSSBtimesREAL4 ( multiDetStatesV->data[n], doppler->Alpha, doppler->Delta, doppler->refTime)) == NULL ) {
            XLALEmptyComputeFBufferREAL4V ( cfvBuffer );
            XLALPrintError ( "%s: XLALGetMultiSSBtimesREAL4() failed. xlalErrno = %d.\n", fn, xlalErrno );
            XLAL_ERROR ( fn, XLAL_EFUNC );
          }

          LALStatus status = empty_LALStatus;
          LALGetMultiAMCoeffs ( &status, &(cfvBuffer->multiAMcoefV[n]), multiDetStatesV->data[n], skypos );
          if ( status.statusCode ) {
            XLALEmptyComputeFBufferREAL4V ( cfvBuffer );
            XLALPrintError ("%s: LALGetMultiAMCoeffs() failed with statusCode=%d, '%s'\n", fn, status.statusCode, status.statusDescription );
            XLAL_ERROR ( fn, XLAL_EFAILED );
          }

          /* apply noise-weights to Antenna-patterns and compute A,B,C */
          if ( XLALWeighMultiAMCoeffs ( cfvBuffer->multiAMcoefV[n], multiWeightsV->data[n] ) != XLAL_SUCCESS ) {
            XLALEmptyComputeFBufferREAL4V ( cfvBuffer );
            XLALPrintError("%s: XLALWeighMultiAMCoeffs() failed with error = %d\n", fn, xlalErrno );
            XLAL_ERROR ( fn, XLAL_EFUNC );
          }

          /*
           * OpenCL: copy the data to the flat 1D memory buffers
           */
          {
            MultiSSBtimesREAL4 *multiSSB4 = cfvBuffer->multiSSB4V[n];
            MultiAMCoeffs *multiAMcoeff = cfvBuffer->multiAMcoefV[n];
            SSBtimesREAL4 *tSSB;
            AMCoeffs *amcoe;
            UINT4 X, offset, len;
            
            for (X=0; X<clW->numIFOs; X++) {
              offset = (n*clW->numIFOs + X) * clW->maxNumSFTs;
              tSSB = multiSSB4->data[X];
              amcoe = multiAMcoeff->data[X];
              len = tSSB->DeltaT_int->length * sizeof(REAL4);

              // 1D-index of an array element {n, X, s}:
              // ind = (n*clW->numIFOs + X) * clW->maxNumSFTs + s
               
              memcpy (clW->tSSB_DeltaT_int.data + offset, tSSB->DeltaT_int->data, len);
              memcpy (clW->tSSB_DeltaT_rem.data + offset, tSSB->DeltaT_rem->data, len);
              memcpy (clW->tSSB_TdotM1.data + offset, tSSB->TdotM1->data, len);

              memcpy (clW->amcoe_a.data + offset, amcoe->a->data, len);
              memcpy (clW->amcoe_b.data + offset, amcoe->b->data, len);
            }

            clW->ABCInvD.data[n].Ad = multiAMcoeff->Mmunu.Ad;
            clW->ABCInvD.data[n].Bd = multiAMcoeff->Mmunu.Bd;
            clW->ABCInvD.data[n].Cd = multiAMcoeff->Mmunu.Cd;
            clW->ABCInvD.data[n].InvDd = 1.0f / multiAMcoeff->Mmunu.Dd;
            
          }

        } /* for n < numSegments */

        /* 
         * OpenCL: initialize the array of REAL4-split frequencies
         */
        Freq = Freq0;	
      
        for (k=0; k<clW->numBins; k++) {
          clW->fkdot4.data[k].FreqMain = (INT4)Freq;
          clW->fkdot4.data[k].fkdot0 = (REAL4)( Freq - (REAL8)fkdot4.FreqMain );
          Freq += deltaF;
        }

#if USE_OPENCL_KERNEL
        err_total = CL_SUCCESS;
        err = clEnqueueWriteBuffer ( *(clW->cmd_queue), clW->fkdot4.memobj, 
                                     CL_TRUE, 0, clW->numBins * sizeof(REAL42),
                                     clW->fkdot4.data, 0, NULL, NULL);                       
        err_total += (err-CL_SUCCESS);
        

        /*
         * OpenCL: copy the buffers to the device memory
         */
        UINT4 l2 = clW->numSegments * clW->numIFOs;
        UINT4 l3 = l2 * clW->maxNumSFTs;
        err = clEnqueueWriteBuffer ( *(clW->cmd_queue),           // which queue
                                     clW->tSSB_DeltaT_int.memobj, // destination device pointer
                                     CL_TRUE,                     // blocking write?
                                     0,                           // offset
                                     l3 * sizeof(REAL4),          // size in bytes
                                     clW->tSSB_DeltaT_int.data,   // source pointer
                                     0,                           // cl_uint num_events_in_wait_list
                                     NULL,                        // const cl_event *event_wait_list
                                     NULL);                       // cl_event *event
        err_total += (err-CL_SUCCESS);

        err = clEnqueueWriteBuffer ( *(clW->cmd_queue), clW->tSSB_DeltaT_rem.memobj, 
                                     CL_TRUE, 0, l3 * sizeof(REAL4),          
                                     clW->tSSB_DeltaT_rem.data, 0, NULL, NULL);                       
        err_total += (err-CL_SUCCESS);

        err = clEnqueueWriteBuffer ( *(clW->cmd_queue), clW->tSSB_TdotM1.memobj, 
                                     CL_TRUE, 0, l3 * sizeof(REAL4),          
                                     clW->tSSB_TdotM1.data, 0, NULL, NULL);                       
        err_total += (err-CL_SUCCESS);

        err = clEnqueueWriteBuffer ( *(clW->cmd_queue), clW->amcoe_a.memobj, 
                                     CL_TRUE, 0, l3 * sizeof(REAL4),          
                                     clW->amcoe_a.data, 0, NULL, NULL);                       
        err_total += (err-CL_SUCCESS);

        err = clEnqueueWriteBuffer ( *(clW->cmd_queue), clW->amcoe_b.memobj, 
                                     CL_TRUE, 0, l3 * sizeof(REAL4),          
                                     clW->amcoe_b.data, 0, NULL, NULL);                       
        err_total += (err-CL_SUCCESS);

        err = clEnqueueWriteBuffer ( *(clW->cmd_queue), clW->ABCInvD.memobj, 
                                     CL_TRUE, 0, l2 * sizeof(REAL44),          
                                     clW->ABCInvD.data, 0, NULL, NULL);                       
        err_total += (err-CL_SUCCESS);

        if (err_total != CL_SUCCESS) {
          XLALPrintError ("%s: Error copying data to memory buffer, error code = %d\n", fn, err );
          XLALDestroyCLWorkspace (clW, multiSFTsV);
          XLAL_ERROR ( fn, XLAL_EINVAL );
        }
#endif // #if USE_OPENCL_KERNEL

    } /* if we could NOT reuse previously buffered quantites */

#if USE_OPENCL_KERNEL  
  /*
   * OpenCL: set kernel arguments
   */
  err_total = CL_SUCCESS;
  err = clSetKernelArg(*(clW->kernel),       // wchich kernel      
                       0,                    // argument index
                       sizeof(cl_mem),       // argument data size 
                       (void *)&(clW->Fstat.memobj) ); // argument data
  err_total += (err-CL_SUCCESS);

  err = clSetKernelArg(*(clW->kernel), 1, sizeof(cl_mem), (void *)&(clW->multiSFTsFlat.memobj)); 
  err_total += (err-CL_SUCCESS);

  err = clSetKernelArg(*(clW->kernel), 2, sizeof(cl_mem), (void *)&(clW->numSFTsV.memobj) );
  err_total += (err-CL_SUCCESS);

  err = clSetKernelArg(*(clW->kernel), 3, sizeof(UINT4), (void *)&(clW->sftLen) );
  err_total += (err-CL_SUCCESS);

  err = clSetKernelArg(*(clW->kernel), 4, sizeof(REAL4), (void *)&constTsft);
  err_total += (err-CL_SUCCESS);

  err = clSetKernelArg(*(clW->kernel), 5, sizeof(REAL4), (void *)&constDFreq);
  err_total += (err-CL_SUCCESS);

  err = clSetKernelArg(*(clW->kernel), 6, sizeof(INT4), (void *)&constFreqIndex0);
  err_total += (err-CL_SUCCESS);

  err = clSetKernelArg(*(clW->kernel), 7, sizeof(cl_mem), (void *)&(clW->fkdot4.memobj) );
  err_total += (err-CL_SUCCESS);

  err = clSetKernelArg(*(clW->kernel), 8, sizeof(PulsarSpins16), (void *)&(fkdot16) );
  err_total += (err-CL_SUCCESS);

  err = clSetKernelArg(*(clW->kernel), 9, sizeof(cl_mem), (void *)&(clW->tSSB_DeltaT_int.memobj) );
  err_total += (err-CL_SUCCESS);

  err = clSetKernelArg(*(clW->kernel), 10, sizeof(cl_mem), (void *)&(clW->tSSB_DeltaT_rem.memobj) );
  err_total += (err-CL_SUCCESS);

  err = clSetKernelArg(*(clW->kernel), 11, sizeof(cl_mem), (void *)&(clW->tSSB_TdotM1.memobj) );
  err_total += (err-CL_SUCCESS);

  err = clSetKernelArg(*(clW->kernel), 12, sizeof(cl_mem), (void *)&(clW->amcoe_a.memobj) );
  err_total += (err-CL_SUCCESS);

  err = clSetKernelArg(*(clW->kernel), 13, sizeof(cl_mem), (void *)&(clW->amcoe_b.memobj) );
  err_total += (err-CL_SUCCESS);

  err = clSetKernelArg(*(clW->kernel), 14, sizeof(cl_mem), (void *)&(clW->ABCInvD.memobj) );
  err_total += (err-CL_SUCCESS);

  err = clSetKernelArg(*(clW->kernel), 15, sizeof(FcomponentsREAL4)*clW->numIFOs*clW->maxNumSFTs, NULL );
  err_total += (err-CL_SUCCESS);

  if (err_total != CL_SUCCESS) {
    XLALPrintError ("%s: Error while setting the kernel arguments, error code = %d\n", fn, err );
    XLALDestroyCLWorkspace (clW, multiSFTsV);
    XLAL_ERROR ( fn, XLAL_EINVAL );
  }


  /*
   * OpenCL: enqueue kernel for execution
   * block-thread geometry: (numSegments,numBins,1) x (maxNumSFTs, numIFOs(2), 1)
   */
  LogPrintf(LOG_DEBUG, "In function %s: launching the kernel...\n", fn);

  size_t local_work_size[2], global_work_size[2];
  local_work_size[0] = clW->maxNumSFTs;
  local_work_size[1] = clW->numIFOs;
  global_work_size[0] = local_work_size[0] * clW->numSegments;
  global_work_size[1] = local_work_size[1] * numBins;
    
  err = clEnqueueNDRangeKernel(*(clW->cmd_queue), *(clW->kernel),
                               2, // Work dimensions
                               NULL, // must be NULL (work offset)
                               global_work_size,  // global work items grid dimension
                               local_work_size,   // local work group size
                               0, // no events to wait on
                               NULL, // event list
                               NULL); // event for this kernel
  if (err != CL_SUCCESS) {
    XLALPrintError ("%s: Error enqueueing the kernel, error code = %d\n", fn, err );
    XLALDestroyCLWorkspace (clW, multiSFTsV);
    XLAL_ERROR ( fn, XLAL_EINVAL );
  }

  /*
   * Read output memory buffer after the kernel call
   */
  err = clEnqueueReadBuffer( *(clW->cmd_queue),  // which queue
                             clW->Fstat.memobj,  // source device memory buffer
                             CL_TRUE,            // blocking read
                             0,                  // offset
                             sizeof(REAL4)*clW->Fstat.length, // size
                             clW->Fstat.data,    // pointer
                             0, NULL, NULL);     // events
  if (err != CL_SUCCESS) {
    XLALPrintError ("%s: Error reading output buffer, error code = %d\n", fn, err );
    XLALDestroyCLWorkspace (clW, multiSFTsV);
    XLAL_ERROR ( fn, XLAL_EINVAL );
  }

  /*
   * Store the results in fstatBandV
   */
  for ( n = 0; n < numSegments; n ++ ) {
    for ( k = 0; k < numBins; k++) {
      fstatBandV->data[n].data->data[k] = clW->Fstat.data[k * numSegments + n];
    }
  }


#endif // #if USE_OPENCL_KERNEL

  /* loop over all segments and compute FstatVector over frequencies for each */

#if USE_OPENCL_KERNEL_CPU
  FcomponentsREAL4 *FaFb_components;
  if ( (FaFb_components = XLALCalloc ( clW->numIFOs * clW->maxNumSFTs, sizeof(FcomponentsREAL4) )) == NULL ) {
    XLALEmptyComputeFBufferREAL4V ( cfvBuffer );
    XLALDestroyCLWorkspace (clW, multiSFTsV);
    XLALPrintError ( "%s: memory allocation for FaFb_components failed\n", fn, xlalErrno );
    XLAL_ERROR ( fn, XLAL_EINVAL );
  }
#endif

  for ( n = 0; n < numSegments; n ++ )
    {

      Freq = Freq0;	/* reset frequency to start value for next segment */

      /* loop over frequency values and fill up values in fstatVector */
      for ( k = 0; k < numBins; k++)
        {
          /* REAL4-split frequency value */
          fkdot4.FreqMain = (INT4)Freq;
          fkdot4.fkdot[0] = (REAL4)( Freq - (REAL8)fkdot4.FreqMain );

          /* call the core function to compute one multi-IFO F-statistic */

          if (CALL_XLALCoreFstatREAL4) {
	      /* >>>>> this function could run on the GPU device <<<<< */
	      XLALCoreFstatREAL4 ( &Fstat, &fkdot4, multiSFTsV->data[n], cfvBuffer->multiSSB4V[n], cfvBuffer->multiAMcoefV[n], Dterms );

	      if ( xlalErrno ) {
		XLALEmptyComputeFBufferREAL4V ( cfvBuffer );
		XLALPrintError ("%s: XLALCoreFstatREAL4() failed with errno = %d in loop n=%d, k=%d.\n", fn, xlalErrno, n, k );
		XLAL_ERROR ( fn, XLAL_EFUNC );
	      }

	      fstatBandV->data[n].data->data[k] = Fstat;

	      /* increase frequency by deltaF */
	      Freq += deltaF;
          }

#if USE_OPENCL_KERNEL_CPU
          UINT4 X, alpha;
          for (X = 0; X < clW->numIFOs; X++ ) {
            for (alpha = 0; alpha < clW->maxNumSFTs; alpha++ ) {
              OpenCLComputeFstatFaFb(clW->Fstat.data, 
                                     n, // curSegment
                                     k, // curBin
                                     clW->maxNumSFTs,
                                     alpha,
                                     X,
                                     numSegments,
                                     clW->multiSFTsFlat.data,
                                     clW->numSFTsV.data, 
                                     clW->sftLen, 
                                     constTsft, 
                                     constDFreq, 
                                     constFreqIndex0,
                                     clW->fkdot4.data,
                                     fkdot16,
                                     clW->tSSB_DeltaT_int.data,
                                     clW->tSSB_DeltaT_rem.data,
                                     clW->tSSB_TdotM1.data,
                                     clW->amcoe_a.data,
                                     clW->amcoe_b.data,
                                     clW->ABCInvD.data,
                                     FaFb_components);
              
              if (alpha) {
                FaFb_components[X * clW->maxNumSFTs].Fa.re += FaFb_components[X * clW->maxNumSFTs + alpha].Fa.re;
                FaFb_components[X * clW->maxNumSFTs].Fa.im += FaFb_components[X * clW->maxNumSFTs + alpha].Fa.im;
                FaFb_components[X * clW->maxNumSFTs].Fb.re += FaFb_components[X * clW->maxNumSFTs + alpha].Fb.re;
                FaFb_components[X * clW->maxNumSFTs].Fb.im += FaFb_components[X * clW->maxNumSFTs + alpha].Fb.im;
              }

            }
          }


          REAL4 Fa_re = (FaFb_components[0].Fa.re + FaFb_components[clW->maxNumSFTs].Fa.re) * OOTWOPI_FLOAT;
          REAL4 Fa_im = (FaFb_components[0].Fa.im + FaFb_components[clW->maxNumSFTs].Fa.im) * OOTWOPI_FLOAT;
          REAL4 Fb_re = (FaFb_components[0].Fb.re + FaFb_components[clW->maxNumSFTs].Fb.re) * OOTWOPI_FLOAT;
          REAL4 Fb_im = (FaFb_components[0].Fb.im + FaFb_components[clW->maxNumSFTs].Fb.im) * OOTWOPI_FLOAT;

          REAL4 Ad =  clW->ABCInvD.data[n].Ad;
          REAL4 Bd =  clW->ABCInvD.data[n].Bd;
          REAL4 Cd =  clW->ABCInvD.data[n].Cd;
          REAL4 Dd_inv =  clW->ABCInvD.data[n].InvDd;


          clW->Fstat.data[k * numSegments + n] = Dd_inv * ( Bd * (SQ(Fa_re) + SQ(Fa_im) )
                                               + Ad * ( SQ(Fb_re) + SQ(Fb_im) )
                                               - 2.0f * Cd *( Fa_re * Fb_re + Fa_im * Fb_im )
                                               );
          fstatBandV->data[n].data->data[k] = clW->Fstat.data[k * numSegments + n];

#endif // #if USE_OPENCL_KERNEL_CPU

        } /* for k < numBins */

    } /* for n < numSegments */

#if USE_OPENCL_KERNEL_CPU
  LALFree (FaFb_components);
#endif

  return XLAL_SUCCESS;

} /* XLALComputeFStatFreqBandVector() */



/** Host-bound 'driver' function for the central F-stat computation
 *  of a single F-stat value for one parameter-space point.
 *
 * This is a GPU-adapted replacement for ComputeFstat(), and implements a 'wrapper'
 * around the core F-stat routines that can be executed as kernels on a GPU device.
 *
 */
int
XLALDriverFstatREAL4 ( REAL4 *Fstat,	                 		/**< [out] Fstatistic value 'F' */
                     const PulsarDopplerParams *doppler, 		/**< parameter-space point to compute F for */
                     const MultiSFTVector *multiSFTs,    		/**< normalized (by DOUBLE-sided Sn!) data-SFTs of all IFOs */
                     const MultiNoiseWeights *multiWeights,		/**< noise-weights of all SFTs */
                     const MultiDetectorStateSeries *multiDetStates,	/**< 'trajectories' of the different IFOs */
                     UINT4 Dterms,       				/**< number of terms to use in Dirichlet kernel interpolation */
                     ComputeFBufferREAL4 *cfBuffer       		/**< CF-internal buffering structure */
                     )
{
  static const char *fn = "XLALDriverFstatREAL4()";

  MultiSSBtimesREAL4 *multiSSB4 = NULL;
  MultiAMCoeffs *multiAMcoef = NULL;

  /* check input consistency */
  if ( !Fstat || !doppler || !multiSFTs || !multiDetStates || !cfBuffer ) {
    XLALPrintError("%s: Illegal NULL pointer input.\n", fn );
    XLAL_ERROR (fn, XLAL_EINVAL );
  }
  UINT4 numIFOs = multiSFTs->length;
  if ( multiDetStates->length != numIFOs ) {
    XLALPrintError("%s: inconsistent number of IFOs in SFTs (%d) and detector-states (%d).\n", fn, numIFOs, multiDetStates->length);
    XLAL_ERROR (fn, XLAL_EINVAL );
  }
  if ( multiWeights && multiWeights->length != numIFOs ) {
    XLALPrintError("%s: inconsistent number of IFOs in SFTs (%d) and noise-weights (%d).\n", fn, numIFOs, multiWeights->length);
    XLAL_ERROR (fn, XLAL_EINVAL );
  }

  /* make sure sin/cos lookup-tables are initialized */
  init_sin_cos_LUT_REAL4();

  /* check if for this skyposition and data, the SSB+AMcoef were already buffered */
  if ( cfBuffer->Alpha == doppler->Alpha && cfBuffer->Delta == doppler->Delta && multiDetStates == cfBuffer->multiDetStates )
    { /* yes ==> reuse */
      multiSSB4   = cfBuffer->multiSSB;
      multiAMcoef = cfBuffer->multiAMcoef;
    }
  else
    {
      /* compute new SSB timings */
      if ( (multiSSB4 = XLALGetMultiSSBtimesREAL4 ( multiDetStates, doppler->Alpha, doppler->Delta, doppler->refTime)) == NULL ) {
        XLALPrintError ( "%s: XLALGetMultiSSBtimesREAL4() failed. xlalErrno = %d.\n", fn, xlalErrno );
	XLAL_ERROR ( fn, XLAL_EFUNC );
      }

      /* compute new AM-coefficients */
      SkyPosition skypos;
      LALStatus status = empty_LALStatus;
      skypos.system =   COORDINATESYSTEM_EQUATORIAL;
      skypos.longitude = doppler->Alpha;
      skypos.latitude  = doppler->Delta;

      LALGetMultiAMCoeffs ( &status, &multiAMcoef, multiDetStates, skypos );
      if ( status.statusCode ) {
        XLALPrintError ("%s: LALGetMultiAMCoeffs() failed with statusCode=%d, '%s'\n", fn, status.statusCode, status.statusDescription );
        XLAL_ERROR ( fn, XLAL_EFAILED );
      }

      /* apply noise-weights to Antenna-patterns and compute A,B,C */
      if ( XLALWeighMultiAMCoeffs ( multiAMcoef, multiWeights ) != XLAL_SUCCESS ) {
	XLALPrintError("%s: XLALWeighMultiAMCoeffs() failed with error = %d\n", fn, xlalErrno );
	XLAL_ERROR ( fn, XLAL_EFUNC );
      }

      /* store these in buffer */
      XLALEmptyComputeFBufferREAL4 ( cfBuffer );

      cfBuffer->Alpha = doppler->Alpha;
      cfBuffer->Delta = doppler->Delta;
      cfBuffer->multiDetStates = multiDetStates ;

      cfBuffer->multiSSB = multiSSB4;
      cfBuffer->multiAMcoef = multiAMcoef;

    } /* if we could NOT reuse previously buffered quantites */


  /* ---------- prepare REAL4 version of PulsarSpins to be passed into core functions */
  PulsarSpinsREAL4 fkdot4 = empty_PulsarSpinsREAL4;
  UINT4 s;
  fkdot4.FreqMain = (INT4)doppler->fkdot[0];
  /* fkdot[0] now only carries the *remainder* of Freq wrt FreqMain */
  fkdot4.fkdot[0] = (REAL4)( doppler->fkdot[0] - (REAL8)fkdot4.FreqMain );
  /* the remaining spins are simply down-cast to REAL4 */
  for ( s=1; s < PULSAR_MAX_SPINS; s ++ )
    fkdot4.fkdot[s] = (REAL4) doppler->fkdot[s];
  /* find largest non-zero spindown order */
  UINT4 maxs;
  for ( maxs = PULSAR_MAX_SPINS - 1;  maxs > 0 ; maxs --  )
    if ( doppler->fkdot[maxs] != 0 )
      break;
  fkdot4.spdnOrder = maxs;

  /* call the core function to compute one multi-IFO F-statistic */

  /* >>>>> this function could run on the GPU device <<<<< */
  XLALCoreFstatREAL4 (Fstat, &fkdot4, multiSFTs, multiSSB4, multiAMcoef, Dterms );

  if ( xlalErrno ) {
    XLALPrintError ("%s: XLALCoreFstatREAL4() failed with errno = %d.\n", fn, xlalErrno );
    XLAL_ERROR ( fn, XLAL_EFUNC );
  }

  return XLAL_SUCCESS;


} /* XLALDriverFstatREAL4() */


/** This function computes a multi-IFO F-statistic value for given frequency (+fkdots),
 * antenna-pattern functions, SSB-timings and data ("SFTs").
 *
 * It is *only* using single-precision quantities. The aim is that this function can easily
 * be turned into code to be run on a GPU device.
 *
 */
void
XLALCoreFstatREAL4 (REAL4 *Fstat,				/**< [out] multi-IFO F-statistic value 'F' */
                  PulsarSpinsREAL4 *fkdot4,		/**< [in] frequency and derivatives in REAL4-safe format */
                  const MultiSFTVector *multiSFTs,	/**< [in] input multi-IFO data ("SFTs") */
                  MultiSSBtimesREAL4 *multiSSB4,	/**< [in] multi-IFO SSB-timings in REAL4-safe format */
                  MultiAMCoeffs *multiAMcoef,		/**< [in] multi-IFO antenna-pattern functions */
                  UINT4 Dterms)				/**< [in] parameter: number of Dterms to use in Dirichlet kernel */
{
  static const char *fn = "XLALCoreFstatREAL4()";

  UINT4 numIFOs, X;
  REAL4 Fa_re, Fa_im, Fb_re, Fb_im;

#ifndef LAL_NDEBUG
  /* check input consistency */
  if ( !Fstat || !fkdot4 || !multiSFTs || !multiSSB4 || !multiAMcoef ) {
    XLALPrintError ("%s: illegal NULL input.\n", fn);
    XLAL_ERROR_VOID ( fn, XLAL_EINVAL );
  }
  if ( multiSFTs->length == 0 || multiSSB4->length == 0 || multiAMcoef->length == 0 ||
       !multiSFTs->data || !multiSSB4->data || !multiAMcoef->data )
    {
      XLALPrintError ("%s: invalid empty input.\n", fn);
      XLAL_ERROR_VOID ( fn, XLAL_EINVAL );
    }
#endif

  numIFOs = multiSFTs->length;

#ifndef LAL_NDEBUG
  if ( multiSSB4->length != numIFOs || multiAMcoef->length != numIFOs ) {
    XLALPrintError ("%s: inconsistent number of IFOs between multiSFTs, multiSSB4 and multiAMcoef.\n", fn);
    XLAL_ERROR_VOID ( fn, XLAL_EINVAL );
  }
#endif

  /* ----- loop over detectors and compute all detector-specific quantities ----- */
  Fa_re = Fa_im = Fb_re = Fb_im = 0;

  for ( X=0; X < numIFOs; X ++)
    {
      FcomponentsREAL4 FcX;

      XLALComputeFaFbREAL4 (&FcX, multiSFTs->data[X], fkdot4, multiSSB4->data[X], multiAMcoef->data[X], Dterms);

#ifndef LAL_NDEBUG
      if ( xlalErrno ) {
        XLALPrintError ("%s: XALComputeFaFbREAL4() failed\n", fn );
        XLAL_ERROR_VOID ( fn, XLAL_EFUNC );
      }
      if ( !finite(FcX.Fa.re) || !finite(FcX.Fa.im) || !finite(FcX.Fb.re) || !finite(FcX.Fb.im) ) {
	XLALPrintError("%s: XLALComputeFaFbREAL4() returned non-finite: Fa_X=(%f,%f), Fb_X=(%f,%f) for X=%d\n",
                       fn, FcX.Fa.re, FcX.Fa.im, FcX.Fb.re, FcX.Fb.im, X );
	XLAL_ERROR_VOID ( fn, XLAL_EFPINVAL );
      }
#endif

      /* Fa = sum_X Fa_X */
      Fa_re += FcX.Fa.re;
      Fa_im += FcX.Fa.im;

      /* Fb = sum_X Fb_X */
      Fb_re += FcX.Fb.re;
      Fb_im += FcX.Fb.im;

    } /* for  X < numIFOs */

  /* ----- compute final Fstatistic-value ----- */

  /* convenient shortcuts */
  REAL4 Ad = multiAMcoef->Mmunu.Ad;
  REAL4 Bd = multiAMcoef->Mmunu.Bd;
  REAL4 Cd = multiAMcoef->Mmunu.Cd;
  REAL4 Dd_inv = 1.0f / multiAMcoef->Mmunu.Dd;

  /* NOTE: the data MUST be normalized by the DOUBLE-SIDED PSD (using LALNormalizeMultiSFTVect),
   * therefore there is a factor of 2 difference with respect to the equations in JKS, which
   * where based on the single-sided PSD.
   */
  (*Fstat) = Dd_inv * ( Bd * (SQ(Fa_re) + SQ(Fa_im) )
                      + Ad * ( SQ(Fb_re) + SQ(Fb_im) )
                      - 2.0f * Cd *( Fa_re * Fb_re + Fa_im * Fb_im )
                      );

  return;

} /* XLALCoreFstatREAL4() */



/** Revamped version of LALDemod() (based on TestLALDemod() in CFS).
 * Compute JKS's Fa and Fb, which are ingredients for calculating the F-statistic.
 *
 * Note: this is a single-precision version aimed for GPU parallelization.
 *
 */
void
XLALComputeFaFbREAL4 ( FcomponentsREAL4 *FaFb,		/**< [out] single-IFO Fa/Fb for this parameter-space point */
                       const SFTVector *sfts,		/**< [in] single-IFO input data ("SFTs") */
                       const PulsarSpinsREAL4 *fkdot4,	/**< [in] frequency (and derivatives) in REAL4-safe format */
                       const SSBtimesREAL4 *tSSB,	/**< [in] single-IFO SSB-timings in REAL4-safe format */
                       const AMCoeffs *amcoe,		/**< [in] single-IFO antenna-pattern coefficients */
                       UINT4 Dterms)			/**< [in] number of Dterms to use in Dirichlet kernel */
{
  static const char *fn = "XLALComputeFaFbREAL4()";

  UINT4 alpha;                 	/* loop index over SFTs */
  UINT4 numSFTs;		/* number of SFTs (M in the Notes) */
  COMPLEX8 Fa, Fb;
  REAL4 Tsft; 			/* length of SFTs in seconds */
  INT4 freqIndex0;		/* index of first frequency-bin in SFTs */
  INT4 freqIndex1;		/* index of last frequency-bin in SFTs */

  REAL4 *a_al, *b_al;		/* pointer to alpha-arrays over a and b */

  REAL4 *DeltaT_int_al, *DeltaT_rem_al, *TdotM1_al;	/* pointer to alpha-arrays of SSB-timings */
  SFTtype *SFT_al;		/* SFT alpha  */

  REAL4 norm = OOTWOPI_FLOAT;

  /* ----- check validity of input */
#ifndef LAL_NDEBUG
  if ( !FaFb ) {
    XLALPrintError ("%s: Output-pointer is NULL !\n", fn);
    XLAL_ERROR_VOID ( fn, XLAL_EINVAL);
  }

  if ( !sfts || !sfts->data ) {
    XLALPrintError ("%s: Input SFTs are NULL!\n", fn);
    XLAL_ERROR_VOID ( fn, XLAL_EINVAL);
  }

  if ( !fkdot4 || !tSSB || !tSSB->DeltaT_int || !tSSB->DeltaT_rem || !tSSB->TdotM1 || !amcoe || !amcoe->a || !amcoe->b )
    {
      XLALPrintError ("%s: Illegal NULL in input !\n", fn);
      XLAL_ERROR_VOID ( fn, XLAL_EINVAL);
    }
#endif

  /* ----- prepare convenience variables */
  numSFTs = sfts->length;
  Tsft = (REAL4)( 1.0 / sfts->data[0].deltaF );

  REAL4 dFreq = sfts->data[0].deltaF;
  freqIndex0 = (UINT4) ( sfts->data[0].f0 / dFreq + 0.5); /* lowest freqency-index */
  freqIndex1 = freqIndex0 + sfts->data[0].data->length;

  REAL4 f0 = fkdot4->FreqMain;
  REAL4 df = fkdot4->fkdot[0];
  REAL4 tau = 1.0f / df;
  REAL4 Freq = f0 + df;

  Fa.re = 0.0f;
  Fa.im = 0.0f;
  Fb.re = 0.0f;
  Fb.im = 0.0f;

  /* convenient shortcuts, pointers to beginning of alpha-arrays */
  a_al = amcoe->a->data;
  b_al = amcoe->b->data;
  DeltaT_int_al = tSSB->DeltaT_int->data;
  DeltaT_rem_al = tSSB->DeltaT_rem->data;
  TdotM1_al = tSSB->TdotM1->data;

  SFT_al = sfts->data;

  /* Loop over all SFTs  */
  for ( alpha = 0; alpha < numSFTs; alpha++ )
    {
      REAL4 a_alpha, b_alpha;

      INT4 kstar;		/* central frequency-bin k* = round(xhat_alpha) */
      INT4 k0, k1;

      COMPLEX8 *Xalpha = SFT_al->data->data; /* pointer to current SFT-data */
      COMPLEX8 *Xalpha_l; 	/* pointer to frequency-bin k in current SFT */
      REAL4 s_alpha, c_alpha;	/* sin(2pi kappa_alpha) and (cos(2pi kappa_alpha)-1) */
      REAL4 realQ, imagQ;	/* Re and Im of Q = e^{-i 2 pi lambda_alpha} */
      REAL4 realXP, imagXP;	/* Re/Im of sum_k X_ak * P_ak */
      REAL4 realQXP, imagQXP;	/* Re/Im of Q_alpha R_alpha */

      REAL4 lambda_alpha;
      REAL4 kappa_max, kappa_star;

      /* ----- calculate kappa_max and lambda_alpha */
      {
	UINT4 s; 		/* loop-index over spindown-order */

	REAL4 Tas;		/* temporary variable to calculate (DeltaT_alpha)^s */
        REAL4 T0, dT, deltaT;
        REAL4 Dphi_alpha_int, Dphi_alpha_rem;
        REAL4 phi_alpha_rem;
        REAL4 TdotM1;		/* defined as Tdot_al - 1 */

        /* 1st oder: s = 0 */
        TdotM1 = *TdotM1_al;
        T0 = *DeltaT_int_al;
        dT = *DeltaT_rem_al;
        deltaT = T0 + dT;

        /* phi_alpha = f * Tas; */
        REAL4 T0rem = fmodf ( T0, tau );
        phi_alpha_rem = f0 * dT;
        phi_alpha_rem += T0rem * df;
        phi_alpha_rem += df * dT;
        Dphi_alpha_int = f0;
        Dphi_alpha_rem = df + Freq * TdotM1;

        /* higher-order spindowns */
        Tas = deltaT;
	for (s=1; s <= fkdot4->spdnOrder; s++)
	  {
	    REAL4 fsdot = fkdot4->fkdot[s];
	    Dphi_alpha_rem += fsdot * Tas * inv_fact[s]; 	/* here: Tas = DT^s */
	    Tas *= deltaT;					/* now:  Tas = DT^(s+1) */
	    phi_alpha_rem += fsdot * Tas * inv_fact[s+1];
	  } /* for s <= spdnOrder */

	/* Step 3: apply global factor of Tsft to complete Dphi_alpha */
        Dphi_alpha_int *= Tsft;
	Dphi_alpha_rem *= Tsft;

        REAL4 tmp = REM( 0.5f * Dphi_alpha_int ) + REM ( 0.5f * Dphi_alpha_rem );
	lambda_alpha = phi_alpha_rem - tmp;

	/* real- and imaginary part of e^{-i 2 pi lambda_alpha } */
	sin_cos_2PI_LUT_REAL4 ( &imagQ, &realQ, - lambda_alpha );

        kstar = (INT4)Dphi_alpha_int + (INT4)Dphi_alpha_rem;
	kappa_star = REM(Dphi_alpha_int) + REM(Dphi_alpha_rem);
	kappa_max = kappa_star + 1.0f * Dterms - 1.0f;

	/* ----- check that required frequency-bins are found in the SFTs ----- */
	k0 = kstar - Dterms + 1;
	k1 = k0 + 2 * Dterms - 1;
	if ( (k0 < freqIndex0) || (k1 > freqIndex1) )
	  {
	    XLALPrintError ("%s: Required frequency-bins [%d, %d] not covered by SFT-interval [%d, %d]\n\n",
                            fn, k0, k1, freqIndex0, freqIndex1 );
	    XLAL_ERROR_VOID( fn, XLAL_EDOM);
	  }

      } /* compute kappa_star, lambda_alpha */

      /* NOTE: sin[ 2pi (Dphi_alpha - k) ] = sin [ 2pi Dphi_alpha ], therefore
       * the trig-functions need to be calculated only once!
       * We choose the value sin[ 2pi(Dphi_alpha - kstar) ] because it is the
       * closest to zero and will pose no numerical difficulties !
       */
      sin_cos_2PI_LUT_REAL4 ( &s_alpha, &c_alpha, kappa_star );
      c_alpha -= 1.0f;

      /* ---------- calculate the (truncated to Dterms) sum over k ---------- */

      /* ---------- ATTENTION: this the "hot-loop", which will be
       * executed many millions of times, so anything in here
       * has a HUGE impact on the whole performance of the code.
       *
       * DON'T touch *anything* in here unless you really know
       * what you're doing !!
       *------------------------------------------------------------
       */

      Xalpha_l = Xalpha + k0 - freqIndex0;  /* first frequency-bin in sum */

      realXP = 0;
      imagXP = 0;

      /* if no danger of denominator -> 0 */
      if ( ( kappa_star > LD_SMALL4 ) && (kappa_star < 1.0 - LD_SMALL4) )
	{
	  /* improved hotloop algorithm by Fekete Akos:
	   * take out repeated divisions into a single common denominator,
	   * plus use extra cleverness to compute the nominator efficiently...
	   */
	  REAL4 Sn = (*Xalpha_l).re;
	  REAL4 Tn = (*Xalpha_l).im;
	  REAL4 pn = kappa_max;
	  REAL4 qn = pn;
	  REAL4 U_alpha, V_alpha;

	  /* recursion with 2*Dterms steps */
	  UINT4 l;
	  for ( l = 1; l < 2*Dterms; l ++ )
	    {
	      Xalpha_l ++;

	      pn = pn - 1.0f; 			/* p_(n+1) */
	      Sn = pn * Sn + qn * (*Xalpha_l).re;	/* S_(n+1) */
	      Tn = pn * Tn + qn * (*Xalpha_l).im;	/* T_(n+1) */
	      qn *= pn;				/* q_(n+1) */
	    } /* for l <= 2*Dterms */

          REAL4 qn_inv = 1.0f / qn;

	  U_alpha = Sn * qn_inv;
	  V_alpha = Tn * qn_inv;

#ifndef LAL_NDEBUG
	  if ( !finite(U_alpha) || !finite(V_alpha) || !finite(pn) || !finite(qn) || !finite(Sn) || !finite(Tn) ) {
	    XLAL_ERROR_VOID (fn, XLAL_EFPINVAL );
	  }
#endif

	  realXP = s_alpha * U_alpha - c_alpha * V_alpha;
	  imagXP = c_alpha * U_alpha + s_alpha * V_alpha;

	} /* if |remainder| > LD_SMALL4 */
      else
	{ /* otherwise: lim_{rem->0}P_alpha,k  = 2pi delta_{k,kstar} */
	  UINT4 ind0;
  	  if ( kappa_star <= LD_SMALL4 ) ind0 = Dterms - 1;
  	  else ind0 = Dterms;
	  realXP = TWOPI_FLOAT * Xalpha_l[ind0].re;
	  imagXP = TWOPI_FLOAT * Xalpha_l[ind0].im;
	} /* if |remainder| <= LD_SMALL4 */

      realQXP = realQ * realXP - imagQ * imagXP;
      imagQXP = realQ * imagXP + imagQ * realXP;

      /* we're done: ==> combine these into Fa and Fb */
      a_alpha = (*a_al);
      b_alpha = (*b_al);

      Fa.re += a_alpha * realQXP;
      Fa.im += a_alpha * imagQXP;

      Fb.re += b_alpha * realQXP;
      Fb.im += b_alpha * imagQXP;


      /* advance pointers over alpha */
      a_al ++;
      b_al ++;

      DeltaT_int_al ++;
      DeltaT_rem_al ++;
      TdotM1_al ++;

      SFT_al ++;

    } /* for alpha < numSFTs */

  /* return result */
  FaFb->Fa.re = norm * Fa.re;
  FaFb->Fa.im = norm * Fa.im;
  FaFb->Fb.re = norm * Fb.re;
  FaFb->Fb.im = norm * Fb.im;

  return;

} /* XLALComputeFaFbREAL4() */



/** Destroy a MultiSSBtimesREAL4 structure.
 *  Note, this is "NULL-robust" in the sense that it will not crash
 *  on NULL-entries anywhere in this struct, so it can be used
 *  for failure-cleanup even on incomplete structs
 */
void
XLALDestroyMultiSSBtimesREAL4 ( MultiSSBtimesREAL4 *multiSSB )
{
  UINT4 X;

  if ( ! multiSSB )
    return;

  if ( multiSSB->data )
    {
      for ( X=0; X < multiSSB->length; X ++ )
	{
          XLALDestroySSBtimesREAL4 ( multiSSB->data[X] );
	} /* for X < numDetectors */
      LALFree ( multiSSB->data );
    }

  LALFree ( multiSSB );

  return;

} /* XLALDestroyMultiSSBtimesREAL4() */


/** Destroy a SSBtimesREAL4 structure.
 *  Note, this is "NULL-robust" in the sense that it will not crash
 *  on NULL-entries anywhere in this struct, so it can be used
 *  for failure-cleanup even on incomplete structs
 */
void
XLALDestroySSBtimesREAL4 ( SSBtimesREAL4 *tSSB )
{
  if ( ! tSSB )
    return;

  if ( tSSB->DeltaT_int )
    XLALDestroyREAL4Vector ( tSSB->DeltaT_int );

  if ( tSSB->DeltaT_rem )
    XLALDestroyREAL4Vector ( tSSB->DeltaT_rem );

  if ( tSSB->TdotM1 )
    XLALDestroyREAL4Vector ( tSSB->TdotM1 );

  LALFree ( tSSB );

  return;

} /* XLALDestroySSBtimesREAL4() */


/** Multi-IFO version of LALGetSSBtimesREAL4().
 * Get all SSB-timings for all input detector-series in REAL4 representation.
 *
 */
MultiSSBtimesREAL4 *
XLALGetMultiSSBtimesREAL4 ( const MultiDetectorStateSeries *multiDetStates, 	/**< [in] detector-states at timestamps t_i */
                            REAL8 Alpha, REAL8 Delta,				/**< source sky-location, in equatorial coordinates  */
                            LIGOTimeGPS refTime
                            )
{
  static const char *fn = "XLALGetMultiSSBtimesREAL4()";

  UINT4 X, numDetectors;
  MultiSSBtimesREAL4 *ret;

  /* check input */
  if ( !multiDetStates || multiDetStates->length==0 ) {
    XLALPrintError ("%s: illegal NULL or empty input 'multiDetStates'.\n", fn );
    XLAL_ERROR_NULL ( fn, XLAL_EINVAL );
  }

  numDetectors = multiDetStates->length;

  if ( ( ret = XLALCalloc( 1, sizeof( *ret ) )) == NULL ) {
    XLALPrintError ("%s: XLALCalloc( 1, %d ) failed.\n", fn, sizeof( *ret ) );
    XLAL_ERROR_NULL ( fn, XLAL_ENOMEM );
  }

  ret->length = numDetectors;
  if ( ( ret->data = XLALCalloc ( numDetectors, sizeof ( *ret->data ) )) == NULL ) {
    XLALFree ( ret );
    XLALPrintError ("%s: XLALCalloc( %d, %d ) failed.\n", fn, numDetectors, sizeof( *ret->data ) );
    XLAL_ERROR_NULL ( fn, XLAL_ENOMEM );
  }

  for ( X=0; X < numDetectors; X ++ )
    {
      if ( (ret->data[X] = XLALGetSSBtimesREAL4 (multiDetStates->data[X], Alpha, Delta, refTime)) == NULL ) {
        XLALPrintError ("%s: XLALGetSSBtimesREAL4() failed. xlalErrno = %d\n", fn, xlalErrno );
        goto failed;
      }

    } /* for X < numDet */

  goto success;

 failed:
  /* free all memory allocated so far */
  XLALDestroyMultiSSBtimesREAL4 ( ret );
  XLAL_ERROR_NULL (fn, XLAL_EFAILED );

 success:
  return ret;

} /* XLALGetMultiSSBtimesREAL4() */



/** XLAL REAL4-version of LALGetSSBtimes()
 *
 */
SSBtimesREAL4 *
XLALGetSSBtimesREAL4 ( const DetectorStateSeries *DetectorStates,	/**< [in] detector-states at timestamps t_i */
                       REAL8 Alpha, REAL8 Delta,			/**< source sky-location, in equatorial coordinates  */
                       LIGOTimeGPS refTime
                       )
{
  static const char *fn = "XLALGetSSBtimesREAL4()";

  UINT4 numSteps, i;
  REAL8 refTimeREAL8;
  SSBtimesREAL4 *ret;

  /* check input consistency */
  if ( !DetectorStates || DetectorStates->length==0 ) {
    XLALPrintError ("%s: illegal NULL or empty input 'DetectorStates'.\n", fn );
    XLAL_ERROR_NULL ( fn, XLAL_EINVAL );
  }

  numSteps = DetectorStates->length;		/* number of timestamps */

  /* convenience variables */
  refTimeREAL8 = XLALGPSGetREAL8( &refTime );

  /* allocate return container */
  if ( ( ret = XLALMalloc( sizeof(*ret))) == NULL ) {
    XLALPrintError ("%s: XLALMalloc(%d) failed.\n", fn, sizeof(*ret) );
    goto failed;
  }
  if ( (ret->DeltaT_int  = XLALCreateREAL4Vector ( numSteps )) == NULL ||
       (ret->DeltaT_rem  = XLALCreateREAL4Vector ( numSteps )) == NULL ||
       (ret->TdotM1      = XLALCreateREAL4Vector ( numSteps )) == NULL )
    {
      XLALPrintError ("%s: XLALCreateREAL4Vector ( %d ) failed.\n", fn, numSteps );
      goto failed;
    }

  /* loop over all SFTs and compute timing info */
  for (i=0; i < numSteps; i++ )
    {
      BarycenterInput baryinput = empty_BarycenterInput;
      EmissionTime emit;
      DetectorState *state = &(DetectorStates->data[i]);
      LALStatus status = empty_LALStatus;
      REAL8 deltaT;
      REAL4 deltaT_int;

      baryinput.tgps = state->tGPS;
      baryinput.site = DetectorStates->detector;
      baryinput.site.location[0] /= LAL_C_SI;
      baryinput.site.location[1] /= LAL_C_SI;
      baryinput.site.location[2] /= LAL_C_SI;

      baryinput.alpha = Alpha;
      baryinput.delta = Delta;
      baryinput.dInv = 0;

      LALBarycenter(&status, &emit, &baryinput, &(state->earthState) );
      if ( status.statusCode ) {
        XLALPrintError ("%s: LALBarycenter() failed with status = %d, '%s'\n", fn, status.statusCode, status.statusDescription );
        goto failed;
      }

      deltaT = XLALGPSGetREAL8 ( &emit.te ) - refTimeREAL8;
      deltaT_int = (INT4)deltaT;

      ret->DeltaT_int->data[i] = deltaT_int;
      ret->DeltaT_rem->data[i]  = (REAL4)( deltaT - (REAL8)deltaT_int );
      ret->TdotM1->data[i] = (REAL4) ( emit.tDot - 1.0 );

    } /* for i < numSteps */

  /* finally: store the reference-time used into the output-structure */
  ret->refTime = refTime;

  goto success;

 failed:
  XLALDestroySSBtimesREAL4 ( ret );
  XLAL_ERROR_NULL ( fn, XLAL_EFAILED );

 success:
  return ret;

} /* XLALGetSSBtimesREAL4() */



/** Destruction of a ComputeFBufferREAL4 *contents*,
 * i.e. the multiSSB and multiAMcoeff, while the
 * buffer-container is not freed (which is why it's passed
 * by value and not by reference...) */
void
XLALEmptyComputeFBufferREAL4 ( ComputeFBufferREAL4 *cfb)
{
  if ( !cfb )
    return;

  XLALDestroyMultiSSBtimesREAL4 ( cfb->multiSSB );
  cfb->multiSSB = NULL;

  XLALDestroyMultiAMCoeffs ( cfb->multiAMcoef );
  cfb->multiAMcoef = NULL;

  return;

} /* XLALDestroyComputeFBufferREAL4() */



/** Initialize OpenCL workspace
 * Create memory objects associated with OpenCL context
 * and memory buffers */
int
XLALInitCLWorkspace ( CLWorkspace *clW,  
                      const MultiSFTVectorSequence *stackMultiSFT )
{
  static const char *fn = "XLALInitCLWorkspace()";
  static const char *cl_kernel_filepath = "/Users/oleg/lalsuite/lalapps/src/pulsar/FDS_isolated/kernel.cl"; //TODO: do something with hardcoded kernel path

#if USE_OPENCL_KERNEL  
  cl_int err, err_total;
  const cl_uint max_num_platforms = 3;
  static cl_platform_id platforms[3];
  cl_uint num_platforms;
  char strInfo[100];
  static cl_context context;
  const cl_uint max_num_devices = 4; 
  cl_device_id devices[4];
  cl_uint num_devices;
  static cl_command_queue cmd_queue;
  static cl_program program;
  static cl_kernel kernel;

  clW->platform  = NULL;
  clW->device    = NULL;
  clW->context   = NULL;
  clW->cmd_queue = NULL;
  clW->program   = NULL;
  clW->kernel    = NULL;
#endif

  clW->multiSFTsFlat.data = NULL;
  clW->numSFTsV.data = NULL;
  clW->tSSB_DeltaT_int.data = NULL;
  clW->tSSB_DeltaT_rem.data = NULL; 
  clW->tSSB_TdotM1.data = NULL; 
  clW->amcoe_a.data = NULL;
  clW->amcoe_b.data = NULL;
  clW->ABCInvD.data = NULL;
  clW->Fstat.data = NULL;

  LogPrintf(LOG_DEBUG, "In function %s: initializing OpenCL workspace\n", fn);

#if USE_OPENCL_KERNEL
  // query the platform ID
  LogPrintf(LOG_DEBUG, "In function %s: query the platform ID\n", fn);
  clGetPlatformIDs(max_num_platforms, platforms, &num_platforms);
  clW->platform = &(platforms[0]);

  // query OpenCL platform info
  LogPrintf(LOG_DEBUG, "In function %s: query the OpenCL platform info\n", fn);
  err = clGetPlatformInfo ( *(clW->platform), CL_PLATFORM_PROFILE, 100, strInfo, NULL );
  if (err != CL_SUCCESS) {
      XLALPrintError ("%s: Error calling clGetPlatformInfo.\n", fn );
      XLAL_ERROR ( fn, XLAL_EINVAL );
  }

  // create OpenCL GPU context 
  LogPrintf(LOG_DEBUG, "In function %s: create the OpenCL GPU context\n", fn);
  context = clCreateContextFromType(0, CL_DEVICE_TYPE_GPU, NULL, NULL, NULL); 
  if (context == (cl_context)0) {
      XLALPrintError ("%s: Failed to create context\n", fn );
      XLALDestroyCLWorkspace (clW, stackMultiSFT);
      XLAL_ERROR ( fn, XLAL_EINVAL );
  }
  clW->context = &context;

  // get the list of available GPU devices
  LogPrintf(LOG_DEBUG, "In function %s: get the list of all available GPU devices\n", fn);
  err = clGetDeviceIDs( *(clW->platform),
                    CL_DEVICE_TYPE_GPU,
                    max_num_devices,
                    devices,
                    &num_devices);
  if (err != CL_SUCCESS) {
      XLALPrintError ("%s: Error querying number of OpenCL devices\n", fn );
      XLALDestroyCLWorkspace (clW, stackMultiSFT);
      XLAL_ERROR ( fn, XLAL_EINVAL );
  }
  clW->device = &(devices[0]);

  // create a command-queue
  LogPrintf(LOG_DEBUG, "In function %s: create OpenCL command queue\n", fn);
  cmd_queue = clCreateCommandQueue(*(clW->context), *(clW->device),  
                                   CL_QUEUE_PROFILING_ENABLE, 
                                   &err);
  if (cmd_queue == (cl_command_queue)0) {
      XLALPrintError ("%s: Failed to create command queue\n", fn );
      XLALDestroyCLWorkspace (clW, stackMultiSFT);
      XLAL_ERROR ( fn, XLAL_EINVAL );
  }
  clW->cmd_queue = &cmd_queue;

#endif // #if USE_OPENCL_KERNEL

  // allocate flattened memory buffers on host

  // WARNING: HARDCODED VALUES FOR NOW (RUN S5R4)
  // TODO: include proper assertions here to ensure correctness
  clW->numSegments = stackMultiSFT->length;
  clW->numIFOs = NUM_IFOS;
  clW->maxNumSFTs = MAX_NUM_SFTS;
  clW->sftLen = stackMultiSFT->data[0]->data[0]->data[0].data->length;
  clW->numBins = 200;

  // allocate large buffer arrays on a host
  LogPrintf(LOG_DEBUG, "In function %s: allocate 1D buffer arrays\n", fn);
  UINT4 l2 = clW->numSegments * clW->numIFOs;
  UINT4 l3 = l2 * clW->maxNumSFTs;
  UINT4 l4 = l3 * clW->sftLen;

  clW->multiSFTsFlat.length = l4;
  clW->multiSFTsFlat.data = XLALMalloc( sizeof(COMPLEX8) * l4);

  clW->numSFTsV.length = l2;
  clW->numSFTsV.data = XLALMalloc( sizeof(UINT4) * l2 );

  clW->tSSB_DeltaT_int.length = l3;
  clW->tSSB_DeltaT_int.data = XLALMalloc( sizeof(REAL4) * l3 );

  clW->tSSB_DeltaT_rem.length = l3;
  clW->tSSB_DeltaT_rem.data = XLALMalloc( sizeof(REAL4) * l3 );

  clW->tSSB_TdotM1.length = l3;
  clW->tSSB_TdotM1.data = XLALMalloc( sizeof(REAL4) * l3 );

  clW->amcoe_a.length = l3;
  clW->amcoe_a.data = XLALMalloc( sizeof(REAL4) * l3 );

  clW->amcoe_b.length = l3;
  clW->amcoe_b.data = XLALMalloc( sizeof(REAL4) * l3 );

  clW->ABCInvD.length = l2;
  clW->ABCInvD.data = XLALMalloc( sizeof(REAL44) * l2 );

  // clW->fkdot4: initialized in RearrangeSFTData
  // clW->Fstat:  initialized in RearrangeSFTData

  if ( clW->multiSFTsFlat.data == NULL || clW->numSFTsV.data == NULL
       || clW->tSSB_DeltaT_int.data == NULL || clW->tSSB_DeltaT_rem.data == NULL || clW->tSSB_DeltaT_rem.data == NULL 
       || clW->amcoe_a.data == NULL || clW->amcoe_b.data == NULL ) {
      XLALPrintError ("%s: XLALMalloc() failed.\n", fn );
      XLALDestroyCLWorkspace (clW, stackMultiSFT);
      XLAL_ERROR ( fn, XLAL_EINVAL );
  } 
  
  { // SFT data rearrangement block
    UINT4 n, X, s;

    MultiSFTVector *multiSFT;
    SFTVector *sFT;
    COMPLEX8Vector *cV;
    COMPLEX8 *ptrData;
    //TODO: state your assertions here!!!
    /* Traverse stackMultiSFT and do the following:
     * 1. copy data segments sequentially to multiSFTsFlat;
     * 2. deallocate data segments in stackMultiSFT;
     * 3. repoint the data pointers to the middle of multiSFTsFlat.
     *
     * Index of a data element at position m in SFT s, detector X and segment n, is:
     * ind = m + cV->sftLen * (s + cV->maxNumSFTs * (X + cV->numIFOs * n))
     */
    LogPrintf(LOG_DEBUG, "In function %s: flatten the stackMultiSFT data structure\n", fn);
    ptrData = clW->multiSFTsFlat.data;

    if (clW->numSegments != stackMultiSFT->length) {
      XLALPrintError ("%s: internal error: inconsistent clW->numSegments\n", fn);
      XLALDestroyCLWorkspace (clW, stackMultiSFT);
      XLAL_ERROR ( fn, XLAL_EINVAL );
    }
    for (n=0; n<stackMultiSFT->length; n++) {
      multiSFT = stackMultiSFT->data[n];
      if (clW->numIFOs != multiSFT->length) {
        XLALPrintError ("%s: internal error: inconsistent clW->numIFOs for segment %d\n", fn, n);
        XLALDestroyCLWorkspace (clW, stackMultiSFT);
        XLAL_ERROR ( fn, XLAL_EINVAL );
      }
      for (X=0; X<multiSFT->length; X++) {
        sFT = multiSFT->data[X];
        if (clW->maxNumSFTs < sFT->length) {
          XLALPrintError ("%s: internal error: number of SFTs exceeds MAX_NUM_SFTS for segment %d, detector %d\n", fn, n, X);
          XLALDestroyCLWorkspace (clW, stackMultiSFT);
          XLAL_ERROR ( fn, XLAL_EINVAL );
        }
        clW->numSFTsV.data[n * multiSFT->length + X] = sFT->length;
        for (s=0; s<sFT->length; s++) { 
          cV = sFT->data[s].data;
          if (clW->sftLen != cV->length) {
            XLALPrintError ("%s: internal error: inconsistent SFT length in segment=%d, detector=%d, SFT %d\n", fn, n, X, s);
            XLALDestroyCLWorkspace (clW, stackMultiSFT);
            XLAL_ERROR ( fn, XLAL_EINVAL );
          }
          memcpy (ptrData, cV->data, cV->length * sizeof(COMPLEX8));
          
          LALFree (cV->data);
          cV->data = ptrData;
          ptrData += clW->sftLen;
        }
        ptrData += (clW->maxNumSFTs - sFT->length) * clW->sftLen;
      }
    }
  } // end of SFT data rearrangement block

#if USE_OPENCL_KERNEL  
  // allocate buffer arrays on the device
  // only SFT array is copied to the device; the rest of the arrays are created empty and will be filled later
  LogPrintf(LOG_DEBUG, "In function %s: allocate OpenCL device memory buffers\n", fn);
  err_total = CL_SUCCESS;
  clW->multiSFTsFlat.memobj = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(COMPLEX8) * clW->multiSFTsFlat.length, clW->multiSFTsFlat.data, &err);            
  err_total += (err-CL_SUCCESS);

  clW->numSFTsV.memobj = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(UINT4) * clW->numSFTsV.length, clW->numSFTsV.data, &err);
  err_total += (err-CL_SUCCESS);

  clW->tSSB_DeltaT_int.memobj = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(REAL4) * clW->tSSB_DeltaT_int.length, NULL, &err);
  err_total += (err-CL_SUCCESS);

  clW->tSSB_DeltaT_rem.memobj = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(REAL4) * clW->tSSB_DeltaT_rem.length, NULL, &err);
  err_total += (err-CL_SUCCESS);

  clW->tSSB_TdotM1.memobj = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(REAL4) * clW->tSSB_TdotM1.length, NULL, &err);
  err_total += (err-CL_SUCCESS);

  clW->amcoe_a.memobj = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(REAL4) * clW->amcoe_a.length, NULL, &err);
  err_total += (err-CL_SUCCESS);

  clW->amcoe_b.memobj = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(REAL4) * clW->amcoe_b.length, NULL, &err);
  err_total += (err-CL_SUCCESS);

  clW->ABCInvD.memobj = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(REAL44) * clW->ABCInvD.length, NULL, &err);
  err_total += (err-CL_SUCCESS);

  if (err_total != CL_SUCCESS) {
      XLALPrintError ("%s: Error creating memory buffer for ABCInvD, error code = %d\n", fn, err );
      XLALDestroyCLWorkspace (clW, stackMultiSFT);
      XLAL_ERROR ( fn, XLAL_EINVAL );
  }



  // read kernel source into memory
  LogPrintf(LOG_DEBUG, "In function %s: read kernel source into memory\n", fn);

  FILE *fd;
  char *cl_kernel_strings;
  const size_t max_kernel_bytes = 50000;
  size_t bytes_read;

  if ( ( cl_kernel_strings  = XLALMalloc( max_kernel_bytes * sizeof(char))) == NULL ) {
      XLALPrintError ("%s: XLALMalloc() failed.\n", fn );
      XLALDestroyCLWorkspace (clW, stackMultiSFT );
      XLAL_ERROR ( fn, XLAL_EINVAL );
  }

  if((fd=fopen(cl_kernel_filepath, "rb"))==NULL) {
    XLALPrintError ("%s: ERROR: Cannot open OpenCL kernel file at location \"%s\".\n", fn, cl_kernel_filepath );
    XLALDestroyCLWorkspace (clW, stackMultiSFT );
    XLAL_ERROR ( fn, XLAL_EINVAL );
  }

  fread (cl_kernel_strings, max_kernel_bytes, 1, fd);
  if (! feof(fd) || ferror(fd)) {
    XLALPrintError ("%s: ERROR: Cannot read OpenCL kernel file at location \"%s\".\n", fn, cl_kernel_filepath );
    XLALDestroyCLWorkspace (clW, stackMultiSFT );
    XLAL_ERROR ( fn, XLAL_EINVAL );
  }
  bytes_read = ftell(fd);
  fclose (fd);
  cl_kernel_strings[bytes_read] = '\0'; // null-terminated string

  // create the program
  LogPrintf(LOG_DEBUG, "In function %s: create OpenCL program\n", fn);
  program = clCreateProgramWithSource( *(clW->context),
                                       1, // string count
                                       (const char **) &cl_kernel_strings, // program strings
                                       NULL, // string lengths
                                       &err); // error code
  LALFree(cl_kernel_strings);                                     

  if (program == (cl_program)0) {
    XLALPrintError( "%s: ERROR: failed to create OpenCL program\n", fn);
    XLALDestroyCLWorkspace (clW, stackMultiSFT );
    XLAL_ERROR ( fn, XLAL_EINVAL );
  } 
  clW->program = &program;

  // build the program
  LogPrintf(LOG_DEBUG, "In function %s: build OpenCL program...\n", fn);
  err = clBuildProgram(*(clW->program),
                       0,     // num devices in device list
                       NULL,  // device list
                       NULL,  // options
                       NULL,  // notifier callback function ptr
                       NULL); // error code
  if (err != CL_SUCCESS) {
    size_t len;
    char debug_buffer[2048];
    XLALPrintError( "%s: ERROR: failed to compile OpenCL program\n", fn);
    clGetProgramBuildInfo(*(clW->program), *(clW->device), CL_PROGRAM_BUILD_LOG, 
                          sizeof(debug_buffer), debug_buffer, &len);
    XLALPrintError("%s\n", debug_buffer);
    XLALDestroyCLWorkspace (clW, stackMultiSFT );
    XLAL_ERROR ( fn, XLAL_EINVAL );
  }

  // finally, create the kernel
  LogPrintf(LOG_DEBUG, "In function %s: create kernel...\n", fn);
  kernel = clCreateKernel(*(clW->program), "OpenCLComputeFstatFaFb", NULL); 
  if (kernel == (cl_kernel)0) {
    XLALPrintError( "%s: ERROR: failed to create kernel\n", fn);
    XLALDestroyCLWorkspace (clW, stackMultiSFT );
    XLAL_ERROR ( fn, XLAL_EINVAL );
  }
  clW->kernel = &kernel;
#endif // #if USE_OPENCL_KERNEL
   
  return 0;
} /* XLALInitCLWorkspace() */



/** Rearrange SFT data structures
 * Flatten the SFT data: combine small chunks of memory into a single 
 * contiguous array, accessable via 4d-index */
void
XLALRearrangeSFTData ( CLWorkspace *clW,
                       const REAL4FrequencySeriesVector *fstatBandV )
{
  static const char *fn = "XLALRearrangeSFTData()";
  static int call_count = 0;
  ++call_count;

  LogPrintf(LOG_DEBUG, "In function %s: rearrange SFT data structures\n", fn);

  clW->numBins = fstatBandV->data[0].data->length;

  // deallocate previously allocated memory
  if (clW->fkdot4.data) LALFree(clW->fkdot4.data);
  if (clW->Fstat.data)  LALFree(clW->Fstat.data);

  // allocate memory for new arrays
  clW->fkdot4.length = clW->numBins;
  clW->fkdot4.data = XLALMalloc( sizeof(REAL42) * clW->numBins );

  clW->Fstat.length = clW->numSegments * clW->numBins;
  clW->Fstat.data = XLALMalloc( sizeof(REAL4) * clW->Fstat.length );

  if ( clW->Fstat.data == NULL || clW->fkdot4.data == NULL ) {
      XLALPrintError ("%s: XLALMalloc() failed.\n", fn );
      XLAL_ERROR ( fn, XLAL_EINVAL );
  } 

#if USE_OPENCL_KERNEL
  { // create memory buffers on the device
    cl_int err, err_total;

    if (call_count > 1) {
      freeCLMemoryObject(&(clW->fkdot4.memobj));
      freeCLMemoryObject(&(clW->Fstat.memobj));
    }

    clW->Fstat.memobj = clCreateBuffer (*(clW->context), CL_MEM_READ_WRITE, sizeof(REAL4)*clW->Fstat.length, NULL, &err);
    err_total += (err-CL_SUCCESS);

    clW->fkdot4.memobj = clCreateBuffer(*(clW->context), CL_MEM_READ_ONLY, sizeof(REAL42)*clW->fkdot4.length, NULL, &err);
    err_total += (err-CL_SUCCESS);

    if (err_total != CL_SUCCESS) {
        XLALPrintError ("%s: Error creating memory buffer for ABCInvD, error code = %d\n", fn, err );
        XLAL_ERROR ( fn, XLAL_EINVAL );
    }
  }
#endif // #if USE_OPENCL_KERNEL  

} /* XLALRearrangeSFTData */



/** Close OpenCL workspace
 * Free all objects and memory associated with the OpenCL Workspace */
void
XLALDestroyCLWorkspace ( CLWorkspace *clW,
                         const MultiSFTVectorSequence *stackMultiSFT )
{
  static const char *fn = "XLALDestroyCLWorkspace()";

  LogPrintf(LOG_DEBUG, "In function %s: deallocate memory, release OpenCL context\n", fn);

#if USE_OPENCL_KERNEL
  freeCLMemoryObject(&(clW->multiSFTsFlat.memobj));
  freeCLMemoryObject(&(clW->numSFTsV.memobj));
  freeCLMemoryObject(&(clW->fkdot4.memobj));
  freeCLMemoryObject(&(clW->tSSB_DeltaT_int.memobj));
  freeCLMemoryObject(&(clW->tSSB_DeltaT_rem.memobj));
  freeCLMemoryObject(&(clW->tSSB_TdotM1.memobj));
  freeCLMemoryObject(&(clW->amcoe_a.memobj));
  freeCLMemoryObject(&(clW->amcoe_b.memobj));
  freeCLMemoryObject(&(clW->ABCInvD.memobj));
  freeCLMemoryObject(&(clW->Fstat.memobj));
#endif

  if (clW->multiSFTsFlat.data) {
    // deallocate the array which contains flattened data for stackMultiSFTs
    LALFree (clW->multiSFTsFlat.data);

    UINT4 n, X, s;

    MultiSFTVector *multiSFT;
    SFTVector *sFT;
    COMPLEX8Vector *cV;
    
    // set all data pointers on the lowest level to NULL, since their memory has been 
    // already deallocated
    for (n=0; n<stackMultiSFT->length; n++) {
      multiSFT = stackMultiSFT->data[n];
      for (X=0; X<multiSFT->length; X++) {
        sFT = multiSFT->data[X];
        for (s=0; s<sFT->length; s++) { 
          cV = sFT->data[s].data;
          cV->data = NULL;
        }
      }
    }
  } // if clW->multiSFTsFlat.data != NULL 

  if (clW->tSSB_DeltaT_int.data) LALFree(clW->tSSB_DeltaT_int.data);
  if (clW->tSSB_DeltaT_rem.data) LALFree(clW->tSSB_DeltaT_rem.data);
  if (clW->tSSB_TdotM1.data)     LALFree(clW->tSSB_TdotM1.data);
  if (clW->amcoe_a.data)         LALFree(clW->amcoe_a.data);
  if (clW->amcoe_b.data)         LALFree(clW->amcoe_b.data);
  if (clW->ABCInvD.data)         LALFree(clW->ABCInvD.data);
  if (clW->fkdot4.data)          LALFree(clW->fkdot4.data);
  if (clW->numSFTsV.data)        LALFree(clW->numSFTsV.data);
  if (clW->Fstat.data)           LALFree(clW->Fstat.data);

#if USE_OPENCL_KERNEL
  if (clW->kernel)    clReleaseKernel(*(clW->kernel));
  if (clW->program)   clReleaseProgram(*(clW->program));
  if (clW->cmd_queue) clReleaseCommandQueue (*(clW->cmd_queue));
  if (clW->context)   clReleaseContext (*(clW->context));
#endif

  return;
} /* XLALDestroyCLWorkspace() */



/** Destruction of a ComputeFBufferREAL4V *contents*,
 * i.e. the arrays of multiSSB and multiAMcoeff, while the
 * buffer-container is not freed (which is why it's passed
 * by value and not by reference...) */
void
XLALEmptyComputeFBufferREAL4V ( ComputeFBufferREAL4V *cfbv )
{
  if ( !cfbv )
    return;

  UINT4 numSegments = cfbv->numSegments;
  UINT4 i;
  for (i=0; i < numSegments; i ++ )
    {
      XLALDestroyMultiSSBtimesREAL4 ( cfbv->multiSSB4V[i] );
      XLALDestroyMultiAMCoeffs ( cfbv->multiAMcoefV[i] );
    }

  XLALFree ( cfbv->multiSSB4V );
  cfbv->multiSSB4V = NULL;

  XLALFree ( cfbv->multiAMcoefV );
  cfbv->multiAMcoefV = NULL;

  return;

} /* XLALDestroyComputeFBufferREAL4V() */



/* ---------- pure REAL4 version of sin/cos lookup tables */

/** REAL4 version of sin_cos_LUT()
 *
 * Calculate sin(x) and cos(x) to roughly 1e-7 precision using
 * a lookup-table and Taylor-expansion.
 *
 * NOTE: this function will fail for arguments larger than
 * |x| > INT4_MAX = 2147483647 ~ 2e9 !!!
 *
 * return = 0: OK, nonzero=ERROR
 */
void
sin_cos_LUT_REAL4 (REAL4 *sinx, REAL4 *cosx, REAL4 x)
{
  sin_cos_2PI_LUT_REAL4 ( sinx, cosx, x * OOTWOPI_FLOAT );
  return;
}

/* initialize the global sin/cos lookup table */
void
init_sin_cos_LUT_REAL4 (void)
{
  UINT4 k;
  static int firstCall = 1;

  if ( !firstCall )
    return;

  for (k=0; k <= LUT_RES; k++)
    {
      sinVal[k] = sin( LAL_TWOPI * k / LUT_RES );
      cosVal[k] = cos( LAL_TWOPI * k / LUT_RES );
    }

  firstCall = 0;

  return;

} /* init_sin_cos_LUT_REAL4() */


/* REAL4 version of sin_cos_2PI_LUT() */
void
sin_cos_2PI_LUT_REAL4 (REAL4 *sin2pix, REAL4 *cos2pix, REAL4 x)
{
  REAL4 xt;
  INT4 i0;
  REAL4 d, d2;
  REAL4 ts, tc;

  /* we only need the fractional part of 'x', which is number of cylces,
   * this was previously done using
   *   xt = x - (INT4)x;
   * which is numerically unsafe for x > LAL_INT4_MAX ~ 2e9
   * for saftey we therefore rather use modf(), even if that
   * will be somewhat slower...
   */
  xt = x - (INT8)x;	/* xt in (-1, 1) */

  if ( xt < 0.0 )
    xt += 1.0f;			/* xt in [0, 1 ) */

#ifndef LAL_NDEBUG
  if ( xt < 0.0 || xt > 1.0 )
    {
      XLALPrintError("\nFailed numerics in sin_cos_2PI_LUT_REAL4(): xt = %f not in [0,1)\n\n", xt );
      XLAL_ERROR_VOID ( "sin_cos_2PI_LUT_REAL4()", XLAL_EFPINEXCT );
    }
#endif

  i0 = (INT4)( xt * LUT_RES + 0.5f );	/* i0 in [0, LUT_RES ] */
  d = d2 = LAL_TWOPI * (xt - OO_LUT_RES * i0);
  d2 *= 0.5f * d;

  ts = sinVal[i0];
  tc = cosVal[i0];

  /* use Taylor-expansions for sin/cos around LUT-points */
  (*sin2pix) = ts + d * tc - d2 * ts;
  (*cos2pix) = tc - d * ts - d2 * tc;

  return;

} /* sin_cos_2PI_LUT_REAL4() */

#if USE_OPENCL_KERNEL
/** A helper function to release OpenCL memory objects */
void freeCLMemoryObject (cl_mem *memobj) {
  cl_uint ref_count, i;

  // get an object's reference count
  clGetMemObjectInfo (*memobj, CL_MEM_REFERENCE_COUNT, sizeof(ref_count), &ref_count, NULL);
 
  // decrement reference count of a memory object unless its destroyed
  for (i=0;i<ref_count;i++) clReleaseMemObject(*memobj);
}
#endif
