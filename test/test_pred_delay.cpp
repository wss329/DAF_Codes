#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

#include <Utilities.h>
#include <DelayEnc.h>
#include <DelayDec.h>
#include <LDPCStruct.h>
#include <bats.h>


float LOSS_RATE;// = 0.05;
double ETA = 1.0;//0.94;
long data_rate;// = 350000; // data rate (byte / s)
float modeSelect;// = 0.0; // force the mode to be a value between [-1,1],
						 // to enable optimized values, give a number outside that interval

int dt;// = 5;
int window_size;// = 10;
int pkt_size;// = 1024;
double code_rate;
int seed;

int pkt_num = 1024;
int batch_size = 1;
int ff_order = 8;

int frame_rate;
int frame_num;
//int coded_pkt_num = 0;
int coded_window_num = 0;

long evalFrom = 0;
long evalTo = 0;


typedef struct HeaderType {
	PosType from;
	PosType window;
	ModeType mode;
} headerType;

int run_test_file(DelayEncoder *encoder, DelayDecoder *decoder, double loss_rate)
{
	int coded_len = (batch_size * ff_order / SYMBOLSIZE) + pkt_size + sizeof(KeyType)
						+ 2*sizeof(PosType) + sizeof(ModeType);

	SymbolType *packet = NULL;
	SymbolType *coded_buffer = new SymbolType[coded_len];
	int cnt = 0;
	int c1_sent = 0;
	int c2_received = 0;
	double eta = ETA;

	MTRand *psrand_1 = new MTRand();

	cout << "Start test: eta: "<< eta <<", loss rate: " << loss_rate << endl;

	while (!decoder->complete(eta)) {

		packet = encoder->genPacket();
		c1_sent++;
		if (psrand_1->rand() > loss_rate) {
			memcpy(coded_buffer, packet, coded_len);
			decoder->receivePacket(coded_buffer);
			c2_received++;
			if (decoder->complete(eta)) {
				decoder->logRankDist();
				break;
			}
		}
	} /* end while */

	delete [] coded_buffer;
	delete psrand_1;
	cout << "packet sent: " << c1_sent << " packet received: " << c2_received << endl;
	return c2_received;
}

int run_test_slide(DelayEncoder *encoder,
		DelayDecoder *decoder,
		int coded_window_num,
		headerType scheduler[],
		double loss_rate)
{
	int coded_len = (batch_size * ff_order / SYMBOLSIZE) + pkt_size + sizeof(KeyType)
						+ 2*sizeof(PosType) + sizeof(ModeType);

	SymbolType *packet = NULL;
	SymbolType *coded_buffer = new SymbolType[coded_len];
	int cnt = 0;
	int c1_sent = 0;
	int add_sent = 0;
	int c2_received = 0;
	double eta = ETA;
	//int NPacketInWindow = (int)ceil(((double)(data_rate*dt))/((double)frame_rate)/((double)pkt_size));
	double NPacketInWindow = ((double)pkt_num) /( code_rate * ((double)coded_window_num));
	double RestPktInWindow = 0.0;
	int NInWindow = 0;

	MTRand *psrand_1 = new MTRand();

	cout << "Start sliding test: eta: "<< eta <<", loss rate: " << loss_rate << endl;

	for (int i = 1; i <= coded_window_num; i++){
		RestPktInWindow += NPacketInWindow;
		NInWindow = ceil(RestPktInWindow); // how many coded packets are sent within this window[i]
		for (int j=1; j <=NInWindow; j++){
			RestPktInWindow -= 1.0;
			packet = encoder->genPacket(scheduler[i].from, scheduler[i].window, scheduler[i].mode);
			c1_sent++;
			if (psrand_1->rand() > loss_rate) {
				memcpy(coded_buffer, packet, coded_len);
				decoder->receivePacket(coded_buffer,false);
				c2_received++;
			}
		}
	} /* end for */

	decoder->complete(eta);
	//cout << "packets sent: " << c1_sent  << ", decode rate after sliding: " << decoder->complete_flag << endl;
	cout << "packets sent: " << c1_sent  << ", packets received: " << c2_received << endl;
	cout << "decode rate after sliding:\t" << decoder->completeInEval() << endl;
	cout << "In-time decode rate:\t" << decoder->completeInTime() << endl;

	FILE *fileResult = fopen ("tempResult.txt", "w");
	fprintf(fileResult,"%f\t",decoder->completeInEval());
	fclose(fileResult);

	fileResult = fopen ("tempInTimeResult.txt", "w");
	fprintf(fileResult,"%f\t",decoder->completeInTime());
	fclose(fileResult);

	/*
	while ( (!decoder->complete(eta)) && decoder->completeInEval()<eta) {
	//while (!decoder->complete(eta)) {
		packet = encoder->genPacket(0,0,0);//(evalFrom, evalFrom - evalTo + 1, 0); // send more packets to finish decoding
		add_sent++;
		if (psrand_1->rand() > loss_rate) {
			memcpy(coded_buffer, packet, coded_len);
			decoder->receivePacket(coded_buffer,true); // the last 'true' flag turns on inactive decoding
			c2_received++;
			if (decoder->complete(eta) || decoder->completeInEval() >= eta) {
				decoder->logRankDist();
				break;
			}
		}
	}

	delete [] coded_buffer;
	delete psrand_1;
	cout <<"additional packets sent for complete decode: "<< add_sent << ", packets received: " << c2_received << endl;
	*/
	return c2_received;
}

void setup_degree(BatsBasic *role)
{
	role->selectDegree();
}

int main(int argc, char *argv[])
{
	int received_pkg = 0;
	float fraction = 0.0;
	int retval = 0;
	int fountainMode = 0; // 0 is file, 1 is slide
	int result;
	bool fixLength = false;
	bool longWindows = false;
	bool windowFromStart = false;
	long minWindow = 10000000;
	double interv = 10.0;
	int group = 0;
	int span = 6;
	int multiplier = 1; // it multiplies the size of input file by certain times

	// library level init
	bats_init();

	if(argc != 7 && argc != 8 && argc != 9) {
		fputs ("The input parameters should be: <NAME> <LossRate> <WindowSize> <dt> <CodeRate> <mode> [<seed> [<multiplier>]]",stderr);
		exit(1);
	}

	char * videoName = argv[1];
	char inputName[100];
	char fullName[100];
	char infoName[100];
	//char fileName[100];
	char schedName[100];
	strcpy(fullName, videoName);
	strcat(fullName,"_W");
	strcat(fullName,argv[3]);
	strcat(fullName,"_dt");
	strcat(fullName,argv[4]);
	strcat(fullName,"_P1024");
	LOSS_RATE = atof(argv[2]);
	code_rate = atof(argv[5]);
	string schemeN(argv[6]);
	if(strcmp(argv[6],"fun3")==0){
		modeSelect = 4.0;
	}
	else if(strcmp(argv[6],"nonopt")==0){
		modeSelect = 0.0;
	}
	else if(strcmp(argv[6],"block") == 0){
		strcpy(fullName, videoName);
		strcat(fullName,"_W");
		strcat(fullName,argv[3]);
		strcat(fullName,"_dt");
		strcat(fullName,argv[3]); // window size = dt
		strcat(fullName,"_P1024");
		modeSelect = 0.0;
	}
	else if(strcmp(argv[6],"fix") ==0){
		fixLength = true;
		modeSelect = 0.0;
	}
	else if(strcmp(argv[6],"fount") ==0){
		longWindows = true;
		windowFromStart = true;
		modeSelect = 0.0;
	}
	else if(strcmp(argv[6],"expand") ==0){
		longWindows = true;
		modeSelect = 0.0;
	}
	else if(strcmp(argv[6],"raptor") ==0){
		longWindows = true;
		windowFromStart = true;
		modeSelect = 0.0;
	}
	else if(schemeN.compare(0,7,"sraptor") ==0){
		modeSelect = 4.0;
		int pos0 = schemeN.find("_")+1;;
		int pos1 = schemeN.find("_",pos0)+1;
		int pos2 = schemeN.find("_",pos1)+1;
		if(pos0!=8 || pos1 == string::npos || pos2 == string::npos){
			fputs ("Do not recognize mod arguments of sraptor.",stderr);
			exit (3);
		}
		interv = std::stod(schemeN.substr(pos0,pos1-pos0-1));
		group  = std::stoi(schemeN.substr(pos1,pos2-pos1-1));
		span   = std::stoi(schemeN.substr(pos2));
	}
	else if(argv[6][0] == '_' && (argv[6][1] == 'L'||argv[6][1] == 'N')){
		strcat(fullName, argv[6]);
		modeSelect = 4.0;
	}
	else  {
		fputs ("Do not recognize mode: select between <fun3> <nonopt> <block> <fix> <fount> <expand> <_Lx_Fx> (predict scheme) <_NMPC_Lx_Fx> (MPC scheme) <sraptor_x_x_x> <raptor>.",stderr);
		exit (3);
	}
	if(argc >= 8){
		seed = atoi(argv[7]);
	}
	else{
		seed = 0;
	}
	if(argc == 9){
		multiplier = atoi(argv[8]);
	}
	else {
		multiplier = 1;
	}


	strcpy(infoName, fullName);
	strcat(infoName,"_info.txt");
	//strcpy(fileName, fullName);
	//strcat(fileName,"_dummy.txt");
	strcpy(schedName, fullName);
	strcat(schedName,"_scheduler.txt");

	// read basic info.
	FILE * fileInfo = fopen (infoName, "r");
	if (fileInfo==NULL) {
		char buffer[100];
		sprintf(buffer,"Info file error: %s does not exist.", infoName);
		fputs (buffer,stderr);
		exit (1);
	}
	result = fscanf(fileInfo,"%s %d %d %d %d %d %d %d %ld %ld",
				inputName,
				&pkt_size,
				&frame_rate,
				&dt,
				&window_size,
				&frame_num,
				&pkt_num,
				&coded_window_num,
				&evalFrom,
				&evalTo);
	if (result != 10) {fputs ("Reading info file error",stderr); exit (3);}
	if (strcmp(inputName, videoName)!=0) {fputs ("File name does not match the info",stderr); exit (3);}
	evalFrom --;
	evalTo --;

	// Multiply the scale of each frame
	pkt_num *= multiplier;
	evalFrom *= multiplier;
	evalTo *= multiplier;


	fclose(fileInfo);

	// prepare file
	SymbolType *input = new SymbolType[pkt_num * pkt_size];
	SymbolType *output = new SymbolType[pkt_num * pkt_size];
	memset(input,'C',pkt_num * pkt_size);
	memset(output,'B',pkt_num * pkt_size);

	//FILE * inputFile = fopen(fileName,"r");
	//if (inputFile==NULL) {fputs ("Input file error",stderr); exit (1);}
	//result = fread (input,1,pkt_num * pkt_size,inputFile);
	//if (result != pkt_num * pkt_size) {fputs ("Reading file error",stderr); exit (3);}
	//for (int i = 0; i < pkt_num; i++) {
	//	input[i] = 0x5a;
	//	output[i] = 0xff;
	//}
	//fclose(inputFile);

	// read scheduler
	headerType scheduler[coded_window_num + 1];
	FILE * fileScheduler = fopen (schedName, "r");
	if (fileScheduler==NULL) {fputs ("File scheduler error",stderr); exit (1);}
	for(int i = 1; i <= coded_window_num; i++ ) {
		result = fscanf(fileScheduler, "%*d %lu %lu %f",
				&(scheduler[i].from),
				&(scheduler[i].window),
				&(scheduler[i].mode));
		if (result != 3) {fputs ("Reading scheduler error",stderr); exit (3);}
		scheduler[i].from --; // positions in de/encoder starts with 0

		// multiply
		scheduler[i].from *= multiplier;
		scheduler[i].window *= multiplier;

		if(minWindow > scheduler[i].window) {
			minWindow = scheduler[i].window;
		}
		if(modeSelect <= 1.0 && modeSelect >= -1.0) {
			// make it a constant sampling distribution if modeSelect is [-1,1]
			scheduler[i].mode = modeSelect;
		}
		//scheduler[i].window = 700;

		if(windowFromStart) {
			evalFrom = 0;
			evalTo = 0;
			scheduler[i].from = 0;
		}
		if(longWindows) {
			scheduler[i].window = 0; // 0 means largest window possible
		}
	}
	if(fixLength) {
		for(int i = 1; i <= coded_window_num; i++ ) {
			scheduler[i].window = minWindow;
		}
	}
	if(strcmp(argv[6],"raptor") ==0){
		interv = pkt_num;
		group = (int) (0.01 * pkt_num + sqrt(2*pkt_num));
		span = group;
	}
	fclose(fileScheduler);

	// setup encoder
	LDPCStruct* encodeLDPC = new LDPCStruct(pkt_num, interv, group, span, 0, 0);
	// packetNum, interval, group size, span, hdpcNum, PINum
	DelayEncoder *encoder = new DelayEncoder(pkt_num, pkt_size, input, encodeLDPC, evalFrom, evalTo, seed);
	setup_degree(encoder);

	// setup decoder
	LDPCStruct* decodeLDPC = new LDPCStruct(*encodeLDPC); // clone a same class
	DelayDecoder *decoder = new DelayDecoder(pkt_num, pkt_size, output, decodeLDPC, evalFrom, evalTo, seed);
	setup_degree(decoder);


	//received_pkg = run_test_file(encoder, decoder, LOSS_RATE);
	received_pkg = run_test_slide(encoder, decoder, coded_window_num, scheduler, LOSS_RATE);

	/*int error_Cnt = 0;
	for (int i = 0; i <= pkt_num-1; i++) {
		int bb = i*pkt_size;
		for (int j = 0; j < pkt_size; j++){
			if (input[bb + j] != output[bb + j]) {
				if(i<evalFrom || i > evalTo){
					// This should not happen! because they are padded.
					cout << "==== Pad Mem coding error : pkt: " << i<< " ====" << endl;
					cout << "Should be "<< input[bb + j] <<", but is "<< output[bb + j] << endl;
				}
				else {
					cout << "==== Mem coding error : pkt: " << i<< " ====" << endl;
					cout << "Should be "<< input[bb + j] <<", but is "<< output[bb + j] << endl;
				}
				retval = -1;
				error_Cnt ++;
				break;
			}
		}
	}

	// fraction = float(received_pkg - pkt_num) / float(pkt_num);
	fraction = float(pkt_num)/float(received_pkg);
	float error_frac = float(error_Cnt)/float(pkt_num);

	cout << "Packet Number:  " << pkt_num << endl;
	cout << "Received package: " << received_pkg <<endl;
	cout << "Finishing Code Rate: " << fraction << endl;
	cout << "Total Error Fraction: " << 1.0 - decoder->complete_flag << endl;
	cout << "Decode ratio within eval area (using BP): " << decoder->completeInEval() << endl;
	cout << "Error package within eval area: " << error_Cnt <<endl;*/

	delete [] input;
	delete [] output;
	//delete [] scheduler;
	delete encodeLDPC;
	delete decodeLDPC;
	//delete encoder;
	//delete decoder;
	return 0;
}


