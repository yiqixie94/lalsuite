/*-----------------------------------------------------------------------
 *
 * File Name: TestHoughMap.c
 *
 * Authors: Sintes, A.M., 
 *
 * Revision: $Id$
 *
 * History:   Created by Sintes June 25, 2001
 *            Modified...  "   August 6, 2001
 *
 *-----------------------------------------------------------------------
 */

/*
 * 1.  An author and Id block
 */

/************************************ <lalVerbatim file="TestHoughMapCV">
Author: Sintes, A. M. 
$Id$
************************************* </lalVerbatim> */

/*
 * 2. Commented block with the documetation of this module
 */


/* ************************************************ <lalLaTeX>

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
\subsection{Program \ \texttt{TestHoughMap.c}}
\label{s:TestHoughMap.c}

Tests the construction of Hough maps.

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
\subsubsection*{Usage}
\begin{verbatim}
TestHoughMap [-d debuglevel] [-o outfile] [-f f0] [-p alpha delta]
\end{verbatim}

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
\subsubsection*{Description}

%TO BE CHANGED


Similar to the previous ones, this program generates a patch grid, calculates 
the parameters needed for
building a {\sc lut}, and  builds the {\sc lut}. Then, given a peak-gram 
 constructs a {\sc phmd} at a
certain frequency (shifted from the frequency at which the {\sc lut} was built).
The sky patch is set at the south pole, 
no spin-down parameters are assumed for the demodulation and
 every third  peak in the spectrum is selected. The peak-gram frequency interval
 is large enough to ensure compatibility with the {\sc lut} and the frequency of
 the {\sc phmd}. 
 
 Moreover, this program initializes a Hough map {\sc ht} 
 and the Hough
 map derivative space {\sc hd}, adds one {\sc phmd} into the Hough map
 derivative {\sc hd},
 constructs the total Hough map {\sc ht} by integrating the {\sc hd},
and outputs the {\sc ht} into a file.  \\

 By default, running this program with no arguments simply tests the subroutines,
producing an output file called \verb@OutHough.asc@.  All default parameters are set from
\verb@#define@d constants.\\


The \verb@-d@ option sets the debug level to the specified value
\verb@debuglevel@.  The \verb@-o@ flag tells the program to print the partial Hough map
derivative  to the specified data file \verb@outfile@.  The
\verb@-f@ option sets the intrinsic frequency \verb@f0@ at which build the {\sc
lut}.   The \verb@-p@ option sets the velocity orientation of the detector
\verb@alpha@, \verb@delta@ (in radians). 




%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
\subsubsection*{Exit codes}
\vspace{0.1in}
\input{TESTHOUGHMAPCErrorTable}

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
\subsubsection*{Uses}
\begin{verbatim}
LALHOUGHPatchGrid()
LALHOUGHParamPLUT()
LALHOUGHConstructPLUT()
LALHOUGHPeak2PHMD()
LALHOUGHInitializeHT()
LALHOUGHInitializeHD()
LALHOUGHAddPHMD2HD()
LALHOUGHIntegrHD2HT()
LALPrintError()
LALMalloc()
LALFree()
LALCheckMemoryLeaks()
\end{verbatim}

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

\subsubsection*{Notes}

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
\vfill{\footnotesize\input{TestHoughMapCV}}

********************************************   </lalLaTeX> */



#include <lal/HoughMap.h>


NRCSID (TESTHOUGHMAPC, "$Id$");


/* Error codes and messages */

/************** <lalErrTable file="TESTHOUGHMAPCErrorTable"> */
#define TESTHOUGHMAPC_ENORM 0
#define TESTHOUGHMAPC_ESUB  1
#define TESTHOUGHMAPC_EARG  2
#define TESTHOUGHMAPC_EBAD  3
#define TESTHOUGHMAPC_EFILE 4

#define TESTHOUGHMAPC_MSGENORM "Normal exit"
#define TESTHOUGHMAPC_MSGESUB  "Subroutine failed"
#define TESTHOUGHMAPC_MSGEARG  "Error parsing arguments"
#define TESTHOUGHMAPC_MSGEBAD  "Bad argument values"
#define TESTHOUGHMAPC_MSGEFILE "Could not create output file"
/******************************************** </lalErrTable> */


/* Default parameters. */

INT4 lalDebugLevel=0;

#define F0 500.0          /*  frequency to build the LUT. */
#define TCOH 100000.0     /*  time baseline of coherent integration. */
#define DF    (1./TCOH)   /*  frequency  resolution. */
#define ALPHA 0.0
#define DELTA 0.0
#define MWR 1             /*.minWidthRatio */
#define FILEOUT "OutHough.asc"      /* file output */

/* Usage format string. */

#define USAGE "Usage: %s [-d debuglevel] [-o outfile] [-f f0] [-p alpha delta]\n"

/*********************************************************************/
/* Macros for printing errors & testing subroutines (from Creighton) */
/*********************************************************************/

#define ERROR( code, msg, statement )                                \
do {                                                                 \
  if ( lalDebugLevel & LALERROR )                                    \
    LALPrintError( "Error[0] %d: program %s, file %s, line %d, %s\n" \
                   "        %s %s\n", (code), *argv, __FILE__,       \
              __LINE__, TESTHOUGHMAPC, statement ? statement :  \
                   "", (msg) );                                      \
} while (0)

#define INFO( statement )                                            \
do {                                                                 \
  if ( lalDebugLevel & LALINFO )                                     \
    LALPrintError( "Info[0]: program %s, file %s, line %d, %s\n"     \
                   "        %s\n", *argv, __FILE__, __LINE__,        \
              TESTHOUGHMAPC, (statement) );                     \
} while (0)

#define SUB( func, statusptr )                                       \
do {                                                                 \
  if ( (func), (statusptr)->statusCode ) {                           \
    ERROR( TESTHOUGHMAPC_ESUB, TESTHOUGHMAPC_MSGESUB,      \
           "Function call \"" #func "\" failed:" );                  \
    return TESTHOUGHMAPC_ESUB;                                  \
  }                                                                  \
} while (0)
/******************************************************************/

/* A global pointer for debugging. */
#ifndef NDEBUG
char *lalWatch;
#endif


/* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>><<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< */
/* vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv------------------------------------ */
int main(int argc, char *argv[]){ 

  static LALStatus       status;  /* LALStatus pointer */
  static HOUGHptfLUT     lut;     /* the Look Up Table */
  static HOUGHPatchGrid  patch;   /* Patch description */
  static HOUGHParamPLUT  parLut;  /* parameters needed to build lut  */
  static HOUGHResolutionPar parRes;
  static HOUGHDemodPar   parDem;  /* demodulation parameters */
  static HOUGHPeakGram   pg;
  static HOUGHphmd       phmd; /* the partial Hough map derivative */
  static HOUGHMapDeriv   hd;   /* the Hough map derivative */
  static HOUGHMapTotal   ht;   /* the total Hough map */
  /* ------------------------------------------------------- */

  UINT2  maxNBins, maxNBorders;
  UINT2  xSideMax, ySideMax;
  
  INT8   f0Bin;           /* freq. bin to construct LUT */
  UINT2  xSide, ySide;
  
  CHAR *fname = NULL;               /* The output filename */
  FILE *fp=NULL;                    /* Output file */

  INT4 arg;                         /* Argument counter */
  INT4 i,j;                       /* Index counter, etc */
  UINT4 k;                       
  
  REAL8 f0, alpha, delta, veloMod;


  /************************************************************/
  /* Set up the default parameters. */
  /************************************************************/
  
  maxNBins    = MAX_N_BINS;     /* from LUT.h */
  maxNBorders = MAX_N_BORDERS;  /* from LUT.h */
  xSideMax    = SIDEX;   /* from LUT.h */
  ySideMax    = SIDEY;   /* from LUT.h */

  ht.map = NULL;
  hd.map = NULL;

  f0 =  F0;
  parRes.f0 =  F0;
  parRes.deltaF = DF;
  parRes.minWidthRatio = MWR;

  f0Bin = F0*TCOH;

  parDem.deltaF = DF;
  parDem.skyPatch.alpha = 0.0;
  parDem.skyPatch.delta = -LAL_PI_2; 

  alpha = ALPHA;
  delta = DELTA;
  veloMod = VTOT;
 
  /**********************************************************/
  /* Memory allocation and other settings */
  /*********************************************************/
  
  lut.maxNBins = maxNBins; 
  lut.maxNBorders = maxNBorders;
  lut.border = 
         (HOUGHBorder *)LALMalloc(maxNBorders*sizeof(HOUGHBorder));
  lut.bin = 
         (HOUGHBin2Border *)LALMalloc(maxNBins*sizeof(HOUGHBin2Border));

  phmd.maxNBorders = maxNBorders;	 
  phmd.leftBorderP = 
       (HOUGHBorder **)LALMalloc(maxNBorders*sizeof(HOUGHBorder *));
  phmd.rightBorderP = 
       (HOUGHBorder **)LALMalloc(maxNBorders*sizeof(HOUGHBorder *));
  
  patch.xSideMax = xSideMax;
  patch.ySideMax = ySideMax;
  patch.xCoor = NULL;
  patch.yCoor = NULL;
  patch.xCoor = (REAL8 *)LALMalloc(xSideMax*sizeof(REAL8));
  patch.yCoor = (REAL8 *)LALMalloc(ySideMax*sizeof(REAL8));

	 
  /********************************************************/  
  /* Parse argument list.  i stores the current position. */
  /********************************************************/  
  arg = 1;
  while ( arg < argc ) {
    /* Parse debuglevel option. */
    if ( !strcmp( argv[arg], "-d" ) ) {
      if ( argc > arg + 1 ) {
        arg++;
        lalDebugLevel = atoi( argv[arg++] );
      } else {
        ERROR( TESTHOUGHMAPC_EARG, TESTHOUGHMAPC_MSGEARG, 0 );
        LALPrintError( USAGE, *argv );
        return TESTHOUGHMAPC_EARG;
      }
    }
    /* Parse output file option. */
    else if ( !strcmp( argv[arg], "-o" ) ) {
      if ( argc > arg + 1 ) {
        arg++;
        fname = argv[arg++];
      } else {
        ERROR( TESTHOUGHMAPC_EARG, TESTHOUGHMAPC_MSGEARG, 0 );
        LALPrintError( USAGE, *argv );
        return TESTHOUGHMAPC_EARG;
      }
    }
    /* Parse frequency option. */
    else if ( !strcmp( argv[arg], "-f" ) ) {
      if ( argc > arg + 1 ) {
        arg++;
	f0 = atof(argv[arg++]);
	parRes.f0 =  f0;
	f0Bin = f0*TCOH;      
      } else {
        ERROR( TESTHOUGHMAPC_EARG, TESTHOUGHMAPC_MSGEARG, 0 );
        LALPrintError( USAGE, *argv );
        return TESTHOUGHMAPC_EARG;
      }
    }
    /* Parse velocity position options. */
    else if ( !strcmp( argv[arg], "-p" ) ) {
      if ( argc > arg + 2 ) {
        arg++;
	alpha = atof(argv[arg++]);
	delta = atof(argv[arg++]);
      } else {
        ERROR( TESTHOUGHMAPC_EARG, TESTHOUGHMAPC_MSGEARG, 0 );
        LALPrintError( USAGE, *argv );
        return TESTHOUGHMAPC_EARG;
      }
    }
    /* Unrecognized option. */
    else {
      ERROR( TESTHOUGHMAPC_EARG, TESTHOUGHMAPC_MSGEARG, 0 );
      LALPrintError( USAGE, *argv );
      return TESTHOUGHMAPC_EARG;
    }
  } /* End of argument parsing loop. */
  /******************************************************************/

  if ( f0 < 0 ) {
    ERROR( TESTHOUGHMAPC_EBAD, TESTHOUGHMAPC_MSGEBAD, "freq<0:" );
    LALPrintError( USAGE, *argv  );
    return TESTHOUGHMAPC_EBAD;
  }


  /******************************************************************/
  /* create patch grid */
  /******************************************************************/
  SUB( LALHOUGHPatchGrid( &status, &patch, &parRes ),  &status );
  
  xSide = patch.xSide;
  ySide = patch.ySide;

  /******************************************************************/
  /* memory allocation again and settings */
  /******************************************************************/
  ht.xSide = xSide;
  ht.ySide = ySide;
  ht.map   = (HoughTT *)LALMalloc(xSide*ySide*sizeof(HoughTT));
  
  hd.xSide = xSide;
  hd.ySide = ySide;
  hd.map   = (HoughDT *)LALMalloc((xSide+1)*ySide*sizeof(HoughDT));

  phmd.ySide = ySide;
  phmd.firstColumn = NULL;
  phmd.firstColumn = (UCHAR *)LALMalloc(ySide*sizeof(UCHAR));


  for (i=0; i<maxNBorders; ++i){
    lut.border[i].ySide = ySide;
    lut.border[i].xPixel = (COORType *)LALMalloc(ySide*sizeof(COORType));
  }
  
  
  /******************************************************************/
  /* Case: no spins, patch at south pole */
  /************************************************************/

  parDem.veloC.x = veloMod*cos(delta)*cos(alpha);
  parDem.veloC.y = veloMod*cos(delta)*sin(alpha);
  parDem.veloC.z = veloMod*sin(delta);
    
  parDem.positC.x = 0.0; 
  parDem.positC.y = 0.0; 
  parDem.positC.z = 0.0; 
  parDem.timeDiff = 0.0;
  parDem.spin.length = 0;
  parDem.spin.data = NULL;
  
 
  /******************************************************************/
  /* Frequency-bin  of the Partial Hough Map*/
  /******************************************************************/

  phmd.fBin = f0Bin + 21; /* a bit shifted from the LUT */

  /******************************************************************/
  /* A Peakgram for testing                                         */
  /******************************************************************/
  pg.deltaF = DF;
  pg.fBinIni = (phmd.fBin) - MAX_N_BINS ;
  pg.fBinFin = (phmd.fBin)+ 5*MAX_N_BINS;
  pg.length = MAX_N_BINS; /* could be much smaller */
  pg.peak = NULL;
  pg.peak = (INT4 *)LALMalloc( (pg.length) * sizeof(INT4));

  for (k=0; k< pg.length; ++k){  pg.peak[k] = 3*k;  } /* a test */


  /******************************************************************/
  /* calculate parameters needed for buiding the LUT */
  /******************************************************************/
  SUB( LALHOUGHParamPLUT( &status, &parLut, f0Bin, &parDem ),  &status );

  /******************************************************************/
  /* build the LUT */
  /******************************************************************/
  SUB( LALHOUGHConstructPLUT( &status, &lut, &patch, &parLut ), &status );

  /******************************************************************/
  /* build a PHMD from a peakgram and LUT  */
  /******************************************************************/

  SUB( LALHOUGHPeak2PHMD( &status, &phmd, &lut, &pg ), &status );
 
  /******************************************************************/
  /* initializing the Hough map space */
  /******************************************************************/

  SUB( LALHOUGHInitializeHT( &status, &ht, &patch ), &status ); 

  /******************************************************************/
  /* initializing the Hough map derivative space */
  /******************************************************************/

  SUB( LALHOUGHInitializeHD( &status, &hd), &status ); 

  /******************************************************************/
  /* sum a partial-HMD into a HD */
  /******************************************************************/
  SUB( LALHOUGHAddPHMD2HD( &status, &hd, &phmd ), &status );

  /******************************************************************/
  /* construction of a total Hough map: integration of a HM-deriv.  */
  /******************************************************************/

  SUB( LALHOUGHIntegrHD2HT( &status, &ht, &hd ), &status ); 

  /******************************************************************/
  /* printing the results into a particular file                    */
  /* if the -o option was given, or into  FILEOUT                   */ 
  /******************************************************************/

  if ( fname ) {
    fp = fopen( fname, "w" );
  } else {
    fp = fopen( FILEOUT , "w" );
  }

  if ( !fp ){
    ERROR( TESTHOUGHMAPC_EFILE, TESTHOUGHMAPC_MSGEFILE, 0 );
    return TESTHOUGHMAPC_EFILE;
  }

 
  for(j=ySide-1; j>=0; --j){
    for(i=0;i<xSide;++i){
      fprintf( fp ," %d", ht.map[j*xSide +i]);
      fflush( fp );
    }
    fprintf( fp ," \n");
    fflush( fp );
  }
  
  fclose( fp );


  /******************************************************************/
  /* Free memory and exit */
  /******************************************************************/

  LALFree(pg.peak);
  
  for (i=0; i<maxNBorders; ++i){
    LALFree( lut.border[i].xPixel);
  } 
   
  LALFree( lut.border);
  LALFree( lut.bin);
    
  LALFree( phmd.leftBorderP);
  LALFree( phmd.rightBorderP);
  LALFree( phmd.firstColumn);
  
  LALFree( ht.map);
  LALFree( hd.map);
  
  LALFree( patch.xCoor);
  LALFree( patch.yCoor);
  
  LALCheckMemoryLeaks(); 

  INFO( TESTHOUGHMAPC_MSGENORM );
  return TESTHOUGHMAPC_ENORM;
}

/* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>><<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< */
