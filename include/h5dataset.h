#ifndef INCLUDE_H5DATASET_H_
#define INCLUDE_H5DATASET_H_

#include <string>

#include "hdf5.h"

class H5Dataset {
 public:
  hid_t dset;
  hid_t dtype;
  hid_t dspace;

 public:
  H5Dataset();
  virtual ~H5Dataset();

  bool open(hid_t h5file, std::string dset_name);
  void close();

};

#endif  // INCLUDE_H5DATASET_H_