# spk

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.801890.svg)](https://doi.org/10.5281/zenodo.801890)

This is infrastructure for distributed online multielectrode
electrophysiology. It is published under the [MIT license][mit].

![spk screenshot](https://raw.githubusercontent.com/joeyo/spk/master/etc/spk.png)

## Dependencies

* [libarmadillo][arma] (>= 6.700)
* [gtk+ 2.x][gtk]
* [hdf5][h5] (1.8 recommended)
* [luajit 2.0][luajit] (or [lua 5.1][lua])
* [nvidia cg][cg]
* libPO8eStreaming (get from [TDT])
* [protocol buffers][protobuf] (version 2)
* [tup]
* [zeromq][zmq] (version 3)
* and likely others that I have forgotten

## Building

First get the dependencies by typing `make deps`.
This should work on Debian and might work on Ubuntu.
Then type `tup init` followed by `tup`.
Install with `make install`.

[myopen]: http://github.com/tlh24/myopen/
[arma]: http://arma.sourceforge.net/
[cg]: https://developer.nvidia.com/cg-toolkit
[gtk]: http://www.gtk.org/
[h5]: https://www.hdfgroup.org/HDF5/
[lua]: https://www.lua.org/
[luajit]: http://luajit.org/
[mit]: https://opensource.org/licenses/MIT
[protobuf]: https://developers.google.com/protocol-buffers/
[tup]: http://gittup.org/tup/
[TDT]: http://www.tdt.com/
[zmq]: http://zeromq.org/
