#include <thread>
#include <signal.h>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <sys/types.h>
#include <time.h>
#include <comedilib.h>
#include <zmq.h>
#include <basedir.h>
#include <basedir_fs.h>
#include "lconf.h"
#include "util.h"
#include "lockfile.h"

// p1 - a single channel pulser
//
// Subscribe to a zmq socket which sends zmq packets which indicate
// action potentials as the happen
// the content of the message is not important as a pulse will be delivered
// on the DIO port immediately.

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
	sigaction(SIGINT, &action, nullptr);
	sigaction(SIGQUIT, &action, nullptr);
	sigaction(SIGTERM, &action, nullptr);
}

static void die(void *ctx, int status)
{
	s_interrupted = true;
	for (auto &sock : g_socks) {
		zmq_close(sock);
	}
	zmq_ctx_term(ctx);
	exit(status);
}

int main()
{
	s_catch_signals();

	xdgHandle xdg;
	xdgInitHandle(&xdg);
	char *confpath = xdgConfigFind("spk/spk.rc", &xdg);
	char *tmp = confpath;
	// confpath is "string1\0string2\0string3\0\0"

	luaConf conf;

	while (*tmp) {
		conf.loadConf(tmp);
		tmp += strlen(tmp) + 1;
	}
	if (confpath)
		free(confpath);
	xdgWipeHandle(&xdg);
	errno = 0;

	std::string zs = "ipc:///tmp/spk.zmq";
	conf.getString("p1.socket", zs);

	int major, minor, patch;
	zmq_version(&major, &minor, &patch);
	printf("zmq: using library version %d.%d.%d\n", major, minor, patch);

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

	void *sock = zmq_socket(zcontext, ZMQ_SUB);
	if (sock == NULL) {
		error("zmq: could not create socket");
		die(zcontext, 1);
	}
	g_socks.push_back(sock);
	printf("%s\n", zs.c_str());
	if (zmq_connect(sock, zs.c_str()) != 0) {
		error("zmq: could not connect to socket");
		die(zcontext, 1);
	}
	// subscribe to everything
	if (zmq_setsockopt(sock, ZMQ_SUBSCRIBE, "", 0) != 0) {
		error("zmq: could not set socket options");
		die(zcontext, 1);
	}

	lockfile lf("/tmp/p1.lock");
	if (lf.lock()) {
		error("executable already running");
		die(zcontext, 1);
	}

	comedi_t *card = comedi_open("/dev/comedi0");
	if (card == NULL) {
		error("comedi_open");
		die(zcontext, 1);
	}

	int nc = comedi_get_n_channels(card, 0);
	printf("max dio chans = %d\n", nc);

	for (int i=0; i<nc; i++) {
		comedi_dio_config(card, 0, i, COMEDI_OUTPUT);
	}

	zmq_pollitem_t items [] = {
		{ sock, 0, ZMQ_POLLIN, 0 }
	};

	struct timespec ts;
	ts.tv_sec = 0; // 0 seconds
	ts.tv_nsec = 100000; // 100 micorseconds

	while (!s_interrupted) {

		if (zmq_poll(items, 1, 1) == -1) { // wait 1 msec
			break;
		}

		if (items[0].revents & ZMQ_POLLIN) {

			// pulse first, as questions later
			u32 bits = 1;
			comedi_dio_bitfield2(card, 0, 0xFFFF, &bits, 0);
			nanosleep(&ts, NULL);
			bits = 0;
			comedi_dio_bitfield2(card, 0, 0xFFFF, &bits, 0);

			zmq_msg_t msg;
			zmq_msg_init(&msg);
			zmq_msg_recv(&msg, sock, 0);
			zmq_msg_close(&msg);
		}

	}

	comedi_close(card);

	lf.unlock();

	die(zcontext, 0);
}
