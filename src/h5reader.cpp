#include <string>
#include "util.h"
#include "h5reader.h"

H5Reader::H5Reader() {
  m_fn.assign("");
  m_h5file = 0;
}

H5Reader::~H5Reader() {
  close();
}

bool H5Reader::open(const char *fn) {
  // Open a file read-only
  m_h5file = H5Fopen(fn, H5F_ACC_RDONLY, H5P_DEFAULT);

  if (m_h5file < 0) {
    return false;
  }

  m_fn.assign(fn);
  fprintf(stdout, "%s: opened %s\n", name(), fn);

  return true;
}

bool H5Reader::close() {
  if (m_h5file) {
    H5Fclose(m_h5file);
    m_h5file = 0;
  }


  if (!m_fn.empty()) {
    fprintf(stdout, "%s: closed %s\n", name(), m_fn.c_str());
    m_fn.clear();
  }

  return true;
}

string H5Reader::filename() {
  return m_fn;
}

string H5Reader::getSessionDescription() {
  return getScalarStringDataset("/session_description");
}

string H5Reader::getUUID() {
  return getScalarStringDataset("/identifier");
}

string H5Reader::getVersion() {
  return getScalarStringDataset("/nwb_version");
}

string H5Reader::getSessionID() {
  return getScalarStringDataset("/general/session_id");
}

string H5Reader::getSessionStartTime() {
  return getScalarStringDataset("/session_start_time");
}

string H5Reader::getScalarStringDataset(const char *str) {
  string s;

  if (m_h5file < 0) {
    return s;
  }

  hid_t dset = H5Dopen(m_h5file, str, H5P_DEFAULT);
  if (dset < 0) {
    return s;
  }

  hid_t dtype = H5Dget_type(dset);
  if (dtype < 0) {
    H5Dclose(dset);
    return s;
  }

  size_t n = H5Tget_size(dtype);
  char * buf = new char[n+1];  // +1 for \0

  H5Dread(dset, dtype, H5S_ALL, H5S_ALL, H5P_DEFAULT, reinterpret_cast<void *>(buf));

  buf[n] = '\0';
  s = buf;

  delete[] buf;

  H5Tclose(dtype);
  H5Dclose(dset);

  return s;
}

bool H5Reader::getInt32Scalar(const char *str, int32_t *x) {

  if (m_h5file < 0) {
    return false;
  }

  hid_t dset = H5Dopen(m_h5file, str, H5P_DEFAULT);
  if (dset < 0) {
    return false;
  }

  hid_t dtype = H5Dget_type(dset);
  if (dtype < 0) {
    H5Dclose(dset);
    return false;
  }

  if (H5T_INTEGER != H5Tget_class(dtype)) {
    H5Tclose(dtype);
    H5Dclose(dset);
    return false;
  }

  hid_t dspace = H5Dget_space(dset);
  if (dspace < 0) {
    H5Tclose(dtype);
    H5Dclose(dset);
    return false;
  }

  if (H5S_SCALAR != H5Sget_simple_extent_type(dspace)) {
    H5Sclose(dspace);
    H5Tclose(dtype);
    H5Dclose(dset);
    return false;
  }

  H5Dread(dset, dtype, H5S_ALL, H5S_ALL, H5P_DEFAULT, x);

  H5Sclose(dspace);
  H5Tclose(dtype);
  H5Dclose(dset);

  return true;
}
