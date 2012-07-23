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

#include <iostream>

#include "ktv.hpp"
#include "decoder.h"

#ifdef B25
#define _REAL_B25_
	#include "B25Decoder.hpp"
#endif
#include "tssplitter_lite.h"

/* usageの表示 */
void usage(char *argv0)
{
	std::cerr << "usage: " << argv0
#ifdef B25
		<< " [--b25]"
#endif
		<< " [-v]"
		<< " [--sid n1,n2,...]"
		<< " [--trim n] channel recsec destfile" << std::endl;
	std::cerr << "--b25:            Decrypt using BCAS card" << std::endl;
	std::cerr << "-v:               " << std::endl;
	std::cerr << "--sid n1,n2,...:  Specify SID number or keywords(all,hd,sd1,sd2,sd3,1seg,epg) in CSV format" << std::endl;
	std::cerr << "--trim n:         Specified number of packets discarded at the beginning" << std::endl;
	exit(1);
}
/* オプション情報 */
struct Args {
	bool b25;
	bool stdout;
	int channel;
	bool forever;
	int recsec;
	char* destfile;
	bool verbose;
	bool splitter;
	char* sid_list;
	bool triming;
	int trimcnt;
};

/* オプションの解析 */
Args parseOption(int argc, char *argv[])
{
	Args args = {
		false,
		false,
		0,
		false,
		0,
		NULL,
		false,
		false,
		NULL,
		false,
		0
	};
	
	while (1) {
		int option_index = 0;
		static option long_options[] = {
			{ "b25",      0, NULL, 'b' },
			{ "B25",      0, NULL, 'b' },
			{ "sid",      1, NULL, 'i'},
			{ "trim",      1, NULL, 't'},
			{ 0,     0, NULL, 0   }
		};
		
		int r = getopt_long(argc, argv,
							"bvi",
							long_options, &option_index);
		if (r < 0) {
			break;
		}
		
		switch (r) {
			case 'b':
				args.b25 = true;
				break;
			case 'v':
				args.verbose = true;
				break;
			case 'i':
				args.splitter = true;
				args.sid_list = optarg;
				break;
			case 't':
				args.triming = true;
				args.trimcnt = atoi(optarg);
				break;
			default:
				break;
		}
	}
	
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
	
	return args;
}

static bool caughtSignal = false;

void sighandler(int arg)
{
	caughtSignal = true;
}


int main(int argc, char **argv)
{
	Args args = parseOption(argc, argv);

	if (!args.forever && args.recsec <= 0) {
		std::cerr << "recsec must be (recsec > 0)." << std::endl;
		exit(1);
	}
	// ログ出力先設定
	std::ostream& log = args.stdout ? std::cerr : std::cout;
	log << "recfsusb2n ver. 0.9.2" << std::endl << "ISDB-T DTV Tuner FSUSB2N" << std::endl;
	EM2874Device::setLog(&log);

	EM2874Device *usbDev = EM2874Device::AllocDevice();
	if(usbDev == NULL)
		return 1;
	usbDev->initDevice2();

	KtvDevice *pDev;
	if(usbDev->getDeviceID() == 2) {
		pDev = new Ktv2Device(usbDev);
	}else{
		pDev = new Ktv1Device(usbDev);
	}

	pDev->InitTuner();
	// 周波数を計算 (UHF13ch = 473143 kHz)
	pDev->SetFrequency( (args.channel * 6000) + 395143 );
	pDev->InitDeMod();
	pDev->ResetDeMod();

#ifdef B25
	// B25初期化
	B25Decoder b25dec;
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

	// 出力先ファイルオープン
	FILE *dest;
	if(!args.stdout) {
		dest = fopen(args.destfile, "w");
		if (NULL == dest) {
			std::cerr << "can't open file '" << args.destfile << "' to write." << std::endl;
			exit(1);
		}
	}else dest = stdout;

	// SIGINT, SIGTERM
	struct sigaction sa;
	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = sighandler;
	sa.sa_flags = SA_RESTART;
	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	do {
		sleep(1);
	} while(pDev->DeMod_GetSequenceState() < 8 && !caughtSignal);

	/* initialize splitter */
	ARIB_STD_B25_BUFFER	ubuf;
	static splitbuf_t	splitbuf;
	splitter			*sp = NULL;
	int					split_select_finish = TSS_ERROR;
	int					code = TSS_SUCCESS;
	if(args.splitter) {
		sp = split_startup(args.sid_list);
		if(sp->sid_list == NULL) {
			std::cerr << "Cannot start TS splitter." << std::endl;
			return 1;
		}
		splitbuf.buffer = (u_char *)malloc( LENGTH_SPLIT_BUFFER );
		splitbuf.allocation_size = LENGTH_SPLIT_BUFFER;
	}

	// 録画時間の基準開始時間
	time_t time_start = time(NULL);

	usbDev->startStream();

	int 		stream_counter = 0;
	uint8_t		*buf = NULL;
	int			rlen;

	if (args.triming){
		while(1){
			usleep(100000);
			stream_counter += usbDev->getStream((const void **)&buf);
			if( stream_counter / LENGTH_PACKET >= args.trimcnt) {
				time_start = time(NULL);
				log << "Remove " << stream_counter / LENGTH_PACKET << " packets(" << stream_counter << " byte)" << std::endl;
				break;
			}
		}
	}

	// Main loop
	while (!caughtSignal && (args.forever || time(NULL) <= time_start + args.recsec)) {
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
			} /* if */

		if(args.verbose) {
			log << "Sequence = " << (unsigned)pDev->DeMod_GetSequenceState() << ", Quality = " << 0.02*pDev->DeMod_GetQuality()
 << ", " << rlen << "bytes wrote." << std::endl;
		}
		if((unsigned)rlen > fwrite(buf, 1, rlen, dest)) {
			log << "fwrite failed." << std::endl;
		}
	}

	if (caughtSignal) {
		log << "interrupted." << std::endl;
	}
	usbDev->stopStream();

	// Default Signal Handler
	struct sigaction saDefault;
	memset(&saDefault, 0, sizeof(struct sigaction));
	saDefault.sa_handler = SIG_DFL;
	sigaction(SIGINT,  &saDefault, NULL);
	sigaction(SIGTERM, &saDefault, NULL);
	rlen = 0;
	buf = NULL;


#ifdef B25
	// B25Decoder flush Data
	if (args.b25) {
		b25dec.flush();
		rlen = b25dec.get((const uint8_t **)&buf);
	}
#endif /* defined(B25) */
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
//				split_select_finish = TSS_ERROR;
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
	if (0 < rlen) {
		fwrite(buf, 1, rlen, dest);
	}
	// 録画時間の測定
	time_t time_end = time(NULL);

	fflush(dest);
	if(!args.stdout) {
		fclose(dest);
	}

	log << "done." << std::endl;
	log << "Rec time: " << static_cast<unsigned>(time_end - time_start) << " sec." << std::endl;
	delete pDev;
	delete usbDev;
	return 0;
}

/* */

