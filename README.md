# spk

This is infrastructure for distributed online multielectrode
electrophysiology. It is forked from [myopen][1] and is licensed
under the GPL v3.

## Dependencies

* libarmadillo
* gtk+
* hdf5
* lua (or luajit)
* nvidia cg
* protobuffers
* tup
* zeromq
* and more that I am forgetting
 
## Building

First get the dependencies by typing `make deps`.
This should work on Debian and might work on Ubuntu.
Then type `tup init` followed by `tup`.
Install with `make install`.

[1]: http://github.com/tlh24/myopen 
