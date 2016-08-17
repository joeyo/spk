# spk

This is infrastructure for distributed online multielectrode
electrophysiology. It is forked from [myopen] and is licensed
under the GPL v3.

## Dependencies

* [libarmadillo][arma] ( >= 6.700)
* gtk+
* hdf5
* lua (or luajit)
* nvidia cg
* protobuffers
* [tup]
* [zeromq] (version 3)
* and likely more that I am forgetting
 
## Building

First get the dependencies by typing `make deps`.
This should work on Debian and might work on Ubuntu.
Then type `tup init` followed by `tup`.
Install with `make install`.

[myopen]: http://github.com/tlh24/myopen/
[arma]: http://arma.sourceforge.net/
[tup]: http://gittup.org/tup/
[zeromq]: http://zeromq.org/
