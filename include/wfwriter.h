#ifndef __WFPACKET_H__
#define __WFPACKET_H__

#include "gtkclient.h"
#include <atomic>
#include <string>
#include <iostream>

//#define _LARGEFILE_SOURCE enabled by default.
#define _FILE_OFFSET_BITS 64

struct wfpak {
	double time; //wall clock time of spike, aligned to beginning.
	i64	ticks; //tdt ticks of spike. 8 bytes.
	i16	channel;
	i16	unit;
	i16   len;
	i16	wf[NWFSAMP];
};
#define WFBUFSIZ (1024*64)
#define WFBUFMASK (WFBUFSIZ-1)

class WfWriter
{
protected:
	std::atomic<i64> m_w; //this is great!
	std::atomic<bool>	m_enable;
	i64 m_r;
	wfpak m_d[WFBUFSIZ];
	std::string m_fn; // the file name
	FILE *m_fid;

public:
	WfWriter()
	{
		m_w = m_r = 0;
		m_enable = false;
		m_fn.assign("");
		m_fid = 0;
	}
	~WfWriter() {}
	FILE *open(char *name)
	{
		m_fid = fopen(name, "w");
		if (m_fid == 0)
			std::cout << "could not open " << name << std::endl;
		else
			m_enable = true;
		m_fn.assign(name);
		fprintf(stdout,"WfWriter: started logging to %s.\n",m_fn.c_str());
		return m_fid;
	}
	void close()
	{
		if (m_enable) {
			write();
			fclose(m_fid);
			m_enable = false;
			m_fn.clear();
			m_w = m_r = 0;
			fprintf(stdout,"WfWriter: stopped logging to file\n");
		}
	}
	void add(wfpak *wf)    //non-blocking. call from po8e thread.
	{
		if (m_enable) {
			i64 w = m_w++; //must be atomic.
			memcpy(&(m_d[w & WFBUFMASK]), wf, sizeof(wfpak));
		}
	}
	int write()   //call from another thread.
	{
		if (m_enable) {
			i64 w = m_w;
			for (; m_r<w; m_r++) {
				u32 tmp = 0xc0edfad0; //magic number, for alignment.
				fwrite((void *)&tmp, 4, 1, m_fid);
				fwrite((void *)&(m_d[m_r & WFBUFMASK]), sizeof(wfpak), 1, m_fid);
			}
			return 1;
		}
		return 0;
	}
	bool enabled()
	{
		return m_enable;
	}
	i64 bytes()
	{
		return m_r * sizeof(wfpak);
	}
	//convert the file to matlab.

	std::string filename()
	{
		return m_fn;
	}
};
#endif

