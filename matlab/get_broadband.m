% get broadband data over zmq sockets

clear all;

n = 24414; % samples

x = nan(n, 96);
x = single(x);

%sock = zmq('subscribe', 'tcp', 'localhost', 2000);
sock = zmq('subscribe', 'ipc', 'bp.zmq');

while(true)

    id = zmq('poll');

    data = zmq('receive', id);
    
    nc = typecast(data(1:8), 'uint64');
    ns = typecast(data(9:16), 'uint64');
    tk = typecast(data(17:24), 'int64');
    
    data = zmq('receive', id);
   
    f = typecast(data, 'single');
    f = reshape(f, ns, nc);
    
    xtmp = x(ns:end,:);
    x(1:end-(ns-1),:) = xtmp;
    x(end-(ns-1):end,:) = f;
       
    %imagesc(x');
    %xlabel('time');
    %ylabel('neuron'); 
    %colormap hot
    %axis normal;
    plot(x(:,1));
    drawnow;
    
end