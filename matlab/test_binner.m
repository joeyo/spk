%box_in = fopen('/tmp/boxcar_in.fifo', 'w'); 
%box_out = fopen('/tmp/boxcar_out.fifo', 'r'); 

gks_in = fopen('/tmp/gks_in.fifo', 'w');
gks_out = fopen('/tmp/gks_out.fifo', 'r'); 

nc = (96*5);
sr = 100;
tmax = 10; % seconds
n = sr * 10;  % samples

%boxmap = memmapfile('/tmp/boxcar.mmap', 'Format', {'uint16' [1 nc] 'x'}); 
%boxbin = boxmap.Data(1).x;

gksmap = memmapfile('/tmp/gks.mmap', 'Format', {'uint16' [1 nc] 'x'}); 
gksbin = gksmap.Data(1).x;

x = nan(n, nc);
y = nan(n, nc);

t_now = tic;

while true
        
    if (toc(t_now) > 1/sr)
        %fwrite(box_in, -1, 'double');  % ask for spikes "now"
        %fread(box_out, 3, 'uchar');  % should inspect output for err?

        fwrite(gks_in, -1, 'double');  % ask for spikes "now"
        fread(gks_out, 3, 'uchar');  % should inspect output for err?

        t_now = tic;
        
        %xtmp = double(boxbin(1:nc)) ./ 0.1; % convert to rate
        ytmp = double(gksbin(1:nc)) ./ 128; % convert to rate
        
        % full matrix of rates
        %xprev      = x;
        %x(2:end,:) = xprev(1:end-1,:);
        %x(1,:)     = xtmp;

        yprev      = y;
        y(2:end,:) = yprev(1:end-1,:);
        y(1,:)     = ytmp;
        
        %{
        imagesc(y');
        xlabel('time');
        ylabel('neuron'); 
        colormap hot
        %colorbar;
        axis normal;
       %}
        
       
        %plot(x(:,1)); hold on;
        plot(y(:,2)); hold on;
        plot(y(:,3)); hold off;
        %}
        
        drawnow
    end
    
end

fclose(box_in);
fclose(box_out);
fclose(gks_in);
fclose(gks_out);
