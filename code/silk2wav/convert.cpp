#ifdef _WIN32
#define _CRT_SECURE_NO_DEPRECATE    1
#endif
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <afx.h>
#include "..\interface\SKP_Silk_SDK_API.h"
#include "..\src\SKP_Silk_SigProc_FIX.h"
using namespace std;

//��������ĳ���
#define MAX_BYTES_PER_FRAME     1024
#define MAX_INPUT_FRAMES        5
#define MAX_FRAME_LENGTH        480
#define FRAME_LENGTH_MS         20
#define MAX_API_FS_KHZ          48
#define MAX_LBRR_DELAY          2

//ת������ֽ���ΪС���ֽ���
#ifdef _SYSTEM_IS_BIG_ENDIAN
void swap_endian(
	SKP_int16       vec[],
	SKP_int         len
	)
{
	SKP_int i;
	SKP_int16 tmp;
	SKP_uint8 *p1, *p2;

	for (i = 0; i < len; i++){
		tmp = vec[i];
		p1 = (SKP_uint8 *)&vec[i]; p2 = (SKP_uint8 *)&tmp;
		p1[0] = p2[1]; p1[1] = p2[0];
	}
}
#endif

#if (defined(_WIN32) || defined(_WINCE)) 
#include <windows.h>	//��windows�У�����ʱ������api��ͷ�ļ�
#else    
#include <sys/time.h>   // Linux or Mac
#endif

//#ifdef _WIN32


////��ȡ�߾��ȵ�ʱ��ֵ
//unsigned long GetHighResolutionTime() 
//{
//
//	LARGE_INTEGER lpPerformanceCount;
//	LARGE_INTEGER lpFrequency;
//	QueryPerformanceCounter(&lpPerformanceCount);
//	QueryPerformanceFrequency(&lpFrequency);
//	return (unsigned long)((1000000 * (lpPerformanceCount.QuadPart)) / lpFrequency.QuadPart);
//}
//#else   
//unsigned long GetHighResolutionTime()
//{
//	struct timeval tv;
//	gettimeofday(&tv, 0);
//	return((tv.tv_sec * 1000000) + (tv.tv_usec));
//}
//#endif // _WIN32
static SKP_int32 rand_seed = 1;

int decode(char *bitInFileName, char *speechOutFileName)
{

	//��ʼ����������еĲ���
	size_t    counter;
	SKP_int32 totPackets, i, k;
	SKP_int16 ret, len, tot_len;
	SKP_int16 nBytes;
	SKP_uint8 payload[MAX_BYTES_PER_FRAME * MAX_INPUT_FRAMES * (MAX_LBRR_DELAY + 1)];
	SKP_uint8 *payloadEnd = NULL, *payloadToDec = NULL;
	SKP_uint8 FECpayload[MAX_BYTES_PER_FRAME * MAX_INPUT_FRAMES], *payloadPtr;
	SKP_int16 nBytesFEC;
	SKP_int16 nBytesPerPacket[MAX_LBRR_DELAY + 1], totBytes;
	SKP_int16 out[((FRAME_LENGTH_MS * MAX_API_FS_KHZ) << 1) * MAX_INPUT_FRAMES], *outPtr;
	FILE      *bitInFile, *speechOutFile;
	SKP_int32 packetSize_ms = 0, API_Fs_Hz = 0;
	SKP_int32 decSizeBytes;
	void      *psDec;
	SKP_float loss_prob;
	SKP_int32 frames, lost, quiet;
	SKP_SILK_SDK_DecControlStruct DecControl;

	quiet = 0;
	loss_prob = 0.0f;

	//�������silk�ļ�
	bitInFile = fopen(bitInFileName, "rb");
	SKP_int32 aaaa = GetLastError();
	if (bitInFile == NULL) {
		return CANNT_OPEN_INPUT_FILE;
	}

	//��������ļ����ļ�ͷ�Ƿ����Tencent����Ƶ�ļ���ʽ
	{
		char silkHeader_TX[] = { 0x02, 0x23, 0x21, 0x53, 0x49, 0x4C, 0x4B, 0x5F, 0x56, 0x33 };//tencent�ڱ�׼��silk�ļ�ͷǰ�����0x02
		SKP_int32 magicLen = sizeof(silkHeader_TX);
		char header_buf[sizeof(silkHeader_TX)];
		counter = fread(header_buf, sizeof(char), magicLen, bitInFile);
		if (0 != memcmp(silkHeader_TX, header_buf, magicLen))
		{
			return WRONG_TENCENT_HEADER;
		}
	}

	speechOutFile = fopen(speechOutFileName, "wb");
	if (speechOutFile == NULL) {
		return CANNT_OPEN_OUT_FILE;
	}

	//���ò����ʣ�Ĭ��Ϊ24000
	if (API_Fs_Hz == 0) {
		DecControl.API_sampleRate = 24000;
	}
	else {
		DecControl.API_sampleRate = API_Fs_Hz;
	}

	DecControl.framesPerPacket = 1;

	ret = SKP_Silk_SDK_Get_Decoder_Size(&decSizeBytes);
	if (ret) {
		return ret;
	}
	psDec = malloc(decSizeBytes);

	ret = SKP_Silk_SDK_InitDecoder(psDec);
	if (ret) {
		return ret;
	}

	totPackets = 0;
	payloadEnd = payload;


	for (i = 0; i < MAX_LBRR_DELAY; i++) {

		counter = fread(&nBytes, sizeof(SKP_int16), 1, bitInFile);
#ifdef _SYSTEM_IS_BIG_ENDIAN
		swap_endian(&nBytes, 1);
#endif

		counter = fread(payloadEnd, sizeof(SKP_uint8), nBytes, bitInFile);

		if ((SKP_int16)counter < nBytes) {
			break;
		}
		nBytesPerPacket[i] = nBytes;
		payloadEnd += nBytes;
		totPackets++;
	}

	while (1) {

		counter = fread(&nBytes, sizeof(SKP_int16), 1, bitInFile);
#ifdef _SYSTEM_IS_BIG_ENDIAN
		swap_endian(&nBytes, 1);
#endif
		if (nBytes < 0 || counter < 1) {
			break;
		}

		counter = fread(payloadEnd, sizeof(SKP_uint8), nBytes, bitInFile);
		if ((SKP_int16)counter < nBytes) {
			break;
		}

		rand_seed = SKP_RAND(rand_seed);
		if ((((float)((rand_seed >> 16) + (1 << 15))) / 65535.0f >= (loss_prob / 100.0f)) && (counter > 0)) {
			nBytesPerPacket[MAX_LBRR_DELAY] = nBytes;
			payloadEnd += nBytes;
		}
		else {
			nBytesPerPacket[MAX_LBRR_DELAY] = 0;
		}

		if (nBytesPerPacket[0] == 0) {

			lost = 1;
			payloadPtr = payload;

			for (i = 0; i < MAX_LBRR_DELAY; i++) {
				if (nBytesPerPacket[i + 1] > 0) {
					SKP_Silk_SDK_search_for_LBRR(payloadPtr, nBytesPerPacket[i + 1], (i + 1), FECpayload, &nBytesFEC);
					if (nBytesFEC > 0) {
						payloadToDec = FECpayload;
						nBytes = nBytesFEC;
						lost = 0;
						break;
					}
				}
				payloadPtr += nBytesPerPacket[i + 1];
			}
		}
		else {
			lost = 0;
			nBytes = nBytesPerPacket[0];
			payloadToDec = payload;
		}

		outPtr = out;
		tot_len = 0;
		if (lost == 0) {
			frames = 0;
			do {				
				ret = SKP_Silk_SDK_Decode(psDec, &DecControl, 0, payloadToDec, nBytes, outPtr, &len);
				if (ret) {
					return ret;
				}

				frames++;
				outPtr += len;
				tot_len += len;
				if (frames > MAX_INPUT_FRAMES) {
					outPtr = out;
					tot_len = 0;
					frames = 0;
				}
			} while (DecControl.moreInternalDecoderFrames);
		}
		else {
			for (i = 0; i < DecControl.framesPerPacket; i++) {
				ret = SKP_Silk_SDK_Decode(psDec, &DecControl, 1, payloadToDec, nBytes, outPtr, &len);
				if (ret) {
					return ret;
				}
				outPtr += len;
				tot_len += len;
			}
		}
		totPackets++;

		
#ifdef _SYSTEM_IS_BIG_ENDIAN   
		swap_endian(out, tot_len);
#endif
		fwrite(out, sizeof(SKP_int16), tot_len, speechOutFile);

		totBytes = 0;
		for (i = 0; i < MAX_LBRR_DELAY; i++) {
			totBytes += nBytesPerPacket[i + 1];
		}
		SKP_memmove(payload, &payload[nBytesPerPacket[0]], totBytes * sizeof(SKP_uint8));
		payloadEnd -= nBytesPerPacket[0];
		SKP_memmove(nBytesPerPacket, &nBytesPerPacket[1], MAX_LBRR_DELAY * sizeof(SKP_int16));

	}

	for (k = 0; k < MAX_LBRR_DELAY; k++) {
		if (nBytesPerPacket[0] == 0) {

			lost = 1;


			payloadPtr = payload;
			for (i = 0; i < MAX_LBRR_DELAY; i++) {
				if (nBytesPerPacket[i + 1] > 0) {
					SKP_Silk_SDK_search_for_LBRR(payloadPtr, nBytesPerPacket[i + 1], (i + 1), FECpayload, &nBytesFEC);
					if (nBytesFEC > 0) {
						payloadToDec = FECpayload;
						nBytes = nBytesFEC;
						lost = 0;
						break;
					}
				}
				payloadPtr += nBytesPerPacket[i + 1];
			}
		}
		else {
			lost = 0;
			nBytes = nBytesPerPacket[0];
			payloadToDec = payload;
		}

		
		outPtr = out;
		tot_len = 0;

		if (lost == 0) {
			frames = 0;
			do {
				
				ret = SKP_Silk_SDK_Decode(psDec, &DecControl, 0, payloadToDec, nBytes, outPtr, &len);
				if (ret) {
					return ret;					
				}

				frames++;
				outPtr += len;
				tot_len += len;
				if (frames > MAX_INPUT_FRAMES) {
					
					outPtr = out;
					tot_len = 0;
					frames = 0;
				}
				
			} while (DecControl.moreInternalDecoderFrames);
		}
		else {
			

			for (i = 0; i < DecControl.framesPerPacket; i++) {
				ret = SKP_Silk_SDK_Decode(psDec, &DecControl, 1, payloadToDec, nBytes, outPtr, &len);
				if (ret) {
					return ret;
				}
				outPtr += len;
				tot_len += len;
			}
		}
		totPackets++;

		
#ifdef _SYSTEM_IS_BIG_ENDIAN   
		swap_endian(out, tot_len);
#endif
		fwrite(out, sizeof(SKP_int16), tot_len, speechOutFile);
		
		totBytes = 0;
		for (i = 0; i < MAX_LBRR_DELAY; i++) {
			totBytes += nBytesPerPacket[i + 1];
		}

		SKP_memmove(payload, &payload[nBytesPerPacket[0]], totBytes * sizeof(SKP_uint8));
		payloadEnd -= nBytesPerPacket[0];
		SKP_memmove(nBytesPerPacket, &nBytesPerPacket[1], MAX_LBRR_DELAY * sizeof(SKP_int16));


	}


	free(psDec);

	fclose(speechOutFile);
	fclose(bitInFile);

	return 0;
}

int int2hex(DWORD &n, char *arr)
{
    arr[4] = { 0 };
	for (int i = 0; i < 4; i++) {
		arr[i] = (n >> ((i) << 3)) & 0xFF;
	}
	return 0;
}
int pcm2wav(char *saveName){
	//wav�ļ�ͷ��Ϣ
	char wav_header[] = { 0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6D, 0x74, 0x20, 0x12, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0xC0, 0x5D, 0x00, 0x00, 0x80, 0xBB, 0x00, 0x00, 0x02, 0x00, 0x10, 0x00, 0x00, 0x00, 0x64, 0x61, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00 };//fmt�еĲ�����Ĭ��Ϊ24000

	//��ȡ������ļ�����
	fstream outfile(saveName, ios::in | ios::binary | ios::out);
	if (!outfile.is_open())
	{
		return 20002;
	}
	outfile.seekg(0, ios::end);
	int length = outfile.tellg();
	outfile.seekg(0, ios::beg);
	BYTE *buf = new BYTE[length + sizeof(wav_header)];
	memset(buf, 0, length + sizeof(wav_header));
	outfile.read((char *)buf + sizeof(wav_header), length);

	//��ȡ�ļ���RIFF��data��ǩ�ĳ���ֵ
	DWORD filelen, datalen;
	filelen = length + sizeof(wav_header)-8;
	datalen = length + sizeof(wav_header)-46;

	//�޸��ļ�ͷ����Ϣ��д�볤����Ϣ
	char tmp[4] = { 0 };
	SKP_int32 ret = int2hex(filelen, tmp);
	memcpy(wav_header + 4, tmp, 4);//�ļ���Сλ�ڱ�ǩRIFF֮��
    ret = int2hex(datalen, tmp);
	memcpy(wav_header + 42, tmp, 4);//�ļ���������Сλ�ڱ�ǩdata֮��
	memcpy(buf, wav_header, sizeof(wav_header));

	//���޸ĺ������д���ļ�
	outfile.seekp(ios::beg);
	outfile.write((char*)buf, length + sizeof(wav_header));

	//�ͷ���Դ
	outfile.close();
	delete[] buf;

	return 0;
}

int main(){
	char infile[FILENAME_MAX];
	cout << "��������Ҫת�����ļ�·����" << endl;
	cin >> infile;

	//��ȡ�ļ�·�������¶���ת������ļ���
	string tmp(infile);
	string filename = tmp.substr(tmp.rfind("\\"), tmp.rfind(".") - tmp.rfind("\\"));
	string folderpath = tmp.substr(0, tmp.rfind("\\") + 1);
	string savename = folderpath + filename + ".wav";
	const char *outfile = savename.c_str();
	SKP_int32 ret = decode(infile, (char *)outfile);
	ret = pcm2wav((char *)outfile);
	cout << "ת�����!!! �ļ�����·��Ϊ��" << endl << outfile << endl;
	system("pause");
	return 0;
}