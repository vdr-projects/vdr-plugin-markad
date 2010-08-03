/*
 * markad-standalone.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __markad_standalone_h_
#define __markad_standalone_h_

#include "demux.h"
#include "decoder.h"
#include "video.h"
#include "audio.h"
#include "streaminfo.h"
#include "marks.h"

#define tr(s) dgettext("markad",s)

#define IGNORE_VIDEOINFO 1
#define IGNORE_AUDIOINFO 2
#define IGNORE_TIMERINFO 4

class cOSDMessage
{
private:
    const char *host;
    int port;
    char *msg;
    pthread_t tid;
    static void *send(void *osd);
    bool readreply(int fd);
public:
    int Send(const char *format, ...);
    cOSDMessage(const char *Host, int Port);
    ~cOSDMessage();
};

class cMarkAdStandalone
{
private:

    struct PAT
    {
unsigned table_id:
        8;
unsigned section_length_H:
        4;
unsigned reserved1:
        2;
unsigned zero:
        1;
unsigned section_syntax_indicator:
        1;
unsigned section_length_L:
        8;
unsigned transport_stream_id_H:
        8;
unsigned transport_stream_id_L:
        8;
unsigned current_next_indicator:
        1;
unsigned version_number:
        5;
unsigned reserved2:
        2;
unsigned section_number:
        8;
unsigned last_section_number:
        8;
unsigned program_number_H:
        8;
unsigned program_number_L:
        8;
unsigned pid_H:
        5;
unsigned reserved3:
        3;
unsigned pid_L:
        8;
    };

    struct PMT
    {
unsigned table_id:
        8;
unsigned section_length_H:
        4;
unsigned reserved1:
        2;
unsigned zero:
        1;
unsigned section_syntax_indicator:
        1;
unsigned section_length_L:
        8;
unsigned program_number_H:
        8;
unsigned program_number_L:
        8;
unsigned current_next_indicator:
        1;
unsigned version_number:
        5;
unsigned reserved2:
        2;
unsigned section_number:
        8;
unsigned last_section_number:
        8;
unsigned PCR_PID_H:
        5;
unsigned reserved3:
        3;
unsigned PCR_PID_L:
        8;
unsigned program_info_length_H:
        4;
unsigned reserved4:
        4;
unsigned program_info_length_L:
        8;
    };

#pragma pack(1)
    struct STREAMINFO
    {
unsigned stream_type:
        8;
unsigned PID_H:
        5;
unsigned reserved1:
        3;
unsigned PID_L:
        8;
unsigned ES_info_length_H:
        4;
unsigned reserved2:
        4;
unsigned ES_info_length_L:
        8;
    };
#pragma pack()

    struct ES_DESCRIPTOR
    {
unsigned Descriptor_Tag:
        8;
unsigned Descriptor_Length:
        8;
    };

    static const char frametypes[8];
    const char *directory;

    cMarkAdDemux *video_demux;
    cMarkAdDemux *ac3_demux;
    cMarkAdDemux *mp2_demux;
    cMarkAdDecoder *decoder;
    cMarkAdVideo *video;
    cMarkAdAudio *audio;
    cMarkAdStreamInfo *streaminfo;
    cOSDMessage *osd;

    MarkAdContext macontext;

    char title[80],*ptitle;

    bool CreatePidfile(const char *Directory);
    void RemovePidfile(const char *Directory);
    bool duplicate; // are we a dup?

    bool isTS;
    int MaxFiles;
    int lastiframe;
    int iframe;
    int framecnt;
    bool abort;
    bool fastexit;
    bool reprocess;
    int waittime;

    bool noticeVDR_MP2;
    bool noticeVDR_AC3;

    bool bDecodeVideo;
    bool bDecodeAudio;
    bool bIgnoreAudioInfo;
    bool bIgnoreVideoInfo;
    bool bIgnoreTimerInfo;
    bool bGenIndex;
    int tStart; // pretimer in seconds
    int iStart; // pretimer as index value
    int iStop;  // posttimer as index value

    bool setAudio51;  // set audio to 5.1 in info
    bool setAudio20;  // set audio to 2.0 in info
    bool setVideo43;  // set video to 4:3 in info
    bool setVideo169; // set video to 16:9 in info

    int chkLEFT;
    int chkRIGHT;

    void CheckIndex(const char *Directory);
    char *indexFile;
    int sleepcnt;

    void SaveFrame(int Frame);

    bool aspectChecked;
    bool marksAligned;
    bool bBackupMarks;
    clMarks marks;
    char *IndexToHMSF(int Index);
    void CheckFirstMark();
    void CheckLastMark();
    void CheckStartStop(int lastiframe);
    void CheckInfoAspectRatio();
    void CheckLogoMarks();
    void AddStartMark();
    void AddMark(MarkAdMark *Mark);
    void Reset();

    bool CheckVDRHD(const char *Directory);
    bool CheckPATPMT(const char *Directory);
    bool CheckTS(const char *Directory);
    bool LoadInfo(const char *Directory);
    bool SaveInfo(const char *Directory);
    bool RegenerateIndex();
    bool ProcessFile(const char *Directory, int Number);
public:
    void SetAbort()
    {
        abort=true;
    }
    void Process(const char *Directory);
    cMarkAdStandalone(const char *Directory, bool BackupMarks, int LogoExtraction,
                      int LogoWidth, int LogoHeight, bool DecodeVideo,
                      bool DecodeAudio, int IgnoreInfo,
                      const char *LogoDir, const char *MarkFileName, bool ASD,
                      bool noPid, bool OSD, const char *SVDRPHost, int SVDRPPort,
                      bool Before, bool GenIndex);

    ~cMarkAdStandalone();
};

#endif
