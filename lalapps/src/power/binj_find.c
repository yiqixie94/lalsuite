#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <lal/LALStdlib.h>
#include <lal/LALConstants.h>
#include <lal/Date.h>
#include <lal/LIGOLwXML.h>
#include <lal/LIGOLwXMLRead.h>
#include <lal/LIGOMetadataUtils.h>
#include <lalapps.h>

RCSID("$Id$");

#define MAXSTR 2048
#define PLAYGROUND_START_TIME 729273613
#define PLAYGROUND_INTERVAL 6370
#define PLAYGROUND_LENGTH 600

/* Usage format string. */
#define USAGE "Usage: %s --input infile  --injfile injectionfile \
     --injmadefile filename --detsnglfile filename --injfoundfile filename \
     [--noplayground] [--help]\n"

#define BINJ_FIND_EARG   1
#define BINJ_FIND_EROW   2
#define BINJ_FIND_EFILE  3

#define BINJ_FIND_MSGEARG   "Error parsing arguments"
#define BINJ_FIND_MSGROW    "Error reading row from XML table"
#define BINJ_FIND_MSGEFILE  "Could not open file"

#define TRUE  1
#define FALSE 0

struct options_t {
	int verbose;

	/* central_freq threshold */
	INT4 maxCentralfreqFlag;
	REAL4 maxCentralfreq;
	INT4 minCentralfreqFlag;
	REAL4 minCentralfreq;
};

/*
 * Set the options to their defaults.
 */

static void set_option_defaults(struct options_t *options)
{
	options->verbose = 0;

	options->maxCentralfreqFlag = 0;
	options->maxCentralfreq = 0.0;
	options->minCentralfreqFlag = 0;
	options->minCentralfreq = 0.0;
}

/*
 * Read a line of text from a file, striping the newline character if read.
 * Return an empty string on any error.
 */

static int getline(char *line, int max, FILE *fpin)
{
	char *end;

	if(!fgets(line, max, fpin))
		line[0] = '\0';
	end = strchr(line, '\n');
	if(end)
		*end = '\0';
	return(strlen(line));
}


/*
 * Function tests if the file contains any playground data.
 * FIXME: check if doing S2 or S3
 */

static int isPlayground(INT4 gpsStart, INT4 gpsEnd)
{
	INT4 runStart = 729273613;
	INT4 playInterval = 6370;
	INT4 playLength = 600;
	INT4 segStart, segEnd, segMiddle;

	segStart = (gpsStart - runStart) % playInterval;
	segEnd = (gpsEnd - runStart) % playInterval;
	segMiddle = gpsStart + (INT4) (0.5 * (gpsEnd - gpsStart));
	segMiddle = (segMiddle - runStart) % playInterval;

	return(segStart < playLength || segEnd < playLength || segMiddle < playLength);
}


/*
 * Read injection data.
 *
 * The input file should contain a list of injection XML files, one file name
 * per line.
 */

static SimBurstTable *read_injection_list(LALStatus *stat, char *filename, INT4 start_time, INT4 end_time, struct options_t options)
{
	FILE *infile;
	char line[MAXSTR];
	SimBurstTable *list = NULL;
	SimBurstTable **addPoint = &list;

	if (options.verbose)
		fprintf(stdout, "Reading in SimBurst Table\n");

	if (!(infile = fopen(filename, "r")))
		LALPrintError("Could not open input file\n");

	while (getline(line, MAXSTR, infile)) {
		if (options.verbose)
			fprintf(stderr, "Working on file %s\n", line);

		LAL_CALL(LALSimBurstTableFromLIGOLw(stat, addPoint, line, start_time, end_time), stat);

		while (*addPoint)
			addPoint = &(*addPoint)->next;
	}

	fclose(infile);

	return(list);
}


/*
 * Trim a SimBurstTable.
 */

static int keep_this_injection(SimBurstTable *injection, struct options_t options)
{
	if (options.minCentralfreqFlag && !(injection->freq > options.minCentralfreq))
		return(FALSE);
	if (options.maxCentralfreqFlag && !(injection->freq < options.maxCentralfreq))
		return(FALSE);
	return(TRUE);
}

static SimBurstTable *free_this_injection(SimBurstTable *injection)
{
	SimBurstTable *next = injection ? injection->next : NULL;
	LALFree(injection);
	return(next);
}

static SimBurstTable *trim_injection_list(SimBurstTable *injection, struct options_t options)
{
	SimBurstTable *head, *prev;

	while(injection && !keep_this_injection(injection, options))
		injection = free_this_injection(injection);
	head = injection;

	/* FIXME: don't check the first event again */
	for(prev = injection; injection; injection = prev->next) {
		if(keep_this_injection(injection, options))
			prev = injection;
		else
			prev->next = free_this_injection(injection);
	}

	return(head);
}


/*
 * Entry point.
 */

int main(int argc, char **argv)
{
	static LALStatus stat;
	FILE *fpin = NULL;
	BOOLEAN playground = FALSE;
	BOOLEAN noplayground = FALSE;

	INT4 sort = FALSE;
	INT4 pass = TRUE;
	size_t len = 0;
	INT4 ndetected = 0;
	INT4 ninjected = 0;
	const long S2StartTime = 729273613;	/* Feb 14 2003 16:00:00 UTC */
	const long S2StopTime = 734367613;	/* Apr 14 2003 15:00:00 UTC */

	static struct options_t options;

	/* filenames */
	CHAR *inputFile = NULL;
	CHAR *injectionFile = NULL;
	CHAR *injmadeFile = NULL;
	CHAR *injFoundFile = NULL;
	CHAR *outSnglFile = NULL;

	/* times of comparison */
	INT4 gpsStartTime = S2StartTime;
	INT4 gpsEndTime = S2StopTime;
	INT8 injPeakTime = 0;
	INT8 burstStartTime = 0;

	/* confidence threshold */
	INT4 maxConfidenceFlag = 0;
	REAL4 maxConfidence = 0.0;

	/* duration thresholds */
	INT4 minDurationFlag = 0;
	REAL4 minDuration = 0.0;
	INT4 maxDurationFlag = 0;
	REAL4 maxDuration = 0.0;

	/* bandwidth threshold */
	INT4 maxBandwidthFlag = 0;
	REAL4 maxBandwidth = 0.0;

	/* amplitude threshold */
	INT4 maxAmplitudeFlag = 0;
	REAL4 maxAmplitude = 0.0;
	INT4 minAmplitudeFlag = 0;
	REAL4 minAmplitude = 0.0;

	/* snr threshold */
	INT4 maxSnrFlag = 0;
	REAL4 maxSnr = 0.0;
	INT4 minSnrFlag = 0;
	REAL4 minSnr = 0.0;

	CHAR line[MAXSTR];

	/* search summary */
	SearchSummaryTable *searchSummary = NULL;

	/* triggers */
	SnglBurstTable *tmpEvent = NULL, *currentEvent = NULL, *prevEvent = NULL;
	SnglBurstTable burstEvent, *burstEventList = NULL, *outEventList = NULL;
	SnglBurstTable *detEventList = NULL, *detTrigList = NULL, *prevTrig = NULL;

	/* injections */
	SimBurstTable *simBurstList = NULL, *outSimdummyList = NULL;
	SimBurstTable *currentSimBurst = NULL, *tmpSimBurst = NULL;
	SimBurstTable *injSimList = NULL, *prevSimBurst = NULL;
	SimBurstTable *injFoundList = NULL, *outSimList = NULL;
	SimBurstTable *tmpInjFound = NULL, *prevInjFound = NULL;

	/* comparison parameters */
	SnglBurstAccuracy accParams;

	/* Table outputs */
	MetadataTable myTable;
	LIGOLwXMLStream xmlStream;

  /*******************************************************************
   * BEGIN PARSE ARGUMENTS (inarg stores the current position)        *
   *******************************************************************/
	set_option_defaults(&options);
	int c;
	while (1) {
		/* getopt arguments */
		static struct option long_options[] = {
			/* these options set a flag */
			{"verbose", no_argument, &options.verbose, 1},
			/* parameters which determine the output xml file */
			{"input", required_argument, 0, 'a'},
			{"injfile", required_argument, 0, 'b'},
			{"injmadefile", required_argument, 0, 'c'},
			{"max-confidence", required_argument, 0, 'd'},
			{"gps-start-time", required_argument, 0, 'e'},
			{"gps-end-time", required_argument, 0, 'f'},
			{"min-centralfreq", required_argument, 0, 'g'},
			{"max-centralfreq", required_argument, 0, 'h'},
			{"injfoundfile", required_argument, 0, 'j'},
			{"playground", no_argument, 0, 'k'},
			{"noplayground", no_argument, 0, 'n'},
			{"help", no_argument, 0, 'o'},
			{"sort", no_argument, 0, 'p'},
			{"detsnglfile", required_argument, 0, 'q'},
			{0, 0, 0, 0}
		};
		/* getopt_long stores the option index here. */
		int option_index = 0;

		c = getopt_long(argc, argv, "a:c:d:e:f:g:h:i:", long_options, &option_index);

		/* detect the end of the options */
		if (c == -1)
			break;

		switch (c) {
		case 0:
			/* if this option set a flag, do nothing else now */
			if (long_options[option_index].flag != 0)
				break;
			else {
				fprintf(stderr, "error parsing option %s with argument %s\n", long_options[option_index].name, optarg);
				exit(1);
			}
			break;

		case 'a':
			/* create storage for the input file name */
			len = strlen(optarg) + 1;
			inputFile = (CHAR *) calloc(len, sizeof(CHAR));
			memcpy(inputFile, optarg, len);
			break;

		case 'b':
			/* create storage for the injection file name */
			len = strlen(optarg) + 1;
			injectionFile = (CHAR *) calloc(len, sizeof(CHAR));
			memcpy(injectionFile, optarg, len);
			break;

		case 'c':
			/* create storage for the output file name */
			len = strlen(optarg) + 1;
			injmadeFile = (CHAR *) calloc(len, sizeof(CHAR));
			memcpy(injmadeFile, optarg, len);
			break;

		case 'd':
			/* the confidence must be smaller than this number */
			maxConfidenceFlag = 1;
			maxConfidence = atof(optarg);
			break;

		case 'e':
			/* only events with duration greater than this are selected */
			gpsStartTime = atoi(optarg);
			break;

		case 'f':
			/* only events with duration less than this are selected */
			gpsEndTime = atoi(optarg);
			break;

		case 'g':
			/* only events with centralfreq greater than this are selected */
			options.minCentralfreqFlag = 1;
			options.minCentralfreq = atof(optarg);
			break;

		case 'h':
			/* only events with centralfreq less than this are selected */
			options.maxCentralfreqFlag = 1;
			options.maxCentralfreq = atof(optarg);
			break;

		case 'j':
			/* create storage for the output file name */
			len = strlen(optarg) + 1;
			injFoundFile = (CHAR *) calloc(len, sizeof(CHAR));
			memcpy(injFoundFile, optarg, len);
			break;

		case 'k':
			/* restrict to the playground data */
			playground = TRUE;
			break;

		case 'n':
			/* don't restrict to the playground data */
			noplayground = TRUE;
			break;

		case 'o':
			/* print help */
			LALPrintError(USAGE, *argv);
			exit(BINJ_FIND_EARG);

		case 'p':
			/* sort the events in time */
			sort = TRUE;
			break;


		case 'q':
			/* create storage for the output file name */
			len = strlen(optarg) + 1;
			outSnglFile = (CHAR *) calloc(len, sizeof(CHAR));
			memcpy(outSnglFile, optarg, len);
			break;

		default:
			exit(BINJ_FIND_EARG);
		}
	}

	if (optind < argc) {
		fprintf(stderr, "extraneous command line arguments:\n");
		while (optind < argc)
			fprintf(stderr, "%s\n", argv[optind++]);
		exit(1);
	}

	if (!injmadeFile || !inputFile || !injectionFile || !outSnglFile) {
		LALPrintError("Input file, injection file, output trig. file and output inj. file " "            names must be specified\n");
		exit(BINJ_FIND_EARG);
	}


  /*******************************************************************
   * END PARSE ARGUMENTS                                             *
   *******************************************************************/


  /*******************************************************************
   * initialize things
   *******************************************************************/
	lal_errhandler = LAL_ERR_EXIT;
	set_debug_level("1");
	memset(&burstEvent, 0, sizeof(SnglBurstTable));
	memset(&xmlStream, 0, sizeof(LIGOLwXMLStream));
	xmlStream.fp = NULL;

  /*******************************************************************
   * Read and trim the injection list                                *
   *******************************************************************/

	simBurstList = read_injection_list(&stat, injectionFile, gpsStartTime, gpsEndTime, options);
	simBurstList = trim_injection_list(simBurstList, options);

  /*****************************************************************
   * OPEN FILE WITH LIST OF Trigger XML FILES (one file per line)
   ****************************************************************/
	if (!(fpin = fopen(inputFile, "r")))
		LALPrintError("Could not open input file\n");

  /*****************************************************************
   * loop over the xml files
   *****************************************************************/
	tmpSimBurst = NULL;
	tmpSimBurst = outSimdummyList;

	/* convert time of first injection in the list to INT8 */
	LAL_CALL(LALGPStoINT8(&stat, &injPeakTime, &(tmpSimBurst->l_peak_time)), &stat);

	currentEvent = tmpEvent = burstEventList = NULL;
	while (getline(line, MAXSTR, fpin)) {
		INT4 tmpStartTime = 0, tmpEndTime = 0;

		if (options.verbose)
			fprintf(stderr, "Working on file %s\n", line);

    /**************************************************************
     * Get the searchsummary info to create the LIST of INJECTIONS 
     * corresponding to jobs that actually succeeded. 
     **************************************************************/

		SearchSummaryTableFromLIGOLw(&searchSummary, line);
		tmpStartTime = searchSummary->in_start_time.gpsSeconds;
		tmpEndTime = searchSummary->in_end_time.gpsSeconds;

		/*
		 * Go to the injection in the list which is made after the  
		 * current job started
		 */
		while (tmpStartTime * 1000000000LL > injPeakTime) {
			tmpSimBurst = tmpSimBurst->next;
			LAL_CALL(LALGPStoINT8(&stat, &injPeakTime, &(tmpSimBurst->l_peak_time)), &stat);
		}

		/*
		 * Link the injections which lie inside the 
		 * duration of the current job
		 */

		while ((tmpStartTime * 1000000000LL < injPeakTime)
		       && (injPeakTime < tmpEndTime * 1000000000LL)
		       && (tmpSimBurst->next != NULL)) {
			if (outSimList == NULL) {
				outSimList = currentSimBurst = (SimBurstTable *) LALCalloc(1, sizeof(SimBurstTable));
				prevSimBurst = currentSimBurst;
			} else {
				currentSimBurst = (SimBurstTable *) LALCalloc(1, sizeof(SimBurstTable));
				prevSimBurst->next = currentSimBurst;
			}
			memcpy(currentSimBurst, tmpSimBurst, sizeof(SimBurstTable));
			prevSimBurst = currentSimBurst;
			currentSimBurst = currentSimBurst->next = NULL;
			tmpSimBurst = tmpSimBurst->next;

			LAL_CALL(LALGPStoINT8(&stat, &injPeakTime, &(tmpSimBurst->l_peak_time)), &stat);
		}


		while (searchSummary) {
			SearchSummaryTable *thisEvent;
			thisEvent = searchSummary;
			searchSummary = searchSummary->next;
			LALFree(thisEvent);
		}
		searchSummary = NULL;

		/* Now the events themselves */
		LAL_CALL(LALSnglBurstTableFromLIGOLw(&stat, &tmpEvent, line), &stat);

		/* connect results to linked list */
		if (currentEvent == NULL)
			burstEventList = currentEvent = tmpEvent;
		else
			currentEvent->next = tmpEvent;

		/* move to the end of the linked list for next input file */
		while (currentEvent->next != NULL)
			currentEvent = currentEvent->next;
		tmpEvent = currentEvent->next;
	}

  /****************************************************************
   * do any requested cuts
   ***************************************************************/
	tmpEvent = burstEventList;
	while (tmpEvent) {

		/* check the confidence */
		if (maxConfidenceFlag && !(tmpEvent->confidence < maxConfidence))
			pass = FALSE;

		/* check min duration */
		if (minDurationFlag && !(tmpEvent->duration > minDuration))
			pass = FALSE;

		/* check max duration */
		if (maxDurationFlag && !(tmpEvent->duration < maxDuration))
			pass = FALSE;

		/* check min centralfreq */
		if (options.minCentralfreqFlag && !(tmpEvent->central_freq > options.minCentralfreq))
			pass = FALSE;

		/* check max centralfreq */
		if (options.maxCentralfreqFlag && !(tmpEvent->central_freq < options.maxCentralfreq))
			pass = FALSE;

		/* check max bandwidth */
		if (maxBandwidthFlag && !(tmpEvent->bandwidth < maxBandwidth))
			pass = FALSE;

		/* check min amplitude */
		if (minAmplitudeFlag && !(tmpEvent->amplitude > minAmplitude))
			pass = FALSE;

		/* check max amplitude */
		if (maxAmplitudeFlag && !(tmpEvent->amplitude < maxAmplitude))
			pass = FALSE;

		/* check min snr */
		if (minSnrFlag && !(tmpEvent->snr > minSnr))
			pass = FALSE;

		/* check max snr */
		if (maxSnrFlag && !(tmpEvent->snr < maxSnr))
			pass = FALSE;

		/* check if trigger starts in playground */
		if (playground && !(isPlayground(tmpEvent->start_time.gpsSeconds, tmpEvent->start_time.gpsSeconds)))
			pass = FALSE;

		/* set it for output if it passes */
		if (pass) {
			if (outEventList == NULL) {
				outEventList = currentEvent = (SnglBurstTable *) LALCalloc(1, sizeof(SnglBurstTable));
				prevEvent = currentEvent;
			} else {
				currentEvent = (SnglBurstTable *) LALCalloc(1, sizeof(SnglBurstTable));
				prevEvent->next = currentEvent;
			}
			memcpy(currentEvent, tmpEvent, sizeof(SnglBurstTable));
			prevEvent = currentEvent;
			currentEvent = currentEvent->next = NULL;
		}
		tmpEvent = tmpEvent->next;
		pass = TRUE;
	}

  /*****************************************************************
   * sort the remaining triggers
   *****************************************************************/
	LAL_CALL(LALSortSnglBurst(&stat, &(outEventList), LALCompareSnglBurstByTime), &stat);


  /*****************************************************************
   * first event in list
   *****************************************************************/

	currentSimBurst = outSimList;
	currentEvent = detEventList = outEventList;

	while (currentSimBurst != NULL) {
		/* check if the injection is made in the playground */
		if ((playground && !(isPlayground(currentSimBurst->l_peak_time.gpsSeconds, currentSimBurst->l_peak_time.gpsSeconds))) == 0) {
			ninjected++;

			/* write the injected signals to an output file */
			if (injSimList == NULL) {
				injSimList = tmpSimBurst = (SimBurstTable *) LALCalloc(1, sizeof(SimBurstTable));
				prevSimBurst = tmpSimBurst;
			} else {
				tmpSimBurst = (SimBurstTable *) LALCalloc(1, sizeof(SimBurstTable));
				prevSimBurst->next = tmpSimBurst;
			}
			memcpy(tmpSimBurst, currentSimBurst, sizeof(SimBurstTable));
			prevSimBurst = tmpSimBurst;
			tmpSimBurst = tmpSimBurst->next = NULL;

			/* convert injection time to INT8 */
			LAL_CALL(LALGPStoINT8(&stat, &injPeakTime, &(currentSimBurst->l_peak_time)), &stat);


			/* loop over the burst events */
			while (currentEvent != NULL) {

				/* convert start time to INT8 */
				LAL_CALL(LALGPStoINT8(&stat, &burstStartTime, &(currentEvent->start_time)), &stat);

				if (injPeakTime < burstStartTime)
					break;

				LAL_CALL(LALCompareSimBurstAndSnglBurst(&stat, currentSimBurst, currentEvent, &accParams), &stat);

				if (accParams.match) {
					ndetected++;

					/*write the detected triggers */
					if (detTrigList == NULL) {
						detTrigList = tmpEvent = (SnglBurstTable *) LALCalloc(1, sizeof(SnglBurstTable));
						prevTrig = tmpEvent;
					} else {
						tmpEvent = (SnglBurstTable *) LALCalloc(1, sizeof(SnglBurstTable));
						prevTrig->next = tmpEvent;
					}
					memcpy(tmpEvent, currentEvent, sizeof(SnglBurstTable));
					prevTrig = tmpEvent;
					tmpEvent = tmpEvent->next = NULL;

					/* write the injected signals to an output file */
					if (injFoundList == NULL) {
						injFoundList = tmpInjFound = (SimBurstTable *) LALCalloc(1, sizeof(SimBurstTable));
						prevInjFound = tmpInjFound;
					} else {
						tmpInjFound = (SimBurstTable *) LALCalloc(1, sizeof(SimBurstTable));
						prevInjFound->next = tmpInjFound;
					}
					memcpy(tmpInjFound, currentSimBurst, sizeof(SimBurstTable));
					prevInjFound = tmpInjFound;
					tmpInjFound = tmpInjFound->next = NULL;

					break;
				}

				currentEvent = currentEvent->next;
			}
		}

		currentSimBurst = currentSimBurst->next;
	}


	/* Uncomment the line if have read in the searchsummary info. above */
	/*fprintf(stdout,"%d sec = %d hours analyzed\n",timeAnalyzed,
	   timeAnalyzed/3600); */

	fprintf(stdout, "Detected %i injections out of %i made\n", ndetected, ninjected);
	fprintf(stdout, "Efficiency is %f \n", ((REAL4) ndetected / (REAL4) ninjected));

  /*****************************************************************
   * open output xml file
   *****************************************************************/
	/* List of injections that were actually made */

	LAL_CALL(LALOpenLIGOLwXMLFile(&stat, &xmlStream, injmadeFile), &stat);
	LAL_CALL(LALBeginLIGOLwXMLTable(&stat, &xmlStream, sim_burst_table), &stat);
	myTable.simBurstTable = injSimList;
	LAL_CALL(LALWriteLIGOLwXMLTable(&stat, &xmlStream, myTable, sim_burst_table), &stat);
	LAL_CALL(LALEndLIGOLwXMLTable(&stat, &xmlStream), &stat);
	LAL_CALL(LALCloseLIGOLwXMLFile(&stat, &xmlStream), &stat);

	/*List of injections which were detected */
	LAL_CALL(LALOpenLIGOLwXMLFile(&stat, &xmlStream, injFoundFile), &stat);
	LAL_CALL(LALBeginLIGOLwXMLTable(&stat, &xmlStream, sim_burst_table), &stat);
	myTable.simBurstTable = injFoundList;
	LAL_CALL(LALWriteLIGOLwXMLTable(&stat, &xmlStream, myTable, sim_burst_table), &stat);
	LAL_CALL(LALEndLIGOLwXMLTable(&stat, &xmlStream), &stat);
	LAL_CALL(LALCloseLIGOLwXMLFile(&stat, &xmlStream), &stat);

	/*List of triggers corresponding to injection */
	LAL_CALL(LALOpenLIGOLwXMLFile(&stat, &xmlStream, outSnglFile), &stat);
	LAL_CALL(LALBeginLIGOLwXMLTable(&stat, &xmlStream, sngl_burst_table), &stat);
	myTable.snglBurstTable = detTrigList;
	LAL_CALL(LALWriteLIGOLwXMLTable(&stat, &xmlStream, myTable, sngl_burst_table), &stat);
	LAL_CALL(LALEndLIGOLwXMLTable(&stat, &xmlStream), &stat);
	LAL_CALL(LALCloseLIGOLwXMLFile(&stat, &xmlStream), &stat);

	exit(0);
}
