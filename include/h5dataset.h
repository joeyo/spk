#ifndef INCLUDE_H5DATASET_H_
#define INCLUDE_H5DATASET_H_

#include <string>

#include "hdf5.h"

class H5Dataset {
 public:
  hid_t dset;
  hid_t dtype;
  hid_t dspace;

  H5Dataset();
  virtual ~H5Dataset();

  bool open(hid_t h5file, std::string dset_name);
  void close();

 protected:
  int rank;
  hsize_t *dims;

};

class H5Broadband : public H5Dataset {
 public:

  H5Broadband();
  virtual ~H5Broadband();

  bool open(hid_t h5file);
  bool getBlock(size_t sample_offset, size_t num_samples, double *block);

};

#endif  // INCLUDE_H5DATASET_H_
