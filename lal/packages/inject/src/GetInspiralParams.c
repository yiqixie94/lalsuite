/**************************** <lalVerbatim file="GetInspiralParamsCV">
Author: Creighton, T. D.
$Id$
**************************************************** </lalVerbatim> */

/********************************************************** <lalLaTeX>

\providecommand{\lessim}{\stackrel{<}{\scriptstyle\sim}}

\subsection{Module \texttt{GetInspiralParams.c}}
\label{ss:GetInspiralParams.c}

Computes the input parameters for a PPN inspiral.

\subsubsection*{Prototypes}
\vspace{0.1in}
\input{GetInspiralParamsCP}
\index{\texttt{LALGalacticInspiralParams()}}

\subsubsection*{Description}

This function takes a Galactic location and pair of masses from
\verb@*input@ and uses them to set the \verb@PPNParamStruc@ fields
\verb@output->ra@, \verb@output->dec@, \verb@output->mTot@,
\verb@output->eta@, and \verb@output->d@.  The fields
\verb@output->psi@, \verb@output->inc@, and \verb@output->phi@ are set
randomly and uniformly using the random sequence specified by
\verb@*params@; if \verb@*params@=\verb@NULL@ a new sequence is
started internally using the current execution time as a seed.

The other \verb@PPNParamStruc@ input fields are not touched by this
routine, and must be specified externally before generating a waveform
with this structure.

\subsubsection*{Algorithm}

Galactocentric Galactic axial coordinates $\rho$, $z$, and $l_G$ are
transformed to geocentric Galactic Cartesian coordinates:
\begin{eqnarray}
x_e & = & R_e + \rho\cos l_G \;,\nonumber\\
y_e & = & \rho\sin l_G \;,\nonumber\\
z_e & = & z \;,
\end{eqnarray}
where
$$
R_e \approx 8.5\,\mathrm{kpc}
$$
is the distance to the Galactic core (this constant will probably
migrate into \verb@LALConstants.h@ eventually).  These are converted
to geocentric Galactic spherical coordinates:
\begin{eqnarray}
d & = & \sqrt{x_e^2 + y_e^2 + z_e^2} \;,\nonumber\\
b & = & \arcsin\left(\frac{z_e}{d_e}\right) \;,\nonumber\\
l & = & \arctan\!2(y_e,x_e) \;.
\end{eqnarray}
In the calculation of $d$ we factor out the leading order term from
the square root to avoid inadvertent overflow, and check for underflow
in case the location lies on top of the Earth.  The angular
coordinates are then transformed to equatorial celestial coordinates
$\alpha$ and $\delta$ using the routines in \verb@SkyCoordinates.h@.

\subsubsection*{Uses}
\begin{verbatim}
LALGalacticToEquatorial()       LALUniformDeviate()
LALCreateRandomParams()         LALDestroyRandomParams()
\end{verbatim}

\subsubsection*{Notes}

\vfill{\footnotesize\input{GetInspiralParamsCV}}

******************************************************* </lalLaTeX> */

#include <math.h>
#include <lal/LALStdlib.h>
#include <lal/LALConstants.h>
#include <lal/GeneratePPNInspiral.h>
#include <lal/SkyCoordinates.h>

NRCSID( GETINSPIRALPARAMSC, "$Id$" );

#define LAL_DGALCORE_SI (2.62e17) /* Galactic core distance (metres) */

/* <lalVerbatim file="GetInspiralParamsCP"> */
void
LALGetInspiralParams( LALStatus                  *stat,
		      PPNParamStruc              *output,
		      GalacticInspiralParamStruc *input,
		      RandomParams               *params )
{ /* </lalVerbatim> */
  REAL4 x, y, z;  /* geocentric Galactic Cartesian coordinates */
  REAL4 max, d;   /* maximum of x, y, and z, and normalized distance */
  REAL4 psi, phi, inc; /* polarization, phase, and inclination angles */
  REAL4 mTot;          /* total binary mass */
  SkyPosition direction; /* direction to the source */
  RandomParams *localParams = NULL; /* local random parameters pointer */

  INITSTATUS( stat, "LALGetInspiralParams", GETINSPIRALPARAMSC );
  ATTATCHSTATUSPTR( stat );

  /* Make sure parameter structures exist. */
  ASSERT( output, stat, GENERATEPPNINSPIRALH_ENUL,
	  GENERATEPPNINSPIRALH_MSGENUL );
  ASSERT( input, stat, GENERATEPPNINSPIRALH_ENUL,
	  GENERATEPPNINSPIRALH_MSGENUL );

  /* Compute total mass. */
  mTot = input->m1 + input->m2;
  if ( mTot == 0.0 ) {
    ABORT( stat, GENERATEPPNINSPIRALH_EMBAD,
	   GENERATEPPNINSPIRALH_MSGEMBAD );
  }

  /* Compute Galactic geocentric Cartesian coordinates. */
  x = LAL_DGALCORE_SI + input->rho*cos( input->lGal );
  y = input->rho*sin( input->lGal );
  z = input->z;

  /* Compute Galactic geocentric spherical coordinates. */
  max = x;
  if ( y > max )
    max = y;
  if ( z > max )
    max = z;
  if ( max == 0.0 ) {
    ABORT( stat, GENERATEPPNINSPIRALH_EDBAD,
	   GENERATEPPNINSPIRALH_MSGEDBAD );
  }
  x /= max;
  y /= max;
  z /= max;
  d = sqrt( x*x + y*y + z*z );
  direction.latitude = asin( z/d );
  direction.longitude = atan2( y, x );
  direction.system = COORDINATESYSTEM_GALACTIC;

  /* Compute equatorial coordinates. */
  TRY( LALGalacticToEquatorial( stat->statusPtr, &direction,
				&direction ), stat );
  output->ra = direction.longitude;
  output->dec = direction.latitude;
  output->d = max*d;

  /* If we haven't been given a random sequence, generate one. */
  if ( params )
    localParams = params;
  else {
    TRY( LALCreateRandomParams( stat->statusPtr, &localParams, 0 ),
	 stat );
  }

  /* Compute random inclination and polarization angle. */
  LALUniformDeviate( stat->statusPtr, &psi, localParams );
  if ( params )
    CHECKSTATUSPTR( stat );
  else
    BEGINFAIL( stat )
      TRY( LALDestroyRandomParams( stat->statusPtr, &localParams ),
	   stat );
      ENDFAIL( stat );
  LALUniformDeviate( stat->statusPtr, &phi, localParams );
  if ( params )
    CHECKSTATUSPTR( stat );
  else
    BEGINFAIL( stat )
      TRY( LALDestroyRandomParams( stat->statusPtr, &localParams ),
	   stat );
      ENDFAIL( stat );
  LALUniformDeviate( stat->statusPtr, &inc, localParams );
  if ( params )
    CHECKSTATUSPTR( stat );
  else
    BEGINFAIL( stat )
      TRY( LALDestroyRandomParams( stat->statusPtr, &localParams ),
	   stat );
      ENDFAIL( stat );
  output->psi = LAL_TWOPI*psi;
  output->phi = LAL_TWOPI*phi;
  output->inc = LAL_TWOPI*inc;

  /* Set output masses. */
  output->mTot = mTot;
  output->eta = (input->m1/mTot)*(input->m2/mTot);

  /* Clean up and exit. */
  if ( !params ) {
    TRY( LALDestroyRandomParams( stat->statusPtr, &localParams ),
	 stat );
  }
  DETATCHSTATUSPTR( stat );
  RETURN( stat );
}
