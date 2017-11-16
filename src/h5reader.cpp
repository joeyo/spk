#include <string>
#include "util.h"
#include "h5reader.h"
#include "h5dataset.h"

H5Reader::H5Reader() {
  m_fn.assign("");
  m_h5file = 0;
}

H5Reader::~H5Reader() {
  close();
}

bool H5Reader::open(std::string fn) {
  // Open a file read-only
  m_h5file = H5Fopen(fn.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);

  if (m_h5file < 0) {
    return false;
  }

  m_fn = fn;
  fprintf(stdout, "%s: opened %s\n", name(), m_fn.c_str());

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

std::string H5Reader::filename() {
  return m_fn;
}

std::string H5Reader::getSessionDescription() {
  return getScalarStringDataset("/session_description");
}

std::string H5Reader::getUUID() {
  return getScalarStringDataset("/identifier");
}

std::string H5Reader::getVersion() {
  return getScalarStringDataset("/nwb_version");
}

std::string H5Reader::getSessionID() {
  return getScalarStringDataset("/general/session_id");
}

std::string H5Reader::getSessionStartTime() {
  return getScalarStringDataset("/session_start_time");
}

std::string H5Reader::getScalarStringDataset(std::string str) {
  std::string s;

  H5Dataset dataset;
  if (!dataset.open(m_h5file, str)) {
    return s;
  }

  size_t n = H5Tget_size(dataset.dtype);
  char * buf = new char[n+1];  // +1 for \0

  H5Dread(dataset.dset, dataset.dtype, H5S_ALL, H5S_ALL, H5P_DEFAULT, reinterpret_cast<void *>(buf));

  buf[n] = '\0';
  s = buf;

  delete[] buf;

  return s;
}

bool H5Reader::getInt32Scalar(std::string str, int32_t *x) {

  H5Dataset dataset;
  if (!dataset.open(m_h5file, str)) {
    return false;
  }

  // should additionally check if it's Int32
  if (H5T_INTEGER != H5Tget_class(dataset.dtype)) {
    return false;
  }

  if (H5S_SCALAR != H5Sget_simple_extent_type(dataset.dspace)) {
    return false;
  }

  H5Dread(dataset.dset, dataset.dtype, H5S_ALL, H5S_ALL, H5P_DEFAULT, x);

  return true;
}

bool H5Reader::openBroadbandData() {

}


bool H5Reader::openBroadbandData() {

  H5Dataset dataset;
  if (!dataset.open(m_h5file, "/acquisition/timeseries/broadband/data")) {
    return false;
  }

  // should additionally check if it's INT_16LE
  if (H5T_INTEGER != H5Tget_class(dataset.dtype)) {
    return false;
  }

  int rank = H5Sget_simple_extent_ndims(dataset.dspace);
  //dims_total
  //status_n  = H5Sget_simple_extent_dims (dataspace, dims_out, NULL);

  size_t foo = sample_offset + block_size; // bullshit

  return true;
}
