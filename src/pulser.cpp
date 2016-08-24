#include <thread>
#include <signal.h>
#include <unistd.h>
#include <cstring>
#include <sys/types.h>
#include <time.h>
#include <comedilib.h>
#include <zmq.h>
#include <basedir.h>
#include <basedir_fs.h>
#include "lconf.h"
#include "util.h"
#include "lockfile.h"
#include "pulse_queue.h"

// pulser - a pulse generator / scheduler

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

	std::string zs = "ipc:///tmp/pulser.zmq";
	conf.getString("pulser.socket", zs);

	int num_stim_chans = 16;
	conf.getInt("pulser.num_stim_chans", num_stim_chans);

	double pulse_dt = 0.003; // nb seconds
	conf.getDouble("pulser.pulse_dt", pulse_dt);

	int num_simultaneous = 3;
	conf.getInt("pulser.num_simultaneous", num_simultaneous);

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

	lockfile lf("/tmp/pulser.lock");
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

	PulseQueue p(num_stim_chans);
	p.setPulseDT(pulse_dt);
	p.setNumSimultaneous(num_simultaneous);

	for (int i=0; i<num_stim_chans; i++) {
		p.setPulseRate(i+1, 0);
	}

	auto bit = [](int n) -> u16 {
		return (n < 0 || n > 15) ? 0 : 1<<n;
	};

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
			zmq_msg_t msg;
			zmq_msg_init(&msg);
			zmq_msg_recv(&msg, sock, 0);
			size_t n = zmq_msg_size(&msg);
			if (n == num_stim_chans*sizeof(i16)) {
				i16 *x = (i16 *)zmq_msg_data(&msg);
				for (int i=0; i<num_stim_chans; i++) {
					p.setPulseRate(i+1, x[i]);
				}
			} else {
				error("packet size error");
			}
			zmq_msg_close(&msg);
		}

		std::vector<int> v = p.step();

		if (!v.empty()) {
			u32 bits = 0;
			for (int i=0; i<(int)v.size(); i++) {
				bits |= bit(v[i]);
			}
			comedi_dio_bitfield2(card, 0, 0xFFFF, &bits, 0);
			nanosleep(&ts, NULL);
			bits = 0;
			comedi_dio_bitfield2(card, 0, 0xFFFF, &bits, 0);
		}
	}

	comedi_close(card);

	lf.unlock();

	die(zcontext, 0);
}
