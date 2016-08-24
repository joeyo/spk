/*
 * MATLAB mex file to send and receive zmq messages (PUB/SUB)
 * Stephen G. McGill copyright 2015 <smcgill3@seas.upenn.edu>
 * Joseph E. O'Doherty copyright 2016 <joeyo@phy.ucsf.edu>
 * */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <zmq.h>
#include "mex.h"

#if defined(_WIN32) || defined(_WIN64)
#include <io.h>
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <unistd.h>
#endif

/* N.B. Do not exceed 255 sockets since indexed by uint8_t */
#define MAX_SOCKETS 16

void *ctx;
void *sockets[MAX_SOCKETS];
zmq_pollitem_t poll_items[MAX_SOCKETS];
uint8_t socket_cnt = 0;
static int initialized = 0;

/* Cleaning up the data */
void cleanup( void )
{
	mexPrintf("ZMQMEX: closing sockets and context.\n");
	for (int i=0; i<socket_cnt; i++)
		zmq_close(sockets[i]);
	zmq_ctx_term(ctx);
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
	/* First run will initialize the context */
	if (!initialized) {
		int major, minor, patch;
		zmq_version(&major, &minor, &patch);
		mexPrintf("ZMQMEX: using library version %d.%d.%d\n", major, minor, patch);

		if ((ctx = zmq_ctx_new()) == NULL) {
			mexErrMsgTxt("Could not create zmq context!");
		}
		mexPrintf("ZMQMEX: creating a ZMQ context.\n");

		/* we don't need 1024 sockets */
		if (zmq_ctx_set(ctx, ZMQ_MAX_SOCKETS, MAX_SOCKETS) != 0) {
			mexErrMsgTxt("Could not set max sockets!");
		}

		initialized = 1;
		/* 'clear all' and 'clear mex' calls this function */
		mexAtExit(cleanup);
	}

	/* Should Have a least a few arguments */
	if (nrhs < 1) {
        mexPrintf("Usage:\n");
        mexPrintf(" zmq('publish', [endpoint]')\n");
        mexPrintf(" zmq('subscribe', [endpoint]')\n");
        mexPrintf(" zmq('poll', [timeout])\n");
        mexPrintf(" zmq('send', [socket_id], [msg])\n");
        mexPrintf(" zmq('recv', [socket_id])\n");
        mexPrintf(" zmq('fd', [fd])\n");
		return;
	}

	char *command;

	/* Grab the command string */
	if ( !(command = mxArrayToString(prhs[0])) )
		mexErrMsgTxt("First argument must be one of: '[publish|subscribe|poll|send|recv|fd]'.");

	/* Setup a publisher */
	if ( strcmp(command, "publish")==0 ) {

		/* Check that we have enough sockets available */
		if ( socket_cnt==MAX_SOCKETS )
			mexErrMsgTxt("Cannot create additional sockets!");

		/* Grab the endpoint */
		if (nrhs < 2)
			mexErrMsgTxt("Usage: zmq('publish', [endpoint])");
		char *endpoint;
		if ( !(endpoint=mxArrayToString(prhs[1])) )
			mexErrMsgTxt("Malformed endpoint string (2nd argument)'.");

		/* Create socket and bind to the endpoint */
		mexPrintf("ZMQMEX: Binding to %s.\n", endpoint);
		if ((sockets[socket_cnt]=zmq_socket(ctx, ZMQ_PUB)) == NULL)
			mexErrMsgTxt("Could not create socket!");
		if ((zmq_bind(sockets[socket_cnt], endpoint)) != 0)
			mexErrMsgTxt("Could not bind socket to endpoint!");

		mxFree(endpoint);

		poll_items[socket_cnt].socket = sockets[socket_cnt];
		/* poll_items[socket_cnt].events = ZMQ_POLLOUT; */

		/* MATLAB specific return of the socket ID */
		if (nlhs > 0) {
			mwSize sz = 1;
			plhs[0] = mxCreateNumericArray(1, &sz, mxUINT8_CLASS, mxREAL);
			uint8_t *out = (uint8_t *)mxGetData(plhs[0]);
			out[0] = socket_cnt;
		}
		socket_cnt++;
	}

	/* Set up a subscriber */
	else if ( strcmp(command, "subscribe")==0 ) {

		/* Check that we have enough socket spaces available */
		if ( socket_cnt==MAX_SOCKETS )
			mexErrMsgTxt("Cannot create additional sockets!");

		/* Grab the endpoint */
		if (nrhs < 2)
			mexErrMsgTxt("Usage: zmq('subscribe', [endpoint])");
		char *endpoint;
		if ( !(endpoint=mxArrayToString(prhs[1])) )
			mexErrMsgTxt("Malformed endpoint string (2nd argument)'.");

		/* Create socket and connect to the endpoint */
		mexPrintf("ZMQMEX: Connecting to %s.\n", endpoint);
		if ((sockets[socket_cnt]=zmq_socket(ctx, ZMQ_SUB))==NULL)
			mexErrMsgTxt("Could not create socket!");
		zmq_setsockopt(sockets[socket_cnt], ZMQ_SUBSCRIBE, "", 0);
		if ( zmq_connect(sockets[socket_cnt], endpoint) != 0 )
			mexErrMsgTxt("Could not connect socket to endpoint!");

		mxFree(endpoint);

		/* Add the connected socket to the poll items */
		poll_items[socket_cnt].socket = sockets[socket_cnt];
		poll_items[socket_cnt].events = ZMQ_POLLIN;

		/* MATLAB specific return of the socket ID */
		if (nlhs > 0) {
			mwSize sz = 1;
			plhs[0] = mxCreateNumericArray(1, &sz, mxUINT8_CLASS, mxREAL);
			uint8_t *out = (uint8_t *)mxGetData(plhs[0]);
			out[0] = socket_cnt;
		}
		socket_cnt++;
	}

	/* Send data over a socket */
	else if (strcasecmp(command, "send") == 0) {
		if (nrhs != 3)
			mexErrMsgTxt("Please provide a socket id and a message to send");
		if ( !mxIsClass(prhs[1],"uint8") || mxGetNumberOfElements(prhs[1])!=1 )
			mexErrMsgTxt("Please provide a valid handle");
		uint8_t socket_id = *( (uint8_t *)mxGetData(prhs[1]) );
		if (socket_id > socket_cnt)
			mexErrMsgTxt("Invalid socket id!");

		/* Get the data and send it  */
		void *msg = (void *)mxGetData(prhs[2]);
		size_t msglen = mxGetNumberOfElements(prhs[2])*mxGetElementSize(prhs[2]);
		int nbytes = zmq_send(sockets[socket_id], msg, msglen, 0);

		/* Check the integrity of the send */
		if (nbytes!=msglen)
			mexErrMsgTxt("Did not send correct number of bytes.");

		if (nlhs > 0) {
			mwSize sz = 1;
			plhs[0] = mxCreateNumericArray(1, &sz, mxINT32_CLASS, mxREAL);
			int *out = (int *)mxGetData(plhs[0]);
			out[0] = nbytes;
		}
	} else if (strcasecmp(command, "recv") == 0) {
		if (nrhs != 2)
			mexErrMsgTxt("Please provide a socket id.");
		if ( !mxIsClass(prhs[1],"uint8") || mxGetNumberOfElements(prhs[1]) !=1 )
			mexErrMsgTxt("Please provide a valid handle");
		uint8_t socket_id = *( (uint8_t *)mxGetData(prhs[1]) );
		if ( socket_id>socket_cnt )
			mexErrMsgTxt("Invalid socket id!");

		/* If a file descriptor, then return the fd */
		if (poll_items[socket_id].socket == NULL) {
			if (nlhs > 0)
				plhs[0] = mxCreateDoubleScalar(poll_items[socket_id].fd);
			if (nlhs > 1)
				plhs[1] = mxCreateDoubleScalar(0);
			return;
		}

		zmq_msg_t msg;
		zmq_msg_init(&msg);

		/* Blocking Receive */
		int nbytes = zmq_msg_recv(&msg, sockets[socket_id], 0);
		/* Non-blocking Receive */
		/*int nbytes = zmq_msg_recv(&msg, sockets[socket_id], ZMQ_DONTWAIT);*/
		if (nbytes==-1) {
			zmq_msg_close(&msg);
			mexErrMsgTxt("Receive failure!");
		}

		/* Check if multipart */
		int64_t more;
		size_t more_size = sizeof(more);
		if (zmq_getsockopt(sockets[socket_id], ZMQ_RCVMORE,
		                   &more, &more_size) != 0) {
			zmq_msg_close(&msg);
			mexErrMsgTxt("ZMQ_RCVMORE failure!");
		}

		/* Output the data to MATLAB */
		if (nlhs > 0) {
			mwSize sz = nbytes;
			plhs[0] = mxCreateNumericArray(1, &sz, mxUINT8_CLASS, mxREAL);
			void *start = mxGetData(plhs[0]);
			memcpy(start, zmq_msg_data(&msg), nbytes);
		}
		if (nlhs > 1) {
			mwSize sz = 1;
			plhs[1] = mxCreateNumericArray(1, &sz, mxUINT8_CLASS, mxREAL);
			uint8_t *out = (uint8_t *)mxGetData(plhs[1]);
			out[0] = (uint8_t)more;
		}
		zmq_msg_close(&msg);
	}

	/* Poll for data at a specified interval.  Default interval: indefinite */
	else if (strcasecmp(command, "poll") == 0) {
		long mytimeout = -1;
		if (nrhs > 1) {
			if (mxGetNumberOfElements(prhs[1]) > 0) {
				double *timeout_ptr = (double *)mxGetData(prhs[1]);
				mytimeout = (long)(timeout_ptr[0]);
			}
		}

		/* Get the number of objects that have data */
		int rc;
		if ( (rc=zmq_poll(poll_items, socket_cnt, mytimeout)) < 0 )
			mexErrMsgTxt("Poll error!");

		/*
		Create a cell mxArray to hold the poll elements, and have an
		index array to know which socket received data
		*/
		if (nlhs > 0) {
			mwSize sz = rc;
			mxArray *idx_array_ptr =
			    mxCreateNumericArray(1, &sz, mxUINT8_CLASS, mxREAL);
			uint8_t *idx = (uint8_t *)mxGetData(idx_array_ptr);
			int r = 0;
			for (int i=0; i<socket_cnt; i++) {
				if (poll_items[i].revents)
					idx[r++] = i;
			}
			plhs[0] = idx_array_ptr;
		}
	}

	/* Add a file descriptor to the polling list (typically udp) */
	else if (strcasecmp(command, "fd") == 0) {
		if ( socket_cnt==MAX_SOCKETS )
			mexErrMsgTxt("Cannot create additional poll items!");
		if (nrhs != 2)
			mexErrMsgTxt("Please provide a file descriptor.");
		if ( !mxIsClass(prhs[1],"uint32") || mxGetNumberOfElements( prhs[1] )!=1 )
			mexErrMsgTxt("Please provide a valid file descriptor.");
		uint32_t fd = *( (uint32_t *)mxGetData(prhs[1]) );

		/* cannot be stdin, stdout or stderr */
		if (fd < 3)
			mexErrMsgTxt("Bad file descriptor!");

		poll_items[socket_cnt].socket = NULL;
		poll_items[socket_cnt].fd = fd;
		poll_items[socket_cnt].events = ZMQ_POLLIN;
		if (nlhs > 0) {
			mwSize sz = 1;
			plhs[0] = mxCreateNumericArray(1, &sz, mxUINT8_CLASS, mxREAL);
			uint8_t *out = (uint8_t *)mxGetData(plhs[0]);
			out[0] = socket_cnt;
		}
		socket_cnt++;
	}

	else {
		mexPrintf("Usage:\n");
		mexPrintf(" zmq('publish', [endpoint]')\n");
		mexPrintf(" zmq('subscribe', [endpoint]')\n");
		mexPrintf(" zmq('poll', [timeout])\n");
		mexPrintf(" zmq('send', [socket_id], [msg])\n");
		mexPrintf(" zmq('recv', [socket_id])\n");
		mexPrintf(" zmq('fd', [fd])\n");
	}

	mxFree(command);
}
