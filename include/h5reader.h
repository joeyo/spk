#ifndef INCLUDE_H5READER_H_
#define INCLUDE_H5READER_H_
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include "hdf5.h"

using namespace std;

class H5Reader {
 protected:
  string      m_fn;       // the file name
  hid_t       m_h5file;   // the h5 file

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
  string getVersion();
  string getSessionDescription();
  string getSessionID();
  string getSessionStartTime();

  // virtual const char *name() = 0; // set in child class

  const char *name() {
    return "H5 Broadband Reader v1.0";
  }

 protected:
  string getScalarStringDataset(const char *str);

};

#endif  // INCLUDE_H5READER_H_
