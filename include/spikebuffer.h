#ifndef __SPIKEBUFFER_H__
#define	__SPIKEBUFFER_H__

#include <armadillo>
#include <atomic>
#include "util.h"

#define SPIKE_BUF_SIZE (4096) // BYTES, MUST BE POWER OF 2
#define SPIKE_MASK (SPIKE_BUF_SIZE-1)

using namespace std;
using namespace arma;

class NEO
{
protected:
	float x_prev;
	float x_prev2;
	running_stat<float> m_stats;
public:
	NEO();
	~NEO();

	float eval(float x);
	float mean();
};

class SpikeBuffer
{
protected:
	float m_wf[SPIKE_BUF_SIZE];	 	// the spike buffer
	u32 m_tk[SPIKE_BUF_SIZE];		// the tick buffer
	float m_neo[SPIKE_BUF_SIZE];	// the neo buffer
	std::atomic<long> m_w;       	// atomic write pointer
	std::atomic<long> m_r;       	// atomic read pointer
	NEO neof;

public:
	SpikeBuffer();

	virtual ~SpikeBuffer();

	bool addSample(u32 _tk, float _wf);

	bool getSpike(u32 *tk, float *wf, float *neo, int n, float threshold, int alignment, int pre_emphasis);

	// returns buffer capcity as a fraction. 1 means filled. 0 means empty.
	float capacity();

	// returns the number of bytes written
	long bytes();

	const char *name()
	{
		return "spike buffer v2";
	};

	long rp();
	long wp();

protected:

};
#endif
