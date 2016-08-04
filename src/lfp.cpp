#include <signal.h>	// for signal, SIGINT
#include <string>
#include <cstring>
#include <zmq.h>
#include "zmq_packet.h"
#include "util.h"
#include "filter.h"

bool s_interrupted = false;
std::vector <void *> g_socks;

static void s_signal_handler(int)
{
	s_interrupted = true;
}

static void s_catch_signals(void)
{
	struct sigaction action;
	action.sa_handler = s_signal_handler;
	action.sa_flags = 0;
	sigemptyset(&action.sa_mask);
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGQUIT, &action, NULL);
	sigaction(SIGTERM, &action, NULL);
}

static void die(void *ctx, int status)
{
	s_interrupted = true;
	for (auto &sock : g_socks) {
		zmq_close(sock);
	}
	zmq_ctx_destroy(ctx);
	exit(status);
}

int main(int argc, char *argv[])
{
	// lfp [band=(beta|gamma|hgamma)] [zmq in] [zmq out]

	if (argc < 4) {
		printf("\nlfp - LFP band-pass filter (IIR, 4th order Butterworth)\n");
		printf("usage: lfp [band=(beta|gamma|hgamma)] [zmq in] [zmq out]\n\n");
		printf("Beta is 12-30 Hz\nGamma 30-60 Hz\nHigh Gamma 60-200 Hz\n\n");
		return 1;
	}

	int lo, hi;
	if (strcmp(argv[1],"beta") == 0) {
		lo = 12;
		hi = 30;
	} else if (strcmp(argv[1],"gamma") == 0) {
		lo = 30;
		hi = 60;
	} else if (strcmp(argv[1],"hgamma") == 0) {
		lo = 60;
		hi = 200;
	} else {
		printf("filter band (%s) is unknown\n", argv[1]);
		return 1;
	}

	std::string zin  = argv[2];
	std::string zout = argv[3];

	debug("lo cutoff: %d", lo);
	debug("hi cutoff: %d", hi);
	debug("ZMQ SUB: %s", zin.c_str());
	debug("ZMQ PUB: %s", zout.c_str());

	s_catch_signals();

	void *zcontext = zmq_ctx_new();
	if (zcontext == NULL) {
		error("zmq: could not create context");
		return 1;
	}

	// we don't need 1024 sockets
	if (zmq_ctx_set(zcontext, ZMQ_MAX_SOCKETS, 64) != 0) {
		error("zmq: could not set max sockets");
		die(zcontext, 1);
	}

	void *po8e_query_sock = zmq_socket(zcontext, ZMQ_REQ);
	if (po8e_query_sock == NULL) {
		error("zmq: could not create socket");
		die(zcontext, 1);
	}
	g_socks.push_back(po8e_query_sock);

	if (zmq_connect(po8e_query_sock, "ipc:///tmp/po8e-query.zmq") != 0) {
		error("zmq: could not connect to socket");
		die(zcontext, 1);
	}

	u64 nnc; // num neural channels
	zmq_send(po8e_query_sock, "NNC", 3, 0);
	if (zmq_recv(po8e_query_sock, &nnc, sizeof(u64), 0) == -1) {
		die(zcontext, 0);
	}

	vector <Filter *> bandpass;

	for (size_t i=0; i<nnc; i++) {
#if defined KHZ_24
		switch (lo+hi) {
		case 42: // beta band
			bandpass.push_back(new FilterButterBand_24k_12_30());
			break;
		case 90: // gamma band
			bandpass.push_back(new FilterButterBand_24k_30_60());
			break;
		case 260: // high gamma band
			bandpass.push_back(new FilterButterBand_24k_60_200());
			break;
		case 3300:
			bandpass.push_back(new FilterButterBand_24k_300_3000());
			break;
		case 5300:
			bandpass.push_back(new FilterButterBand_24k_300_5000());
			break;
		case 3500:
			bandpass.push_back(new FilterButterBand_24k_500_3000());
			break;
		case 5500:
			bandpass.push_back(new FilterButterBand_24k_500_5000());
			break;
		}
#elif defined KHZ_48
		switch (lo+hi) {
		case 3300:
			bandpass.push_back(new FilterButterBand_48k_300_3000());
			break;
		case 5300:
			bandpass.push_back(new FilterButterBand_48k_300_5000());
			break;
		case 3500:
			bandpass.push_back(new FilterButterBand_48k_500_3000());
			break;
		case 5500:
			bandpass.push_back(new FilterButterBand_48k_500_5000());
			break;
		}
#else
#error Bad sampling rate!
#endif
	}

	void *socket_in = zmq_socket(zcontext, ZMQ_SUB);
	if (socket_in == NULL) {
		error("zmq: could not create socket");
		die(zcontext, 1);
	}
	g_socks.push_back(socket_in);

	if (zmq_connect(socket_in, zin.c_str()) != 0) {
		error("zmq: could not connect to socket");
		die(zcontext, 1);
	}
	// subscribe to everything
	if (zmq_setsockopt(socket_in, ZMQ_SUBSCRIBE, "", 0) != 0) {
		error("zmq: could not set socket options");
		die(zcontext, 1);
	}

	void *socket_out = zmq_socket(zcontext, ZMQ_PUB);
	if (socket_out == NULL) {
		error("zmq: could not create socket");
		die(zcontext, 1);
	}
	g_socks.push_back(socket_out);
	if (zmq_bind(socket_out, zout.c_str()) != 0) {
		error("zmq: could not bind to socket");
		die(zcontext, 1);
	}

	// init poll set
	zmq_pollitem_t items [] = {
		{ socket_in, 0, ZMQ_POLLIN, 0 }
	};

	while (!s_interrupted) {

		if (zmq_poll(items, 1, -1) == -1) {
			break;
		}

		if (items[0].revents & ZMQ_POLLIN) {
			zmq_msg_t msg;
			zmq_msg_init(&msg);
			zmq_msg_recv(&msg, socket_in, 0);

			zmq_cont_packet *p = (zmq_cont_packet *)zmq_msg_data(&msg);
			size_t nbytes = zmq_msg_size(&msg);

			u64 nc = p->nc;
			u64 ns = p->ns;

			float *f = &(p->f); // for convenience

			for (size_t i=0; i<nc; i++) {
				bandpass[i]->Proc(&(f[i*ns]), &(f[i*ns]), ns);
			}

			zmq_send(socket_out, p, nbytes, 0);
			zmq_msg_close(&msg);
		}

	}

	for (auto &x : bandpass) {
		delete x;
	}

	die(zcontext, 0);

}