% test sending pulsetrain frequency commands over zmq

clear all;

s = zmq('publish', 'ipc:///tmp/pulser.zmq');

x = 'FREQVEC';
nb = zmq('send', s, uint8(x))

f = zeros(16,1);
f(1) = 10;
f = uint16(f);

nb = zmq('send', s, f)
