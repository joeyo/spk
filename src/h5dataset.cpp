#include <string>
#include "util.h"
#include "h5dataset.h"

H5Dataset::H5Dataset() {
  dset = 0;
  dtype = 0;
  dspace = 0;
  rank = -1;
  dims = nullptr;
}

H5Dataset::~H5Dataset() {
  close();
}

bool H5Dataset::open(hid_t h5file, std::string dset_name) {

  if (h5file < 0) {
      return false;
  }

  dset = H5Dopen(h5file, dset_name.c_str(), H5P_DEFAULT);
  if (dset < 0) {
    return false;
  }

  dtype = H5Dget_type(dset);
  if (dtype < 0) {
    close();
    return false;
  }

  dspace = H5Dget_space(dset);
  if (dspace < 0) {
    close();
    return false;
  }

  // check if this is a simple extent?
  rank = H5Sget_simple_extent_ndims(dspace); //  check for error
  dims = new hsize_t[rank];
  H5Sget_simple_extent_dims(dspace, dims, nullptr); //  check for error

  return true;
}

void H5Dataset::close() {
  delete[] dims;
  if (0 != dspace) {
    H5Sclose(dspace);
  }
  if (0 != dtype) {
    H5Tclose(dtype);
  }
  if (0 != dset) {
    H5Dclose(dset);
  }
}

H5Broadband::H5Broadband() {}

H5Broadband::~H5Broadband() {}

bool H5Broadband::open(hid_t h5file) {
  if (!H5Dataset::open(h5file, "/acquisition/timeseries/broadband/data")) {
    return false;
  }
  // should additionally check if it's INT_16LE
  if (H5T_INTEGER != H5Tget_class(dtype)) {
    H5Dataset::close();
    return false;
  }
  return true;
}

bool H5Broadband::getBlock(size_t sample_offset, size_t num_samples, double *block) {
  return true;
}
