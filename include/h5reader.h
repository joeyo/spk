#ifndef INCLUDE_H5READER_H_
#define INCLUDE_H5READER_H_
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "hdf5.h"

#include "h5dataset.h"

class H5Reader {
 protected:
  std::string m_fn;  // the file name
  hid_t m_h5file;  // the h5 file
  H5Broadband broadband;

 public:
  H5Reader();

  virtual ~H5Reader();

  // start the writer. fn is filename
  virtual bool open(std::string fn);

  // close log file
  virtual bool close();

  // returns the name of the file we are writing to
  virtual std::string filename();

  std::string getUUID();
  std::string getVersion();
  std::string getSessionDescription();
  std::string getSessionID();
  std::string getSessionStartTime();

  bool getInt32Scalar(std::string str, int32_t *x);

  bool getBroadbandBlock(
    size_t sample_offset, size_t num_samples, double *block);


  // virtual const char *name() = 0; // set in child class

  const char *name() {
    return "H5 Broadband Reader v1.0";
  }

 protected:
  std::string getScalarStringDataset(std::string str);

};

#endif  // INCLUDE_H5READER_H_
