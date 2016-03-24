/*
recfsusb2n for Fedora 12 Linux 2.6
http://tri.dw.land.to/fsusb2n/
2009-09-14 20:22
2011-04-06 23:50
*/

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>

#include <iostream>
#include <sys/ipc.h>
#include <sys/msg.h>

#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "ktv.hpp"
#include "decoder.h"

#ifdef B25
#define _REAL_B25_
#include "B25Decoder.hpp"
#endif /* defined(B25) */
#ifdef TSSL
#include "tssplitter_lite.h"
#endif /* defined(TSSL) */

/* maximum write length at once */
#define SIZE_CHANK 1316

/* ipc message size */
#define MSGSZ     255


/* usageの表示 */
void usage(char *argv0)
{
	std::cerr << "usage:\n" << argv0
		<< " [-v]"
#ifdef B25
		<< " [--b25]"
#endif /* defined(B25) */
#ifdef TSSL
		<< " [--sid n1,n2,...]"
#endif /* defined(TSSL) */
#ifdef HTTP
		<< " [--http portnumber]"
#endif /* defined(HTTP) */
		<< " [--wait n] channel recsec destfile\n" << std::endl;

	std::cerr << "Remarks:\n"
			<< "if rectime  is '-', records indefinitely.\n"
			<< "if destfile is '-', stdout is used for output.\n" << std::endl;

	std::cerr << "Options:" << std::endl;
	std::cerr << "-v:                 " << std::endl;
#ifdef B25
	std::cerr << "--b25:              Decrypt using BCAS card" << std::endl;
#endif /* defined(B25) */
#ifdef TSSL
	std::cerr << "--sid n1,n2,...:    Specify SID number or keywords(all,hd,sd1,sd2,sd3,1seg,epg) in CSV format" << std::endl;
#endif /* defined(TSSL) */
#ifdef HTTP
	std::cerr << "--http portnumber:  Turn on http broadcasting (run as a daemon)" << std::endl;
#endif /* defined(HTTP) */
	std::cerr << "--wait n:           Wait insert before of recording (1=100mSec)" << std::endl;
	exit(1);
}

EM2874Device		*usbDev;
KtvDevice			*pDev;
#ifdef B25
B25Decoder			b25dec;
#endif /* defined(B25) */
#ifdef TSSL
splitter			*sp = NULL;
static splitbuf_t	splitbuf;
int					split_select_finish = TSS_ERROR;
#endif /* defined(TSSL) */
time_t				time_start;	// 開始時間

/* オプション情報 */
struct Args {
	bool stdout;
	int channel;
	bool forever;
	int recsec;
	char* destfile;
	bool verbose;
#ifdef B25
	bool b25;
#endif /* defined(B25) */
#ifdef TSSL
	bool splitter;
	char sid_list[32];
#endif /* defined(TSSL) */
#ifdef HTTP
	bool http;
	int port_http;
#endif /* defined(HTTP) */
	int waitcnt;
};
Args args = {
	false,
	0,
	false,
	0,
	NULL,
	false,
#ifdef B25
	false,
#endif /* defined(B25) */
#ifdef TSSL
	false,
	{0},
#endif /* defined(TSSL) */
#ifdef HTTP
	false,
	12345,
#endif /* defined(HTTP) */
	0
};

/* オプションの解析 */
void parseOption(int argc, char *argv[])
{
	while (1) {
		int option_index = 0;
		static option long_options[] = {
#ifdef B25
			{ "b25",      0, NULL, 'b' },
			{ "B25",      0, NULL, 'b' },
#endif /* defined(B25) */
#ifdef TSSL
			{ "sid",      1, NULL, 'i' },
#endif /* defined(TSSL) */
#ifdef HTTP
			{ "http",     1, NULL, 'H' },
#endif /* defined(HTTP) */
			{ "wait",     1, NULL, 'w' },
			{ 0,     0, NULL, 0   }
		};
		
		int r = getopt_long(argc, argv,
							"vbi:H:w:",
							long_options, &option_index);
		if (r < 0) {
			break;
		}
		
		switch (r) {
			case 'v':
				args.verbose = true;
				break;
#ifdef B25
			case 'b':
				args.b25 = true;
				break;
#endif /* defined(B25) */
#ifdef TSSL
			case 'i':
				args.splitter = true;
				strcpy( args.sid_list, optarg );
				break;
#endif /* defined(TSSL) */
#ifdef HTTP
			case 'H':
				args.http      = TRUE;
				args.port_http = atoi(optarg);
				args.forever   = true;
				fprintf(stderr, "creating a http daemon\n");
				break;
#endif /* defined(HTTP) */
			case 'w':
				args.waitcnt = atoi(optarg);
				break;
			default:
				break;
		}
	}
	
#ifdef HTTP
	if( !args.http ){
#endif /* defined(HTTP) */
		if (argc - optind != 3) {
			usage(argv[0]);
		}
		
		char* chstr    = argv[optind++];
		args.channel   = atoi(chstr);
		char *recsecstr = argv[optind++];
		if (strcmp("-", recsecstr) == 0) {
			args.forever = true;
		}
		args.recsec    = atoi(recsecstr);
		args.destfile = argv[optind++];
		if (strcmp("-", args.destfile) == 0) {
			args.stdout = true;
		}
#ifdef HTTP
	}
#endif /* defined(HTTP) */
}


static bool caughtSignal = false;

void sighandler(int arg)
{
	caughtSignal = true;
}


typedef struct Mmsgbuf {
	long	mtype;
	char	mtext[MSGSZ];
} message_buf;

/* ipc message receive */
void mq_recv(int msqid)
{
	message_buf rbuf;
	char channel[16];
	char service_id[32] = {0};
	int ch = 0, recsec = 0, time_to_add = 0;

	if(msgrcv(msqid, &rbuf, MSGSZ, 1, IPC_NOWAIT) < 0) {
		return;
	}

	sscanf(rbuf.mtext, "ch=%s t=%d e=%d sid=%s", channel, &recsec, &time_to_add, service_id);
	ch = atoi(channel);
//	fprintf(stderr, "%s >> ch=%d time=%d extend=%d sid=%s\n", rbuf.mtext, ch, recsec, time_to_add, service_id);

	if(ch && args.channel != ch) {
		/* stop stream */
		usbDev->stopStream();

		args.channel = ch;
		// チューニング開始
		pDev->InitTuner();
		// 周波数を計算 (UHF13ch = 473143 kHz)
		pDev->SetFrequency( (args.channel * 6000) + 395143 );
		pDev->InitDeMod();
		pDev->ResetDeMod();

#ifdef B25
		uint8_t		*buf = NULL;
		// B25Decoder flush Data
		if (args.b25) {
			b25dec.flush();
			b25dec.get((const uint8_t **)&buf);
		// B25初期化
			b25dec.setRound(4);
			b25dec.setStrip(true);
			b25dec.setEmmProcess(false);
			if(b25dec.open(usbDev) == 0) {
//				log << "B25Decoder initialized." << std::endl;
			}else{
				// エラー時b25を行わず処理続行。終了ステータス1
//				std::cerr << "disable b25 decoding." << std::endl;
//				args.b25 = false;
			}
		}
#endif /* defined(B25) */
#ifdef TSSL
		// TS splitter 再初期化
		if( args.splitter ){
			free( splitbuf.buffer );
			split_shutdown(sp);
		}
		if( strlen(service_id) ){
			args.splitter = true;
			strcpy( args.sid_list, service_id );
		}
		if( args.splitter ){
			sp = split_startup(args.sid_list);
			if( sp != NULL ){
				splitbuf.buffer          = (u_char *)malloc( LENGTH_SPLIT_BUFFER );
				splitbuf.allocation_size = LENGTH_SPLIT_BUFFER;
				split_select_finish      = TSS_ERROR;
			}else
				args.splitter = false;
		}
#endif /* defined(TSSL) */

		// 受信安定化待ち
		int timeout = 100;
		do {
			usleep(100000);
			if (--timeout <= 0) {
				fprintf(stderr, "GetSequenceState timeout." );
				exit(1);
			}
		} while(pDev->DeMod_GetSequenceState() < 9 && !caughtSignal);
		if (args.waitcnt){
			usleep(100000*args.waitcnt);
		}

		/* restart recording */
		usbDev->startStream();
#ifdef TSSL
	}else{
		if( strlen(service_id) ){
			// TS splitter 再初期化
			if( args.splitter ){
				free( splitbuf.buffer );
				split_shutdown(sp);
			}else
				args.splitter = true;
			strcpy( args.sid_list, service_id );
			sp = split_startup(args.sid_list);
			if( sp != NULL ){
				splitbuf.buffer          = (u_char *)malloc( LENGTH_SPLIT_BUFFER );
				splitbuf.allocation_size = LENGTH_SPLIT_BUFFER;
				split_select_finish      = TSS_ERROR;
			}else
				args.splitter = false;
		}
#endif /* defined(TSSL) */
	}

	if(time_to_add) {
		args.recsec += time_to_add;
		fprintf(stderr, "Extended %d sec\n", time_to_add);
	}

	if(recsec) {
		time_t cur_time;
		time(&cur_time);
		if(cur_time - time_start <= recsec) {
			args.recsec = recsec;
			fprintf(stderr, "Total recording time = %d sec\n", recsec);
		}
	}
}

#ifdef HTTP
//read 1st line from socket
void read_line(int socket, char *p){
	while (1){
		int ret;
		ret = read(socket, p, 1);
		if ( ret == -1 ){
			perror("read");
			exit(1);
		} else if ( ret == 0 ){
			break;
		}
		if ( *p == '\n' ){
			p++;
			break;
		}
		p++;
	}
	*p = '\0';
}
#endif /* defined(HTTP) */

int main(int argc, char **argv)
{
	int					dest = 0;
	int					msqid;
#ifdef TSSL
	ARIB_STD_B25_BUFFER	ubuf;
	int					code = TSS_SUCCESS;
#endif /* defined(TSSL) */
#ifdef HTTP
	int					new_ch = 0;
#endif /* defined(HTTP) */

	parseOption(argc, argv);
	if (!args.forever && args.recsec <= 0) {
		std::cerr << "recsec must be (recsec > 0)." << std::endl;
		exit(1);
	}
	// ログ出力先設定
	std::ostream& log = args.stdout ? std::cerr : std::cout;
	log << "recfsusb2n ver. 0.9.2" << std::endl << "ISDB-T DTV Tuner FSUSB2N" << std::endl;
	EM2874Device::setLog(&log);

	usbDev = EM2874Device::AllocDevice();
	if(usbDev == NULL)
		return 1;
	usbDev->initDevice2();

	if(usbDev->getDeviceID() == 2) {
		pDev = new Ktv2Device(usbDev);
	}else{
		pDev = new Ktv1Device(usbDev);
	}

	// SIGINT, SIGTERM
	struct sigaction sa;
	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = sighandler;
	sa.sa_flags = SA_RESTART;
	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGPIPE, &sa, NULL);

#ifdef HTTP
	int connected_socket, listening_socket = 0;
	if( !args.http ){
#endif /* defined(HTTP) */
		// 出力先ファイルオープン
		if(!args.stdout) {
			dest = open(args.destfile, (O_RDWR | O_CREAT | O_TRUNC), 0666);
			if (0 > dest) {
				std::cerr << "can't open file '" << args.destfile << "' to write." << std::endl;
				exit(1);
			}
		}else
			dest = 1;	// stdout;
#ifdef HTTP
	}else{
		struct sockaddr_in	sin;
		int					sock_optval = 1;
		int					ret;

		fprintf(stderr, "run as a daemon..\n");
		if(daemon(1,1)){
			perror("failed to start");
			exit(1);
		}

		listening_socket = socket(AF_INET, SOCK_STREAM, 0);
		if ( listening_socket == -1 ){
			perror("socket");
			exit(1);
		}

		if ( setsockopt(listening_socket, SOL_SOCKET, SO_REUSEADDR, &sock_optval, sizeof(sock_optval)) == -1 ){
			perror("setsockopt");
			exit(1);
		}

		sin.sin_family = AF_INET;
		sin.sin_port = htons(args.port_http);
		sin.sin_addr.s_addr = htonl(INADDR_ANY);

		if ( bind(listening_socket, (struct sockaddr *)&sin, sizeof(sin)) < 0 ){
			perror("bind");
			exit(1);
		}

		ret = listen(listening_socket, SOMAXCONN);
		if ( ret == -1 ){
			perror("listen");
			exit(1);
		}
		fprintf(stderr,"listening at port %d\n", args.port_http);
	}
#endif /* defined(HTTP) */
	/* spawn ipc */
	key_t key = (key_t)getpid();

	if ((msqid = msgget(key, IPC_CREAT | 0666)) < 0) {
		perror("msgget");
	}
	log << "pid = " << key << std::endl;
	/* delete message queue*/

	while(1){
#ifdef HTTP
		if( args.http ){
			struct hostent		*peer_host;
			struct sockaddr_in	peer_sin;
			unsigned int		len;
			char				buffer[256];
			char				s0[256],s1[256],s2[256];
			char				delim[] = "/";
			char				*channel;
			char				*sidflg;

			len = sizeof(peer_sin);
			connected_socket = accept(listening_socket, (struct sockaddr *)&peer_sin, &len);
			if ( connected_socket == -1 ){
				perror("accept");
				exit(1);
			}

			peer_host = gethostbyaddr((char *)&peer_sin.sin_addr.s_addr, sizeof(peer_sin.sin_addr), AF_INET);
			if ( peer_host == NULL ){
				fprintf(stderr, "gethostbyname failed\n");
				exit(1);
			}
			fprintf(stderr,"connect from: %s [%s] port %d\n", peer_host->h_name, inet_ntoa(peer_sin.sin_addr), ntohs(peer_sin.sin_port));

			read_line(connected_socket, buffer);
			fprintf(stderr,"request command is %s\n",buffer);
			sscanf(buffer,"%s%s%s",s0,s1,s2);
			channel = strtok(s1,delim);
			fprintf(stderr,"channel is %s\n",channel);
			new_ch = atoi(channel);
			sidflg = strtok(NULL,delim);
			fprintf(stderr,"sidflg is %s\n",sidflg);
			if(sidflg){
				args.splitter = TRUE;
				strcpy( args.sid_list, sidflg );
			}
			char header[] =  "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nCache-Control: no-cache\r\n\r\n";
			write(connected_socket, header, strlen(header));
			//set write target to http
			dest = connected_socket;
		}
		if( new_ch != args.channel ){
			// チューニング開始
			if( new_ch != 0 )
				args.channel = new_ch;
			else
				new_ch = args.channel;
#endif /* defined(HTTP) */
			pDev->InitTuner();
			// 周波数を計算 (UHF13ch = 473143 kHz)
			pDev->SetFrequency( (args.channel * 6000) + 395143 );
			pDev->InitDeMod();
			pDev->ResetDeMod();
#ifdef HTTP
		}
#endif /* defined(HTTP) */
#ifdef B25
		// B25初期化
		if (args.b25) {
			b25dec.setRound(4);
			b25dec.setStrip(true);
			b25dec.setEmmProcess(false);
			if(b25dec.open(usbDev) == 0) {
				log << "B25Decoder initialized." << std::endl;
			}else{
				// エラー時b25を行わず処理続行。終了ステータス1
				std::cerr << "disable b25 decoding." << std::endl;
				args.b25 = false;
			}
		}
#endif /* defined(B25) */
#ifdef TSSL
		/* initialize splitter */
		if(args.splitter) {
			sp = split_startup(args.sid_list);
			if(sp != NULL) {
				splitbuf.buffer = (u_char *)malloc( LENGTH_SPLIT_BUFFER );
				splitbuf.allocation_size = LENGTH_SPLIT_BUFFER;
				split_select_finish      = TSS_ERROR;
			}else{
				args.splitter = false;
				std::cerr << "Cannot start TS splitter." << std::endl;
			}
		}
#endif /* defined(TSSL) */

		// チューニング完了・受信安定化待ち
		int timeout = 100;
		uint8_t		seq_state;
		unsigned int	hi_qua = 0, gt_qua;

		do {
			usleep(100000);
			if (--timeout <= 0) {
				log << "GetSequenceState timeout." << std::endl;
				exit(1);
			}
			seq_state = pDev->DeMod_GetSequenceState();
			gt_qua    = pDev->DeMod_GetQuality();
			if( seq_state>=8 && gt_qua> hi_qua )
				hi_qua = gt_qua;
			if(args.verbose)
				log << "Sequence = " << (unsigned)seq_state << ", Quality = " << 0.02*gt_qua << std::endl;
		} while(seq_state < 9 && !caughtSignal);
		if (args.waitcnt){
			timeout = 0;
			do {
				usleep(100000);
				seq_state = pDev->DeMod_GetSequenceState();
				gt_qua    = pDev->DeMod_GetQuality();
				if( gt_qua> hi_qua )
					hi_qua = gt_qua;
				if(args.verbose)
					log << "Sequence = " << (unsigned)seq_state << ", Quality = " << 0.02*gt_qua << std::endl;
			} while(++timeout<args.waitcnt);
		}
		// 受信開始
		usbDev->startStream();
		// 録画時間の基準開始時間
		time_start = time(NULL);

		uint8_t		*buf = NULL;
		int			rlen;

		if ( args.waitcnt < 20){
			// 受信安定化待ち
			int			sanity = 0;
			timeout = 0;
			do{
				usleep(50000);
				gt_qua    = pDev->DeMod_GetQuality();
				if(args.verbose)
					log << "Sequence = " << sanity  << ", Quality = " << 0.02*gt_qua << "    " << 0.02*hi_qua << std::endl;
				if( gt_qua> hi_qua ){
					if( hi_qua < gt_qua*8/10 ){
						hi_qua = gt_qua;
						rlen = usbDev->getStream((const void **)&buf);
						sanity  = 0;
						// 録画時間の基準開始時間
						time_start = time(NULL);
						continue;
					}else
						hi_qua = gt_qua;
				}else
					if( gt_qua < hi_qua*8/10 ){
						rlen = usbDev->getStream((const void **)&buf);
						sanity  = 0;
						// 録画時間の基準開始時間
						time_start = time(NULL);
						continue;
					}
				if( ++sanity >= 10*2 )
					break;
			}while( !caughtSignal && ++timeout<20*2 );
			buf = NULL;
		}

		// Main loop
		while (!caughtSignal && (args.forever || time(NULL) <= time_start + args.recsec)) {
			if( buf != NULL )
				usleep(500000);
			rlen = usbDev->getStream((const void **)&buf);

			if (0 == rlen) continue;
#ifdef B25
			// B25を経由して受け取る
			if (args.b25) {
				uint8_t *b25buf;
				b25dec.put(buf, rlen);
				rlen = b25dec.get((const uint8_t **)&b25buf);
				if (0 == rlen) {
					continue;
				}
				buf = b25buf;
			}
#endif /* defined(B25) */
#ifdef TSSL
			if (args.splitter) {
				splitbuf.size = 0;
				if( splitbuf.allocation_size < rlen ){
					free( splitbuf.buffer );
					splitbuf.buffer = (u_char *)malloc( rlen );
					splitbuf.allocation_size = rlen;
				}
				ubuf.size = rlen;
				ubuf.data = buf;

				/* 分離対象PIDの抽出 */
				if(split_select_finish != TSS_SUCCESS) {
					split_select_finish = split_select(sp, &ubuf);
					if(split_select_finish == TSS_NULL) {
						/* mallocエラー発生 */
						log << "split_select malloc failed" << std::endl;
						args.splitter = false;
						rlen = ubuf.size;
						buf = ubuf.data;
						free( splitbuf.buffer );
						split_shutdown(sp);
						goto fin;
					}
					else if(split_select_finish != TSS_SUCCESS) {
						/* 分離対象PIDが完全に抽出できるまで出力しない
						 * 1秒程度余裕を見るといいかも
						 */
						time_t cur_time;
						time(&cur_time);
						if(cur_time - time_start > 4) {
							args.splitter = false;
							rlen = ubuf.size;
							buf = ubuf.data;
							free( splitbuf.buffer );
							split_shutdown(sp);
							goto fin;
						}
						// 保持しないといかんかな？
						ubuf.size = 0;
						continue;
					}
				}
				/* 分離対象以外をふるい落とす */
				code = split_ts(sp, &ubuf, &splitbuf);
				if(code == TSS_NULL) {
//					split_select_finish = TSS_ERROR;
					log << "PMT reading.." << std::endl;
				}
				else if(code != TSS_SUCCESS) {
					log << "split_ts failed" << std::endl;
				}

				rlen = splitbuf.size;
				buf = (uint8_t *)splitbuf.buffer;
fin:;
			}
#endif /* defined(TSSL) */

			if(args.verbose) {
				log << "Sequence = " << (unsigned)pDev->DeMod_GetSequenceState() << ", Quality = " << 0.02*pDev->DeMod_GetQuality() << ", " << rlen << "bytes wrote." << std::endl;
			}
			while(rlen > 0) {
				ssize_t wc;
				int ws = rlen < SIZE_CHANK ? rlen : SIZE_CHANK;

				wc = write(dest, buf, ws);
				if(wc < 0) {
					log << "write failed." << std::endl;
					break;
				}
				rlen -= wc;
				buf += wc;
			}
			mq_recv(msqid);
		}
		if (caughtSignal) {
#ifdef HTTP
			if( args.http )
				caughtSignal = false;
			else
#endif /* defined(HTTP) */
				log << "interrupted." << std::endl;
		}

		usbDev->stopStream();

		/* delete message queue*/
		msgctl(msqid, IPC_RMID, NULL);
		rlen = 0;
		buf = NULL;


#ifdef B25
		// B25Decoder flush Data
		if (args.b25) {
			b25dec.flush();
			rlen = b25dec.get((const uint8_t **)&buf);
		}
#endif /* defined(B25) */
#ifdef TSSL
		if(args.splitter) {
			if( rlen ){
				if( splitbuf.allocation_size < rlen ){
					free( splitbuf.buffer );
					splitbuf.buffer = (u_char *)malloc( rlen );
					splitbuf.allocation_size = rlen;
				}
				ubuf.size = rlen;
				ubuf.data = buf;
				/* 分離対象以外をふるい落とす */
				code = split_ts(sp, &ubuf, &splitbuf);
				if(code == TSS_NULL) {
//	//				split_select_finish = TSS_ERROR;
					log << "PMT reading.." << std::endl;
				}else {
					if(code == TSS_SUCCESS) {
						log << "split_ts failed" << std::endl;
					}
				}
				rlen = splitbuf.size;
				buf = (uint8_t *)splitbuf.buffer;
			}
			free( splitbuf.buffer );
			split_shutdown(sp);
		}
#endif /* defined(TSSL) */
		while(rlen > 0) {
			ssize_t wc;
			int ws = rlen < SIZE_CHANK ? rlen : SIZE_CHANK;

			wc = write(dest, buf, ws);
			if(wc < 0) {
				log << "write failed." << std::endl;
				break;
			}
			rlen -= wc;
			buf += wc;
		}
#ifdef HTTP
		if( args.http ){
			/* close http socket */
			close(dest);
			fprintf(stderr,"connection closed. still listening at port %d\n",args.port_http);
		}else
#endif /* defined(HTTP) */
			break;
	}
	// Default Signal Handler
	struct sigaction saDefault;
	memset(&saDefault, 0, sizeof(struct sigaction));
	saDefault.sa_handler = SIG_DFL;
	sigaction(SIGINT,  &saDefault, NULL);
	sigaction(SIGTERM, &saDefault, NULL);
	sigaction(SIGPIPE, &saDefault, NULL);
	// 録画時間の測定
	time_t time_end = time(NULL);

	if(!args.stdout ) {
		close(dest);
	}

	log << "done." << std::endl;
	log << "Rec time: " << static_cast<unsigned>(time_end - time_start) << " sec." << std::endl;
	delete pDev;
	delete usbDev;
	return 0;
}

/* */

