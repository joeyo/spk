// ks: the kernel smoother
// for now i am just going to implement a gaussian kernel smoother
// but boxcars, triangles, etc are very easy

#define KS_LEN 2048 //must be a power of 2.

// base kernel smoother
class KernelSmoother
{
protected:
	double			m_ts[KS_LEN];
	unsigned int	m_w; // write to here.
	double sigma;
public:
	KernelSmoother()
	{
		m_w = 0;
		for (int i=0; i<KS_LEN; i++) {
			m_ts[i] = -1e10;
		}
		sigma = 0;
	}
	virtual ~KernelSmoother()
	{
		//nothing allocated.
	}
	virtual void set_bandwidth(double _sigma)
	{
		sigma = _sigma;
	}

	void add(double time)   // n.b. time is in seconds.
	{
		m_ts[m_w & (KS_LEN-1)] = time;
		m_w++;
	}
};

class GaussianKernelSmoother : public KernelSmoother
{
protected:
	double A;
	double B;
public:
	void set_bandwidth(double _sigma) // units of seconds
	{
		sigma = _sigma;
		A =  1/(sqrt(2*3.14159265)*sigma);
		B = -1/(2*sigma*sigma);
	}
	double get_rate(double time)
	{
		// time is the time to evaluate the rate (seconds)
		// return a short as that's really all the resolution we need...
		unsigned int w = m_w; // atomic.
		double y = 0.0;
		int i=0;
		while (w > 0 && i < KS_LEN) {
			w--;
			i++;
			double t = time - m_ts[w & (KS_LEN-1)];
			if (t <= 0) {
				continue;
			}
			y += A*exp(B*t*t);
		}
		unsigned short s = MIN((unsigned short)(y * 128.f), 0xffff);
		return s;
	}
};

class ExponentialKernelSmoother : public KernelSmoother
{
protected:
	double A;
	double B;
public:
	void set_bandwidth(double _sigma) // units of seconds
	{
		sigma = _sigma;
		A =  1/(sqrt(2)*sigma);
		B = -sqrt(2);
	}
	double get_rate(double time)
	{
		// time is the time to evaluate the rate (seconds)
		// return a short as that's really all the resolution we need...
		unsigned int w = m_w; // atomic.
		double y = 0.0;
		int i=0;
		while (w > 0 && i < KS_LEN) {
			w--;
			i++;
			double t = time - m_ts[w & (KS_LEN-1)];
			if (t <= 0) {
				continue;
			}
			y += A*exp(B*abs(t/sigma));
		}
		unsigned short s = MIN((unsigned short)(y * 128.f), 0xffff);
		return s;
	}
};