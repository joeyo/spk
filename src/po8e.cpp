#include <stdio.h>
#include <signal.h>				// for signal, SIGINT
#include <math.h>
#include <cstring>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <proc/readproc.h>		// for proc_t, openproc, readproc, etc
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <armadillo>
#include <zmq.hpp>
#include "util.h"
#include "PO8e.h"
#include "timesync.h"
#include "po8e_conf.h" 			// parse po8e conf files

#if defined KHZ_24
#define SRATE_HZ	(24414.0625)
#define SRATE_KHZ	(24.4140625)
#elif defined KHZ_48
#define SRATE_HZ	(48828.1250)
#define SRATE_KHZ	(48.8281250)
#else
#error Bad sampling rate!
#endif

using namespace std;
using namespace arma;

running_stat<double>	g_po8eStats;
long double g_lastPo8eTime = 0.0;

mutex g_po8e_mutex;
size_t g_po8e_read_size = 16; // po8e block read size
string g_po8e_socket_name = "tcp://*:1337";

TimeSync 	g_ts(SRATE_HZ); //keeps track of ticks (TDT time)
bool		s_interrupted = false;
bool		g_running = false;

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

// service the po8e buffer
void po8e_thread(zmq::context_t &ctx, PO8e *p, int id)
{

	zmq::socket_t socket(ctx, ZMQ_PUB);
	std::stringstream ss;
	ss << "inproc://po8e-" << id;
	socket.bind(ss.str().c_str());

	size_t bufmax = 10000;	// must be >= 10000

	printf("Waiting for the stream to start ...\n");
	//p->waitForDataReady(1);
	while (p->samplesReady() == 0 && !s_interrupted) {
		usleep(5000);
	}

	if (p == nullptr || s_interrupted) {
		debug("Stopped collecting data");
		p->stopCollecting();
		debug("Releasing card %p", (void *)p);
		PO8e::releaseCard(p);
		return;
	}

	u64 nc = p->numChannels();
	auto bps = p->dataSampleSize(); // bytes/sample
	printf("Card %p: %lu channels @ %d bytes/sample\n", (void *)p, nc, bps);

	// 10000 samples * 2 bytes/sample * nChannels
	auto data = new i16[bufmax*(nc)];
	auto tick = new i64[bufmax];

	// get the initial tick value. hopefully a small number
	p->readBlock(data, 1, tick);
	i64 last_tick = tick[0] - 1;

	while (!s_interrupted) {

		bool stopped = false;
		size_t numSamples;
		{
			// intentional braces
			lock_guard<mutex> lock(g_po8e_mutex);
			numSamples = p->samplesReady(&stopped);
		}
		//if (stopped){
		if (p->getLastError() > 0) {
			//warn("%p: samplesReady() indicated that we are stopped: numSamples: %zu",
			//     (void*)p, numSamples);
			warn("%p: card has thrown an error: %d", (void *)p, p->getLastError());
			break; // xxx how to recover?
		}

		if (numSamples >= g_po8e_read_size) {
			if (numSamples > bufmax) {
				warn("%p: samplesReady() returned too many samples (buffer wrap?): %zu",
				     (void *)p, numSamples);
				numSamples = bufmax;
			}

			size_t ns;

			{
				// warning: these braces are intentional
				lock_guard<mutex> lock(g_po8e_mutex);
				ns = p->readBlock(data, g_po8e_read_size, tick);
				p->flushBufferedData(ns);
			}
			double ts = gettime();

			if (tick[0] != last_tick + 1) {
				warn("%p: PO8e tick glitch between blocks. Expected %zu got %zu",
				     p, last_tick+1, tick[0]);
				g_ts.m_dropped++;
				// xxx how to recover?
			}
			for (size_t i=0; i<ns-1; i++) {
				if (tick[i+1] != tick[i] + 1) {
					warn("%p: PO8e tick glitch within block. Expected %zu got %zu",
					     p, tick[i]+1, tick[i+1]);
					g_ts.m_dropped++;
					// xxx how to recover?
				}
			}
			last_tick = tick[ns-1];

			// pack message
			size_t nbytes = 32 + nc*ns*sizeof(i16);
			zmq::message_t msg(nbytes);
			char *ptr = (char *)msg.data();

			memcpy(ptr+0, &nc, sizeof(u64)); // nc - 8 bytes
			memcpy(ptr+8, &ns, sizeof(u64)); // ns - 8 bytes
			memcpy(ptr+16, tick, sizeof(i64)); // tk - 8 bytes
			memcpy(ptr+24, &ts, sizeof(double)); // ts - 8 bytes
			memcpy(ptr+32, data, nc*ns*sizeof(i16)); // data - nc*ns bytes

			socket.send(msg);

		} else {
			usleep(1e3);
		}
	}

	// delete buffers since we have dynamically allocated;
	delete[] data;
	delete[] tick;

	{
		// warning: these braces are intentional
		lock_guard<mutex> lock(g_po8e_mutex);
		debug("Stopped collecting data");
		p->stopCollecting();
		p->flushBufferedData(-1, true);
		debug("Releasing card %p", (void *)p);
		PO8e::releaseCard(p);
	}
}

void worker(zmq::context_t &ctx, vector<po8e::card *> &cards)
{

	//  Prepare our sockets

	int n = cards.size();

	vector<zmq::socket_t> socks;
	vector<zmq::pollitem_t> items;
	for (int i=0; i<n; i++) {
		socks.push_back( zmq::socket_t(ctx, ZMQ_SUB) );
		std::stringstream ss;
		ss << "inproc://po8e-" << i;
		socks[i].connect(ss.str().c_str());
		socks[i].setsockopt(ZMQ_SUBSCRIBE, "", 0);	// subscribe to everything
		zmq::pollitem_t p = {socks[i], 0, ZMQ_POLLIN, 0};
		items.push_back(p);
	}

	// for control input
	zmq::socket_t controller(ctx, ZMQ_SUB);
	controller.connect("inproc://controller");
	controller.setsockopt(ZMQ_SUBSCRIBE, "", 0);
	zmq::pollitem_t p = {controller, 0, ZMQ_POLLIN, 0};
	items.push_back(p);

	zmq::socket_t socket_out(ctx, ZMQ_PUB);	// we will publish data
	socket_out.bind(g_po8e_socket_name.c_str());

	while (true) {

		zmq::poll(items.data(), items.size(), -1);

		int nready = 0;
		for (int i=0; i<n; i++) {
			if (items[i].revents & ZMQ_POLLIN) {
				nready++;
			}
		}

		if (nready == n) {

			g_running = true;

			auto nc = new u64[n];
			auto ns = new u64[n];
			auto tk = new i64[n];
			auto ts = new double[n];
			vector<i16 *> data;

			for (int i=0; i<n; i++) {
				zmq::message_t msg;
				socks[i].recv(&msg);
				char *ptr = (char *)(msg.data());
				memcpy(&(nc[i]), ptr+0, sizeof(u64));
				memcpy(&(ns[i]), ptr+8, sizeof(u64));
				memcpy(&(tk[i]), ptr+16, sizeof(i64));
				memcpy(&(ts[i]), ptr+24, sizeof(double));
				auto x = new i16[nc[i]*ns[i]];
				memcpy(x, ptr+32, nc[i]*ns[i]*sizeof(i16));
				data.push_back(x);
			}

			bool mismatch = false;
			double time = 0.0;

			for (int i=0; i<n; i++) {
				if (nc[i] != (u64)cards[i]->channel_size()) {
					warn("po8e packet channel mismatch");
					mismatch = true;
					break;
				}
				if (ns[i] != ns[0]) {
					warn("po8e packet sample mismatch");
					mismatch = true;
					break;
				}
				if (tk[i] != tk[0]) {
					warn("p08e packet ticks misaligned");
					mismatch = true;
					break;
				}
				time += ts[i];
			}
			time /= n;

			if (mismatch) { // exit the worker; no data will be processed
				break;
				// XXX CLEANUP MEMORY HERE

			}

			// XXX TODO when to reset the running stats?

			long double last_interval = (time - g_lastPo8eTime)*1000.0; // msec
			g_po8eStats( last_interval );
			g_lastPo8eTime = time;

			// also -- only accept stort-interval responses (ignore outliers..)
			if (last_interval < 1.5*g_po8eStats.mean() ) {
				g_ts.update(time, tk[0]); //also updates the mmap file.
			}

			// package up neural data here
			// should also pack up and broadcast events data, etc below

			u64 nnc = 0; // num neural channels
			for (auto &c : cards) {
				for (int j=0; j<c->channel_size(); j++) {
					if (c->channel(j).data_type() == po8e::channel::NEURAL) {
						nnc++;
					}
				}
			}

			auto raw = new i16[nnc * ns[0]];
			size_t nc_i = 0;
			for (int i=0; i<(int)cards.size(); i++) {
				for (int j=0; j<cards[i]->channel_size(); j++) {
					if (cards[i]->channel(j).data_type() == po8e::channel::NEURAL) {
						for (size_t k=0; k<ns[0]; k++) {
							raw[nc_i*ns[0]+k] = data[i][j*ns[0]+k];
						}
						nc_i++;
					}
				}
			}

			size_t nbytes = 32 + nnc*ns[0]*sizeof(i16);
			zmq::message_t buf(nbytes);
			char *ptr = (char *)buf.data();

			memcpy(ptr+0, &nnc, sizeof(u64)); // nnc - 8 bytes
			memcpy(ptr+8, ns, sizeof(u64)); // ns - 8 bytes
			memcpy(ptr+16, tk, sizeof(i64)); // tk - 8 bytes
			memcpy(ptr+24, &time, sizeof(double)); // ts - 8 bytes
			memcpy(ptr+32, raw, nnc*ns[0]*sizeof(i16)); // data - nnc*ns bytes

			socket_out.send(buf);

			delete[] raw;

			for (auto &x : data) {
				delete[] x;
			}

			delete[] nc;
			delete[] ns;
			delete[] tk;
			delete[] ts;
		}

		if (items[n].revents & ZMQ_POLLIN) {
			// eventually check for what the message says
			//controller.recv(&buf);
			break;
		}

	}

}

int main(void)
{

	s_catch_signals();

	zmq::context_t zcontext(1);	// single zmq thread

	g_startTime = gettime();

	pid_t mypid = getpid();
	PROCTAB *pr = openproc(PROC_FILLSTAT);
	proc_t pr_info;
	memset(&pr_info, 0, sizeof(pr_info));
	while (readproc(pr, &pr_info) != NULL) {
		if (!strcmp(pr_info.cmd, "po8e") &&
		    pr_info.tgid != mypid) {
			error("already running with pid: %d", pr_info.tgid);
			closeproc(pr);
			return 1;
		}
	}
	closeproc(pr);

	auto fileExists = [](const char *f) {
		struct stat sb;
		int res = stat(f, &sb);
		if (res == 0)
			if (S_ISREG(sb.st_mode))
				return true;
		return false;
	};

	// load the lua-based po8e config
	po8eConf pc;
	bool conf_ok = false;
	if (fileExists("po8e.rc")) {
		conf_ok = pc.loadConf("po8e.rc");
	} else if (fileExists("rc/po8e.rc")) {
		conf_ok = pc.loadConf("rc/po8e.rc");
	}
	if (!conf_ok) {
		error("No config file! Aborting!");
		return 1;
	}

	g_po8e_read_size = pc.readSize();
	printf("po8e read size:\t\t%zu\n", 	g_po8e_read_size);

	g_po8e_socket_name = pc.socket();
	printf("po8e socket:\t\t%s\n", 	g_po8e_socket_name.c_str());

	size_t nc = pc.numNeuralChannels();
	printf("Neural channels:\t%zu\n", 	nc);
	printf("Event channels:\t\t%zu\n", 	pc.numEventChannels());
	printf("Analog channels:\t%zu\n", 	pc.numAnalogChannels());
	printf("Ignored channels:\t%zu\n", 	pc.numIgnoredChannels());

	if (nc == 0) {
		error("No neural channels configured!");
		return 1;
	}

	printf("\n");

	vector <thread> threads;
	vector <po8e::card *> cards;

	printf("PO8e API Version %s\n", revisionString());
	int totalcards = PO8e::cardCount();
	if (totalcards < 1) {
		error("Found %d PO8e cards!", totalcards);
		return 1;
	}
	printf("Found %d PO8e card(s)\n", totalcards);

	if (totalcards < (int)pc.cards.size()) {
		error("Config describes more po8e cards than detected!");
		return 1;
	}

	if (totalcards > (int)pc.cards.size()) {
		warn("More po8e cards detected than configured");
		totalcards = (int)pc.cards.size();
	}

	auto configureCard = [&](PO8e* p) -> bool {
		// return true on success
		// return false on failure
		if (!p->startCollecting())
		{
			warn("startCollecting() failed with: %d", p->getLastError());
			p->flushBufferedData();
			p->stopCollecting();
			debug("Releasing card %p\n", (void *)p);
			PO8e::releaseCard(p);
			return false;
		}
		debug("Card %p is collecting incoming data", (void *)p);
		return true;
	};

	for (int i=0; i<totalcards; i++) {
		if (pc.cards[i]->enabled()) {
			int id = (int)pc.cards[i]->id();
			PO8e *p = PO8e::connectToCard(id-1); // 0-indexed
			if (p == nullptr) {
				continue;
			}
			printf("Connection established to card %d at %p\n", id, (void *)p);
			if (configureCard(p)) {
				threads.push_back(thread(po8e_thread, std::ref(zcontext), p, id-1));
				cards.push_back(pc.cards[i]);
			}
		}
	}

	if (threads.size() < 1) {
		error("Connected to zero po8e cards");
		return 1;
	}

	threads.push_back(thread(worker, std::ref(zcontext), std::ref(cards)));

	zmq::socket_t controller(zcontext, ZMQ_PUB);
	controller.bind("inproc://controller");


	zmq::socket_t query(zcontext, ZMQ_REP);
	query.bind("ipc:///tmp/po8e-query.ipc");

	zmq::pollitem_t items [] = {
		{ query, 0, ZMQ_POLLIN, 0 },
	};

	while (!s_interrupted) {

		zmq::poll(items, 1, 50);	// 50 msec ie 20 hz

		zmq::message_t msg;

		if (items[0].revents & ZMQ_POLLIN) {
			query.recv(&msg);
			if (strcmp(msg.data(), "NNC")) {
				msg.rebuild(sizeof(u64)); // bytes
				u64 nnc = pc.numNerualChannels();
				memcpy(msg.data(), &nnc, sizeof(u64));
				query.send(&msg);
			}
			else {
				msg.rebuild(3);
				memcpy(msg.data(), "ERR", 3);
				query.send(&msg);
			}
		}

		if (g_running) {
			printf("ts %s", g_ts.getTime().c_str());
			printf(" | tk %d", g_ts.getTicks());
			printf(" | slope %0.3Lf", g_ts.m_slope);
			printf(" | offset %0.1Lf", g_ts.m_offset);
			printf(" | po8e %0.2f\r", g_po8eStats.mean());
			fflush(stdout);
		}
	}

	std::string s("KILL");
	zmq::message_t msg(s.size());
	memcpy(msg.data(), s.c_str(), s.size());
	controller.send(msg);

	for (auto &thread : threads) {
		thread.join();
	}

}
