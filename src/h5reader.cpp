#include <string.h>
#include "util.h"
#include "h5reader.h"

H5Reader::H5Reader()
{
	m_fn.assign("");
	m_h5file = 0;
	m_h5groups.clear();
	m_h5dataspaces.clear();
	m_h5props.clear();
}

H5Reader::~H5Reader()
{
	close();
}

bool H5Reader::open(const char *fn)
{

	// Open a file read-only
	m_h5file = H5Fopen(fn, H5F_ACC_RDONLY, H5P_DEFAULT);

	if (m_h5file < 0) {
		return false;
	}

	m_fn.assign(fn);
	fprintf(stdout,"%s: opened %s\n", name(), fn);

	// we intentionally don't enable here: rather, enable in the subclasses

	return true;
}

bool H5Reader::close()
{
	/*
	for (auto &x : m_h5props) {
		if (x > 0) {
			H5Pclose(x);
		}
	}
	m_h5props.clear();

	for (auto &x : m_h5dataspaces) {
		if (x > 0) {
			H5Sclose(x);
		}
	}
	m_h5dataspaces.clear();

	for (auto &x : m_h5groups) {
		if (x > 0) {
			H5Gclose(x);
		}
	}
	m_h5groups.clear();
	*/

	if (m_h5file) {
		H5Fclose(m_h5file);
		m_h5file = 0;
	}
	

	if (!m_fn.empty()) {
		fprintf(stdout,"%s: closed %s\n", name(), m_fn.c_str());
		m_fn.clear();
	}

	return true;
}

string H5Reader::filename()
{
	return m_fn;
}

string H5Reader::getUUID()
{
	string s;

	if (m_h5file < 0) {
		return s;
	}

	hid_t dset = H5Dopen(m_h5file, "/identifier", H5P_DEFAULT);
	if (dset < 0) {
		return s;
	}

	hid_t dtype = H5Dget_type(dset);
	if (dtype < 0) {
		H5Dclose(dset);
		return s;
	}

	size_t n = H5Tget_size(dtype)  + 1; // +1 for \0
	char * buf = new char[n];

	H5Dread(dset, dtype, H5S_ALL, H5S_ALL, H5P_DEFAULT, (void *) buf); 
	
	s = buf;

	delete[] buf;

	H5Tclose(dtype);
	H5Dclose(dset);

	return s;

}
/*
void H5Reader::getVersion()
{
	// set the NWB version
	const char *str = "NWB-1.0.6";

	hid_t ds = H5Screate(H5S_SCALAR);
	hid_t dtype = H5Tcopy (H5T_C_S1);
	H5Tset_size(dtype, strlen(str));
	H5Tset_strpad(dtype, H5T_STR_NULLTERM);
	hid_t dset = H5Dcreate(m_h5file, "/nwb_version", dtype, ds,
	                       H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	H5Dwrite(dset, dtype, H5S_ALL, H5S_ALL, H5P_DEFAULT, str);
	H5Dclose(dset);
	H5Tclose(dtype);
	H5Sclose(ds);
}
void H5Reader::getFileCreateDate(char *str)
{
	hsize_t init_dims = 1;
	hsize_t max_dims = H5S_UNLIMITED;
	hid_t ds = H5Screate_simple(1, &init_dims, &max_dims);

	hid_t dtype = H5Tcopy(H5T_C_S1);
	H5Tset_size(dtype, strlen(str));
	H5Tset_strpad(dtype, H5T_STR_NULLTERM);

	hid_t prop = H5Pcreate(H5P_DATASET_CREATE);
	if (m_shuffle)
		shuffleDataset(prop);
	if (m_deflate)
		deflateDataset(prop);
	hsize_t chunk_dims = 8;
	H5Pset_chunk(prop, 1, &chunk_dims);

	hid_t dset = H5Dcreate(m_h5file, "/file_create_date",
		dtype, ds, H5P_DEFAULT, prop, H5P_DEFAULT);

	H5Dwrite(dset, dtype, H5S_ALL, H5S_ALL, H5P_DEFAULT, str);

	H5Dclose(dset);
	H5Pclose(prop);
	H5Tclose(dtype);
	H5Sclose(ds);
}
void H5Reader::getSessionStartTime(char *str)
{
	hid_t ds = H5Screate(H5S_SCALAR);
	hid_t dtype = H5Tcopy (H5T_C_S1);
	H5Tset_size(dtype, strlen(str));
	H5Tset_strpad(dtype, H5T_STR_NULLTERM);
	hid_t dset = H5Dcreate(m_h5file, "/session_start_time", dtype, ds,
	                       H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	H5Dwrite(dset, dtype, H5S_ALL, H5S_ALL, H5P_DEFAULT, str);
	H5Dclose(dset);
	H5Tclose(dtype);
	H5Sclose(ds);
}
void H5Reader::getSessionDescription(const char *str)
{
	hid_t ds = H5Screate(H5S_SCALAR);
	hid_t dtype = H5Tcopy (H5T_C_S1);
	H5Tset_size(dtype, strlen(str));
	H5Tset_strpad(dtype, H5T_STR_NULLTERM);
	hid_t dset = H5Dcreate(m_h5file, "/session_description", dtype, ds,
	                       H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	H5Dwrite(dset, dtype, H5S_ALL, H5S_ALL, H5P_DEFAULT, str);
	H5Dclose(dset);
	H5Tclose(dtype);
	H5Sclose(ds);
}
void H5Reader::getDeviceDescription(const char *name, const char *desc)
{
	hid_t ds = H5Screate(H5S_SCALAR);
	hid_t dtype = H5Tcopy (H5T_C_S1);
	H5Tset_size(dtype, strlen(desc));
	H5Tset_strpad(dtype, H5T_STR_NULLTERM);

	std::string s = "/general/devices/";

	hid_t dset = H5Dcreate(m_h5file, (s+name).c_str(), dtype, ds,
	                       H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	H5Dwrite(dset, dtype, H5S_ALL, H5S_ALL, H5P_DEFAULT, desc);
	H5Dclose(dset);
	H5Tclose(dtype);
	H5Sclose(ds);
}
*/