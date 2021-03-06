#ifndef __GTKCLIENT_H__
#define __GTKCLIENT_H__

#include "util.h"

enum MODES { // ui tabs
	MODE_RASTERS = 0,
	MODE_SPIKES,
	MODE_SORT
};

#if defined KHZ_24
#define SRATE_HZ	(24414.0625)
#define SRATE_KHZ	(24.4140625)
#define NSAMP 		(64*1024)	// about 2.6 seconds. (MUST BE POWER OF 2)
#define NSAMPWF		512			// samples to track for spike sorting (MUST BE POWER OF 2!)
#define NWFSAMP		64 			// wf samples at SRATE_HZ (nb compare with NSAMPWF, above)
#elif defined KHZ_48
#define SRATE_HZ	(48828.1250)
#define SRATE_KHZ	(48.8281250)
#define NSAMP 		(128*1024)
#define NSAMPWF		1024
#define NWFSAMP		128
#else
#error Bad sampling rate!
#endif

#define NFBUF 		4			// number of continuous channels to draw
#define NSBUF		1024 		// number of recent spikes to track

#define NSORT		4
#define NUNIT		(NSORT+1)	// including unsorted (parens are important!)

#define NUMPC		2			// number of PCs to use for clustering

typedef struct PO8Data {
	i64 tick;	// this is the tick for the first sample
	size_t numChannels;
	size_t numSamples;
	i16 *data;
} PO8Data;

typedef struct MatPack {
	size_t rows;
	size_t cols;
	double *data;
} MatPack;

#endif
