#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include "hdf5.h"

#ifndef __H5READER_H__
#define	__H5READER_H__

using namespace std;

class H5Reader
{

protected:
	string 			m_fn; 			// the file name
	hid_t 			m_h5file;		// the h5 file
	vector<hid_t>	m_h5groups;		// for the groups
	vector<hid_t>	m_h5dataspaces;	// [ containers for the datapsaces
	vector<hid_t> 	m_h5props;		// [ and the chunk properties
	
public:
	H5Reader();

	virtual ~H5Reader();

	// start the writer. fn is filename
	virtual bool open(const char *fn);

	// close log file
	virtual bool close();

	// returns the name of the file we are writing to
	virtual string filename();

	string getUUID();

	//virtual void getVersion();

	//void getFileCreateDate(char *str);

	//void getSessionStartTime(char *str);

	//void getSessionDescription(const char *str);

	//void getDeviceDescription(const char *name, const char *desc);

	//virtual const char *name() = 0; // set in child class

	const char *name()
	{
		return "H5 Broadband Reader v1.0";
	};

};

#endif
