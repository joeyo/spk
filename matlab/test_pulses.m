% test sending pulsetrain frequency commands over zmq

clear all;

s = zmq('publish', 'ipc:///tmp/pulser.zmq');

zmq('poll', 1);

for i=1:100
    %data = uint16(randi(200,1,16));
    data = uint16([50 50 50 50 50 50 50 50 25 80 80 25 0 0 0 0]);
    nb = zmq('send', s, data);
    zmq('poll', 2); % need to wait > 1 msec before next send?
end

data = uint16(zeros(1, 16));
nb = zmq('send', s, data);

%data = uint16(randi(200,1,16));
%nb = zmq('send', s, data);

disp('done');