/*
 * markad-standalone.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "markad-standalone.h"

bool SYSLOG=false;
cMarkAdStandalone *cmasta=NULL;
int SysLogLevel=2;

void syslog_with_tid(int priority, const char *format, ...)
{
    va_list ap;
    if (SYSLOG)
    {
        char fmt[255];
        snprintf(fmt, sizeof(fmt), "[%d] %s", getpid(), format);
        va_start(ap, format);
        vsyslog(priority, fmt, ap);
        va_end(ap);
    }
    else
    {
        char fmt[255];
        snprintf(fmt, sizeof(fmt), "markad: [%d] %s", getpid(), format);
        va_start(ap, format);
        vprintf(fmt,ap);
        va_end(ap);
        printf("\n");
        fflush(stdout);
    }
}

cOSDMessage::cOSDMessage(const char *Host, int Port)
{
    tid=0;
    msg=NULL;
    host=strdup(Host);
    port=Port;
}

cOSDMessage::~cOSDMessage()
{
    if (tid) pthread_join(tid,NULL);
    if (msg) free(msg);
    if (host) free((void*) host);
}

bool cOSDMessage::readreply(int fd)
{
    usleep(400000);
    char c=' ';
    do
    {
        struct pollfd fds;
        fds.fd=fd;
        fds.events=POLLIN;
        fds.revents=0;
        int ret=poll(&fds,1,600);

        if (ret<=0) return false;
        if (fds.revents!=POLLIN) return false;
        if (read(fd,&c,1)<0) return false;
    }
    while (c!='\n');
    return true;
}

void *cOSDMessage::send(void *posd)
{
    cOSDMessage *osd=(cOSDMessage *) posd;

    struct hostent *host=gethostbyname(osd->host);
    if (!host)
    {
        osd->tid=0;
        return NULL;
    }

    struct sockaddr_in name;
    name.sin_family = AF_INET;
    name.sin_port = htons(osd->port);
    memcpy(&name.sin_addr.s_addr,host->h_addr,sizeof(host->h_addr));
    uint size = sizeof(name);

    int sock;
    sock=socket(PF_INET, SOCK_STREAM, 0);
    if (sock<0) return NULL;

    if (connect(sock, (struct sockaddr *)&name,size)!=0)
    {
        close(sock);
        return NULL;
    }

    if (!osd->readreply(sock))
    {
        close(sock);
        return NULL;
    }

    write(sock,"MESG ",5);
    write(sock,osd->msg,strlen(osd->msg));
    write(sock,"\r\n",2);

    if (!osd->readreply(sock))
    {
        close(sock);
        return NULL;
    }

    write(sock,"QUIT\r\n",6);

    osd->readreply(sock);
    close(sock);
    return NULL;
}

int cOSDMessage::Send(const char *format, ...)
{
    if (tid) pthread_join(tid,NULL);
    if (msg) free(msg);
    va_list ap;
    va_start(ap, format);
    if (vasprintf(&msg,format,ap)==-1) return -1;
    va_end(ap);

    if (pthread_create(&tid,NULL,(void *(*) (void *))&send, (void *) this)!=0) return -1;
    return 0;
}

void cMarkAdStandalone::AddStartMark()
{
    char *buf;
    if (asprintf(&buf,"start of recording (0)")!=-1)
    {
        marks.Add(MT_COMMON,0,buf);
        isyslog(buf);
        free(buf);
    }

    if (tStop)
    {
        int tmp=tStart+macontext.Info.Length;
        tStop=tmp*macontext.Video.Info.FramesPerSecond;
    }
    if (tStart) tStart*=macontext.Video.Info.FramesPerSecond;
    dsyslog("tStart=%i tStop=%i",tStart,tStop);
}

bool cMarkAdStandalone::CheckFirstMark()
{
    if (marksAligned) return true;

    // Check the second mark
    clMark *second=marks.GetNext(0);
    if (!second) return false;

    if ((second->type==MT_LOGOSTART) || (second->type==MT_BORDERSTART) ||
            (second->type==MT_CHANNELSTART) || (second->type==MT_ASPECTSTART))
    {
        clMark *first=marks.Get(0);
        if (first) marks.Del(first);
        marksAligned=true;
    }

    if ((second->type==MT_LOGOSTOP) || (second->type==MT_BORDERSTOP) ||
            (second->type==MT_CHANNELSTOP) || (second->type==MT_ASPECTSTOP))
    {
        marksAligned=true;
    }

    // If we have an aspectchange, check the next aspectchange mark
    // and the difference between
    if ((second->type==MT_ASPECTCHANGE) && (macontext.Info.Length))
    {
        clMark *next=marks.GetNext(second->position,MT_ASPECTCHANGE);
        if (next)
        {
            int maxlen=macontext.Info.Length*(13*60)/(90*60); // max 13 minutes ads on 90 minutes program
            if (maxlen>(13*60)) maxlen=(13*60); // maximum ad block = 13 minutes
            int MAXPOSDIFF=(int) (macontext.Video.Info.FramesPerSecond*maxlen);
            if ((next->position-second->position)>MAXPOSDIFF)
            {
                clMark *first=marks.Get(0);
                if (first) marks.Del(first);
                marksAligned=true;
            }
        }
    }
    return marksAligned;
}

void cMarkAdStandalone::AddMark(MarkAdMark *Mark)
{
    if (!Mark) return;
    if (!Mark->Type) return;

    if (Mark->Type==MT_LOGOSTART)
    {
        // check if last mark is an aspectratio change
        clMark *prev=marks.GetLast();
        if ((prev) && ((prev->type & 0xF0)==MT_ASPECTCHANGE))
        {
            int MINMARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*5);
            if ((Mark->Position-prev->position)<MINMARKDIFF)
            {
                if (Mark->Comment) isyslog(Mark->Comment);
                isyslog("aspectratio change in short distance, using this mark (%i->%i)",Mark->Position,prev->position);
                return;
            }
        }
    }

    if (Mark->Type==MT_LOGOSTOP)
    {
        // check if last mark is an aspectratio change
        clMark *prev=marks.GetLast();
        if ((prev) && (prev->type==MT_CHANNELSTOP))
        {
            int MINMARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*15);
            if ((Mark->Position-prev->position)<MINMARKDIFF)
            {
                if (Mark->Comment) isyslog(Mark->Comment);
                isyslog("audiochannel change in short distance, using this mark (%i->%i)",Mark->Position,prev->position);
                //marks.Add(MT_MOVED,prev->position,Mark->Comment);
                return;
            }
        }
    }

    if ((Mark->Type & 0xF0)==MT_LOGOCHANGE)
    {
        // check if the distance to the last mark is large enough!
        clMark *prev=marks.GetLast();
        if ((prev) && ((prev->type & 0xF0)==MT_LOGOCHANGE))
        {
            int MINMARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*80);
            if ((Mark->Position-prev->position)<MINMARKDIFF)
            {
                if (Mark->Comment) isyslog(Mark->Comment);
                isyslog("logo distance too short, deleting (%i,%i)",prev->position,Mark->Position);
                marks.Del(prev);
                return;
            }
        }
    }

    if (macontext.Info.Length>0)
    {
        int MINPOS=(int) ((macontext.Video.Info.FramesPerSecond*macontext.Info.Length)/5);
        if (!MINPOS) MINPOS=25000; // fallback
        if (((Mark->Type & 0xF0)==MT_BORDERCHANGE) && (Mark->Position>MINPOS) &&
                (!macontext.Video.Options.IgnoreLogoDetection))
        {
            isyslog("border change detected. logo detection disabled");
            macontext.Video.Options.IgnoreLogoDetection=true;
            marks.Del(MT_LOGOSTART);
            marks.Del(MT_LOGOSTOP);
        }

        if ((((Mark->Type & 0xF0)==MT_CHANNELCHANGE) || ((Mark->Type & 0xF0)==MT_ASPECTCHANGE)) &&
                (Mark->Position>MINPOS) && (bDecodeVideo))
        {
            bool TurnOff=true;
            if ((Mark->Type & 0xF0)==MT_ASPECTCHANGE)
            {
                if ((marks.Count(MT_ASPECTCHANGE)+marks.Count(MT_ASPECTSTART)+
                        marks.Count(MT_ASPECTSTOP))    <3)
                {
                    TurnOff=false;
                }
            }

            if ((Mark->Type & 0xF0)==MT_CHANNELCHANGE)
            {
                if ((marks.Count(MT_CHANNELSTART)+marks.Count(MT_CHANNELSTOP))<3)
                {
                    TurnOff=false;
                }
            }

            if (TurnOff)
            {
                isyslog("%s change detected. logo/border detection disabled",
                        (Mark->Type & 0xF0)==MT_ASPECTCHANGE ? "aspectratio" : "audio channel");

                bDecodeVideo=false;
                macontext.Video.Data.Valid=false;
                marks.Del(MT_LOGOSTART);
                marks.Del(MT_LOGOSTOP);
                marks.Del(MT_BORDERSTART);
                marks.Del(MT_BORDERSTOP);
            }
        }
    }
    CheckFirstMark();

    if (tStart)
    {
        if (abs(Mark->Position-tStart)<200*(macontext.Video.Info.FramesPerSecond))
        {
            if ((Mark->Type==MT_ASPECTSTART) || (Mark->Type==MT_LOGOSTART) ||
                    (Mark->Type==MT_CHANNELSTART) || (Mark->Type==MT_ASPECTCHANGE))
            {
                isyslog("assuming start of broadcast (%i)",Mark->Position);
                marks.Clear(); // clear all other marks till now
                tStart=0;
            }
        }
    }

    if ((tStop) && (Mark->Position>tStop))
    {
        isyslog("assuming stop of broadcast (%i)",Mark->Position);
        fastexit=true;
        tStop=0;
    }

    clMark *old=marks.Get(Mark->Position);
    if (old)
    {
        // Aspect- / Channelchange wins over Logo/Border
        if (((old->type & 0xF0)!=MT_ASPECTCHANGE) && ((old->type & 0xF0)!=MT_CHANNELCHANGE))
        {
            if (Mark->Comment) isyslog(Mark->Comment);
            marks.Add(Mark->Type,Mark->Position,Mark->Comment);
        }
    }
    else
    {
        if (Mark->Comment) isyslog(Mark->Comment);
        marks.Add(Mark->Type,Mark->Position,Mark->Comment);
    }
}

void cMarkAdStandalone::RateMarks()
{
    if (macontext.Info.Length)
    {
        if ((marks.Count()>3) && (macontext.Info.Length>1800))
        {
            int logomarks=marks.Count(MT_LOGOSTART) + marks.Count(MT_LOGOSTOP);
            int audiomarks=marks.Count(MT_CHANNELSTART) +  marks.Count(MT_CHANNELSTOP);

            // If we have logomarks or audiomarks get rid of the aspect changes,
            // cause if we have a recording with (>=3) aspect changes the
            // logomarks were already deleted in AddMark
            if ((logomarks) || (audiomarks))
            {
                marks.Del(MT_ASPECTCHANGE);
                marks.Del(MT_ASPECTSTART);
                marks.Del(MT_ASPECTSTOP);
            }
        }
    }

    // Check the first mark again
    CheckFirstMark();

    // TODO: more checks
}

void cMarkAdStandalone::SaveFrame(int frame)
{
    if (!macontext.Video.Info.Width) return;
    if (!macontext.Video.Data.Valid) return;

    FILE *pFile;
    char szFilename[256];

    // Open file
    sprintf(szFilename, "/tmp/frame%06d.pgm", frame);
    pFile=fopen(szFilename, "wb");
    if (pFile==NULL)
        return;

    // Write header
    fprintf(pFile, "P5\n%d %d\n255\n", macontext.Video.Data.PlaneLinesize[0],
            macontext.Video.Info.Height);

    // Write pixel data
    fwrite(macontext.Video.Data.Plane[0],1,
           macontext.Video.Data.PlaneLinesize[0]*macontext.Video.Info.Height,pFile);
    // Close file
    fclose(pFile);
}

void cMarkAdStandalone::CheckIndex(const char *Directory)
{
    // Here we check if the index is more
    // advanced than our framecounter.
    // If not we wait. If we wait too much,
    // we discard this check...

#define WAITTIME 10

    if (!indexFile) return;
    if (sleepcnt>=2) return; // we already slept too much

    bool notenough=true;
    do
    {
        struct stat statbuf;
        if (stat(indexFile,&statbuf)==-1) return;

        int maxframes=statbuf.st_size/8;
        if (maxframes<(framecnt+200))
        {
            if ((difftime(time(NULL),statbuf.st_mtime))>=10) return; // "old" file
            marks.Save(Directory,macontext.Video.Info.FramesPerSecond,isTS);
            sleep(WAITTIME); // now we sleep and hopefully the index will grow
            waittime+=WAITTIME;
            if (errno==EINTR) return;
            sleepcnt++;
            if (sleepcnt>=2)
            {
                esyslog("no new data after %i seconds, skipping wait!",
                        sleepcnt*WAITTIME);
                notenough=false; // something went wrong?
            }
        }
        else
        {
            sleepcnt=0;
            notenough=false;
        }
    }
    while (notenough);
}

bool cMarkAdStandalone::ProcessFile(const char *Directory, int Number)
{
    if (!Directory) return false;
    if (!Number) return false;

    CheckIndex(Directory);
    if (abort) return false;

    const int datalen=385024;
    uchar data[datalen];

    char *fbuf;
    if (isTS)
    {
        if (asprintf(&fbuf,"%s/%05i.ts",Directory,Number)==-1) return false;
    }
    else
    {
        if (asprintf(&fbuf,"%s/%03i.vdr",Directory,Number)==-1) return false;
    }

    int f=open(fbuf,O_RDONLY);
    free(fbuf);
    if (f==-1) return false;

    int dataread;
    dsyslog("processing file %05i",Number);

    while ((dataread=read(f,data,datalen))>0)
    {
        if (abort) break;
        MarkAdMark *mark=NULL;

        if ((video_demux) && (video) && (streaminfo))
        {
            uchar *pkt;
            int pktlen;

            uchar *tspkt = data;
            int tslen = dataread;

            while (tslen>0)
            {
                int len=video_demux->Process(macontext.Info.VPid,tspkt,tslen,&pkt,&pktlen);
                if (len<0)
                {
                    break;
                }
                else
                {
                    if (pkt)
                    {
                        if (streaminfo->FindVideoInfos(&macontext,pkt,pktlen))
                        {
                            if (!framecnt)
                            {
                                isyslog("%s %i%c",(macontext.Video.Info.Height>576) ? "HDTV" : "SDTV",
                                        macontext.Video.Info.Height,
                                        macontext.Video.Info.Interlaced ? 'i' : 'p');
                                AddStartMark();
                            }
                            //printf("%05i( %c )\n",framecnt,frametypes[macontext.Video.Info.Pict_Type]);
                            framecnt++;
                            if (macontext.Video.Info.Pict_Type==MA_I_TYPE)
                            {
                                lastiframe=iframe;
                                iframe=framecnt-1;
                            }
                        }

                        bool dRes=true;
                        if ((decoder) && (bDecodeVideo)) dRes=decoder->DecodeVideo(&macontext,pkt,pktlen);
                        if (dRes)
                        {
                            if ((framecnt-iframe)<=3)
                            {
                                mark=video->Process(lastiframe);
                                AddMark(mark);
                                //SaveFrame(lastiframe);  // TODO: JUST FOR DEBUGGING!
                            }
                        }
                    }
                    tspkt+=len;
                    tslen-=len;
                }
            }
        }

        if ((ac3_demux) && (streaminfo) && (audio))
        {
            uchar *pkt;
            int pktlen;

            uchar *tspkt = data;
            int tslen = dataread;

            while (tslen>0)
            {
                int len=ac3_demux->Process(macontext.Info.DPid,tspkt,tslen,&pkt,&pktlen);
                if (len<0)
                {
                    break;
                }
                else
                {
                    if (pkt)
                    {
                        if (streaminfo->FindAC3AudioInfos(&macontext,pkt,pktlen))
                        {
                            if ((!isTS) && (!noticeVDR_AC3))
                            {
                                dsyslog("found AC3");
                                if (mp2_demux)
                                {
                                    delete mp2_demux;
                                    mp2_demux=NULL;
                                }
                                noticeVDR_AC3=true;
                            }
                            mark=audio->Process(lastiframe);
                            AddMark(mark);
                        }
                    }
                    tspkt+=len;
                    tslen-=len;
                }
            }
        }

        if ((mp2_demux) && (decoder) && (audio) && (bDecodeAudio))
        {
            uchar *pkt;
            int pktlen;

            uchar *tspkt = data;
            int tslen = dataread;

            while (tslen>0)
            {
                int len=mp2_demux->Process(macontext.Info.APid,tspkt,tslen,&pkt,&pktlen);
                if (len<0)
                {
                    break;
                }
                else
                {
                    if (pkt)
                    {
                        if (decoder->DecodeMP2(&macontext,pkt,pktlen))
                        {
                            if ((!isTS) && (!noticeVDR_MP2))
                            {
                                dsyslog("found MP2");
                                noticeVDR_MP2=true;
                            }
                            mark=audio->Process(lastiframe);
                            AddMark(mark);
                        }
                    }
                    tspkt+=len;
                    tslen-=len;
                }
            }
        }

        if (fastexit)
        {
            if (f!=-1) close(f);
            return true;
        }

        CheckIndex(Directory);
        if (abort)
        {
            if (f!=-1) close(f);
            return false;
        }
    }
    close(f);
    return true;
}

void cMarkAdStandalone::Process(const char *Directory)
{
    if (abort) return;

    struct timeval tv1,tv2;
    struct timezone tz;

    gettimeofday(&tv1,&tz);

    if (bBackupMarks) marks.Backup(Directory,isTS);

    for (int i=1; i<=MaxFiles; i++)
    {
        if (abort) break;
        if (!ProcessFile(Directory,i))
        {
            break;
        }
        if (fastexit) break;
    }

    if (!abort)
    {
        if ((lastiframe) && (!fastexit))
        {
            char *buf;
#if 0
            if ((marks.Count()<=1) && (tStart))
            {
                if (asprintf(&buf,"start of broadcast (%i)",tStart)!=-1)
                {
                }
            }
#endif

            MarkAdMark tempmark;
            tempmark.Type=MT_COMMON;
            tempmark.Position=lastiframe;

            if (asprintf(&buf,"stop of recording (%i)",lastiframe)!=-1)
            {
                tempmark.Comment=buf;
                AddMark(&tempmark);
                free(buf);
            }
        }

        gettimeofday(&tv2,&tz);
        time_t sec;
        suseconds_t usec;
        sec=tv2.tv_sec-tv1.tv_sec;
        usec=tv2.tv_usec-tv1.tv_usec;
        if (usec<0)
        {
            usec+=1000000;
            sec--;
        }
        RateMarks();
        if (marks.Save(Directory,macontext.Video.Info.FramesPerSecond,isTS))
        {
            bool bIndexError=false;
            if (marks.CheckIndex(Directory,isTS,&bIndexError))
            {
                if (bIndexError)
                {
                    if ((macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264) && (!isTS))
                    {
                        esyslog("index doesn't match marks, sorry you're lost");
                    }
                    else
                    {
                        if ((!isTS) && (bGenIndex))
                        {
                            if (RegenerateVDRIndex(Directory))
                            {
                                isyslog("recreated index");
                            }
                            else
                            {
                                esyslog("failed to recreate index");
                            }
                        }
                        else
                        {
                            esyslog("index doesn't match marks%s",
                                    isTS ? ", please recreate it" : ", please run genindex");
                        }
                    }
                }
            }
        }

        double etime,ftime=0,ptime=0;
        etime=sec+((double) usec/1000000)-waittime;
        if (etime>0) ftime=framecnt/etime;
        if (macontext.Video.Info.FramesPerSecond>0)
            ptime=ftime/macontext.Video.Info.FramesPerSecond;
        isyslog("processed time %.2fs, %i frames, %.1f fps, %.1f pps",
                etime,framecnt,ftime,ptime);

    }
}

bool cMarkAdStandalone::LoadInfo(const char *Directory)
{
    char *buf;
    if (isTS)
    {
        if (asprintf(&buf,"%s/info",Directory)==-1) return false;
    }
    else
    {
        if (asprintf(&buf,"%s/info.vdr",Directory)==-1) return false;
    }

    FILE *f;
    f=fopen(buf,"r");
    free(buf);
    if (!f) return false;

    long start=0;
    char *line=NULL;
    size_t length;
    while (getline(&line,&length,f)!=-1)
    {
        if (line[0]=='C')
        {
            int ntok=0;
            char *str=line,*tok;
            while ((tok=strtok(str," ")))
            {
                if (ntok==1) macontext.Info.ChannelID=strdup(tok);
                ntok++;
                str=NULL;
            }
            if (macontext.Info.ChannelID)
            {
                for (int i=0; i<(int) strlen(macontext.Info.ChannelID); i++)
                {
                    if (macontext.Info.ChannelID[i]=='.') macontext.Info.ChannelID[i]='_';
                }
            }
        }
        if (line[0]=='E')
        {
            int result=sscanf(line,"%*c %*i %li %i %*i %*x",&start,&macontext.Info.Length);
            if (result!=2)
            {
                start=0;
                macontext.Info.Length=0;
            }
        }
        if (line[0]=='T')
        {
            int result=sscanf(line,"%*c %79c",title);
            if ((result==0) || (result==EOF))
            {
                title[0]=0;
            }
            else
            {
                char *lf=strchr(title,10);
                if (lf) *lf=0;
                char *cr=strchr(title,13);
                if (cr) *cr=0;
            }
        }
        if ((line[0]=='@') && (start) && (macontext.Info.Length) && (!tStart) && (!tStop))
        {
            int ntok=0;
            char *str=line,*tok;
            while ((tok=strtok(str,">")))
            {
                if (strstr(tok,"</start"))
                {
                    tStart=start-atol(tok);
                }
                if (strstr(tok,"</stop"))
                {
                    tStop=atol(tok)-start-macontext.Info.Length;
                }
                ntok++;
                str=NULL;
            }
        }
        if (line[0]=='X')
        {
            int stream=0,type=0;
            char descr[256]="";
            int result=sscanf(line,"%*c %i %i %250c",&stream,&type,(char *) &descr);
            if ((result!=0) && (result!=EOF))
            {
                if ((stream==1) && (!bIgnoreVideoInfo))
                {
                    if ((type!=1) && (type!=5))
                    {
#if 0
                        // we dont have 4:3, so ignore AspectRatio-Changes
                        macontext.Video.Options.IgnoreAspectRatio=true;
                        isyslog("broadcasts aspectratio is not 4:3, disabling aspect ratio");
#endif
                        macontext.Info.AspectRatio.Num=16;
                        macontext.Info.AspectRatio.Den=9;
                    }
                    else
                    {
                        macontext.Info.AspectRatio.Num=4;
                        macontext.Info.AspectRatio.Den=3;
                    }
                }

                if ((stream==2) && (!bIgnoreAudioInfo))
                {
                    if (type==5)
                    {
                        // if we have DolbyDigital 2.0 disable AC3
                        if (strchr(descr,'2'))
                        {
                            macontext.Info.DPid.Num=0;
                            isyslog("broadcast with DolbyDigital2.0, disabling AC3 decoding");
                        }
                        // if we have DolbyDigital 5.1 disable video decoding
                        if (strchr(descr,'5'))
                        {
                            bDecodeVideo=false;
                            isyslog("broadcast with DolbyDigital5.1, disabling video decoding");
                        }

                    }
                }
            }
        }
    }
    if (line) free(line);

    fclose(f);
    if (!macontext.Info.ChannelID)
    {
        return false;
    }
    else
    {
        return true;
    }
}

bool cMarkAdStandalone::CheckTS(const char *Directory)
{
    MaxFiles=0;
    isTS=false;
    if (!Directory) return false;
    char *buf;
    if (asprintf(&buf,"%s/00001.ts",Directory)==-1) return false;
    struct stat statbuf;
    if (stat(buf,&statbuf)==-1)
    {
        if (errno!=ENOENT)
        {
            free(buf);
            return false;
        }
        free(buf);
        if (asprintf(&buf,"%s/001.vdr",Directory)==-1) return false;
        if (stat(buf,&statbuf)==-1)
        {
            free(buf);
            return false;
        }
        free(buf);
        // .VDR detected
        isTS=false;
        MaxFiles=999;
        return true;
    }
    free(buf);
    // .TS detected
    isTS=true;
    MaxFiles=65535;
    return true;
}

bool cMarkAdStandalone::CheckVDRHD(const char *Directory)
{
    char *buf;
    if (asprintf(&buf,"%s/001.vdr",Directory)==-1) return false;

    int fd=open(buf,O_RDONLY);
    free(buf);
    if (fd==-1) return false;

    uchar pes_buf[32];
    if (read(fd,pes_buf,sizeof(pes_buf))!=sizeof(pes_buf))
    {
        close(fd);
        return false;
    }
    close(fd);

    if ((pes_buf[0]==0) && (pes_buf[1]==0) && (pes_buf[2]==1) && ((pes_buf[3] & 0xF0)==0xE0))
    {
        int payloadstart=9+pes_buf[8];
        if (payloadstart>23) return false;
        uchar *start=&pes_buf[payloadstart];
        if ((start[0]==0) && (start[1]==0) && (start[2]==1) && (start[5]==0) && (start[6]==0)
                && (start[7]==0) && (start[8]==1))
        {
            return true;
        }
    }
    return false;
}

bool cMarkAdStandalone::CheckPATPMT(const char *Directory)
{
    char *buf;
    if (asprintf(&buf,"%s/00001.ts",Directory)==-1) return false;

    int fd=open(buf,O_RDONLY);
    free(buf);
    if (fd==-1) return false;

    uchar patpmt_buf[564];
    uchar *patpmt;

    if (read(fd,patpmt_buf,sizeof(patpmt_buf))!=sizeof(patpmt_buf))
    {
        close(fd);
        return false;
    }
    close(fd);
    patpmt=patpmt_buf;

    if ((patpmt[0]==0x47) && ((patpmt[1] & 0x5F)==0x40) && (patpmt[2]==0x11) &&
            ((patpmt[3] & 0x10)==0x10)) patpmt+=188; // skip SDT

    // some checks
    if ((patpmt[0]!=0x47) || (patpmt[188]!=0x47)) return false; // no TS-Sync
    if (((patpmt[1] & 0x5F)!=0x40) && (patpmt[2]!=0)) return false; // no PAT
    if ((patpmt[3] & 0x10)!=0x10) return false; // PAT not without AFC
    if ((patpmt[191] & 0x10)!=0x10) return false; // PMT not without AFC
    struct PAT *pat = (struct PAT *) &patpmt[5];

    // more checks
    if (pat->reserved1!=3) return false; // is always 11
    if (pat->reserved3!=7) return false; // is always 111

    int pid=pat->pid_L+(pat->pid_H<<8);
    int pmtpid=((patpmt[189] & 0x1f)<<8)+patpmt[190];
    if (pid!=pmtpid) return false; // pid in PAT differs from pid in PMT

    struct PMT *pmt = (struct PMT *) &patpmt[193];

    // still more checks
    if (pmt->reserved1!=3) return false; // is always 11
    if (pmt->reserved2!=3) return false; // is always 11
    if (pmt->reserved3!=7) return false; // is always 111
    if (pmt->reserved4!=15) return false; // is always 1111

    if ((pmt->program_number_H!=pat->program_number_H) ||
            (pmt->program_number_L!=pat->program_number_L)) return false;

    int desc_len=(pmt->program_info_length_H<<8)+pmt->program_info_length_L;
    if (desc_len>166) return false; // beyond patpmt buffer

    int section_end = 196+(pmt->section_length_H<<8)+pmt->section_length_L;
    section_end-=4; // we don't care about the CRC32
    if (section_end>376) return false; //beyond patpmt buffer

    int i=205+desc_len;

    while (i<section_end)
    {
        struct ES_DESCRIPTOR *es=NULL;
        struct STREAMINFO *si = (struct STREAMINFO *) &patpmt[i];
        int esinfo_len=(si->ES_info_length_H<<8)+si->ES_info_length_L;
        if (esinfo_len)
        {
            es = (struct ES_DESCRIPTOR *) &patpmt[i+sizeof(struct STREAMINFO)];
        }

        // oh no -> more checks!
        if (si->reserved1!=7) return false;
        if (si->reserved2!=15) return false;

        int pid=(si->PID_H<<8)+si->PID_L;

        switch (si->stream_type)
        {
        case 0x1:
        case 0x2:
            macontext.Info.VPid.Type=MARKAD_PIDTYPE_VIDEO_H262;
            // just use the first pid
            if (!macontext.Info.VPid.Num) macontext.Info.VPid.Num=pid;
            break;

        case 0x3:
        case 0x4:
            // just use the first pid
            if (!macontext.Info.APid.Num) macontext.Info.APid.Num=pid;
            break;

        case 0x6:
            if (es)
            {
                if (es->Descriptor_Tag==0x6A) macontext.Info.DPid.Num=pid;
            }
            break;

        case 0x1b:
            macontext.Info.VPid.Type=MARKAD_PIDTYPE_VIDEO_H264;
            // just use the first pid
            if (!macontext.Info.VPid.Num) macontext.Info.VPid.Num=pid;
            break;
        }

        i+=(sizeof(struct STREAMINFO)+esinfo_len);
    }

    return true;
}

bool cMarkAdStandalone::RegenerateVDRIndex(const char *Directory)
{
    if (!Directory) return false;
    pid_t pid;

    char *oldfile;
    if (asprintf(&oldfile,"%s/index.vdr.generated",Directory)==-1) return false;
    if (unlink(oldfile)==-1)
    {
        if (errno!=ENOENT)
        {
            free(oldfile);
            return false;
        }
    }
    free(oldfile);

    if ((pid = fork()) < 0) return false;
    if (pid > 0)   // parent process
    {
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) return false;

        if (!status)
        {
            // check stdout/stderr-file for messages
            char tmp[256];
            snprintf(tmp,sizeof(tmp),"/tmp/%i",pid);
            tmp[255]=0;
            struct stat statbuf;
            if (stat(tmp,&statbuf)!=0) return false;
            unlink(tmp);
            if (statbuf.st_size!=0) return false;
        }

        // rename index.vdr.generated -> index.vdr
        char *oldpath,*newpath;
        if (asprintf(&oldpath,"%s/index.vdr.generated",Directory)==-1) return false;

        if (asprintf(&newpath,"%s/index.vdr",Directory)==-1)
        {
            free(oldpath);
            return false;
        }

        if (rename(oldpath,newpath)!=0)
        {
            if (errno!=ENOENT)
            {
                free(oldpath);
                free(newpath);
                return false;
            }
        }
        free(oldpath);

        if (getuid()==0 || geteuid()!=0)
        {
            // if we are root, set fileowner to owner of 001.vdr/00001.ts file
            char *spath=NULL;
            if (asprintf(&spath,"%s/%s",Directory,isTS ? "00001.ts" : "001.vdr")!=-1)
            {
                struct stat statbuf;
                if (!stat(spath,&statbuf))
                {
                    chown(newpath,statbuf.st_uid, statbuf.st_gid);
                }
                free(spath);
            }
        }
        free(newpath);

        return (status==0);
    }
    else   // child process
    {
        int MaxPossibleFileDescriptors = getdtablesize();
        for (int i = STDERR_FILENO + 1; i < MaxPossibleFileDescriptors; i++)
            close(i); //close all dup'ed filedescriptors

        if (chdir(Directory)!=0) return 2;

        char *cmd;
        if (asprintf(&cmd,"genindex -q >& /tmp/%i",(int) getpid())==-1) return 2;
        int ret=execl("/bin/sh", "sh", "-c", cmd, NULL);
        free(cmd);
        if (ret) ret=2;
        _exit(ret);
    }
}

bool cMarkAdStandalone::CreatePidfile(const char *Directory)
{
    char *buf;
    if (asprintf(&buf,"%s/markad.pid",Directory)==-1) return false;

    // check for other running markad process
    FILE *oldpid=fopen(buf,"r");
    if (oldpid)
    {
        // found old pidfile, check if it's still running
        int pid;
        if (fscanf(oldpid,"%i\n",&pid)==1)
        {
            char procname[256]="";
            snprintf(procname,sizeof(procname),"/proc/%i",pid);
            struct stat statbuf;
            if (stat(procname,&statbuf)!=-1)
            {
                // found another, running markad
                isyslog("another instance is running on this recording");
                abort=duplicate=true;
            }
        }
        fclose(oldpid);
    }
    if (duplicate) return false;

    FILE *pidfile=fopen(buf,"w+");

    if (getuid()==0 || geteuid()!=0)
    {
        // if we are root, set fileowner to owner of 001.vdr/00001.ts file
        char *spath=NULL;
        if (asprintf(&spath,"%s/%s",Directory,isTS ? "00001.ts" : "001.vdr")!=-1)
        {
            struct stat statbuf;
            if (!stat(spath,&statbuf))
            {
                chown(buf,statbuf.st_uid, statbuf.st_gid);
            }
            free(spath);
        }
    }

    free(buf);
    if (!pidfile) return false;
    fprintf(pidfile,"%i\n",(int) getpid());
    fflush(pidfile);
    fclose(pidfile);
    return true;
}

void cMarkAdStandalone::RemovePidfile(const char *Directory)
{
    char *buf;
    if (asprintf(&buf,"%s/markad.pid",Directory)!=-1)
    {
        unlink(buf);
        free(buf);
    }
}

const char cMarkAdStandalone::frametypes[8]={'?','I','P','B','D','S','s','b'};

cMarkAdStandalone::cMarkAdStandalone(const char *Directory, bool BackupMarks, int LogoExtraction,
                                     int LogoWidth, int LogoHeight, bool DecodeVideo,
                                     bool DecodeAudio, bool IgnoreVideoInfo, bool IgnoreAudioInfo,
                                     const char *LogoDir, const char *MarkFileName, bool ASD,
                                     bool noPid, bool OSD, const char *SVDRPHost, int SVDRPPort,
                                     bool Before, bool GenIndex, bool Start, bool Stop)
{
    directory=Directory;
    abort=false;
    fastexit=false;

    noticeVDR_MP2=false;
    noticeVDR_AC3=false;

    sleepcnt=0;
    waittime=0;
    duplicate=false;
    marksAligned=false;
    title[0]=0;

    memset(&macontext,0,sizeof(macontext));
    macontext.LogoDir=(char *) LogoDir;
    macontext.Options.LogoExtraction=LogoExtraction;
    macontext.Options.LogoWidth=LogoWidth;
    macontext.Options.LogoHeight=LogoHeight;
    macontext.Audio.Options.AudioSilenceDetection=ASD;

    bDecodeVideo=DecodeVideo;
    bDecodeAudio=DecodeAudio;
    bIgnoreAudioInfo=IgnoreAudioInfo;
    bIgnoreVideoInfo=IgnoreVideoInfo;
    bGenIndex=GenIndex;
    bBackupMarks=BackupMarks;
    tStart=Start;
    tStop=Stop;

    macontext.Info.DPid.Type=MARKAD_PIDTYPE_AUDIO_AC3;
    macontext.Info.APid.Type=MARKAD_PIDTYPE_AUDIO_MP2;

    isyslog("starting v%s",VERSION);
    isyslog("on %s",Directory);

    if (!bDecodeAudio)
    {
        isyslog("audio decoding disabled by user");
    }
    if (!bDecodeVideo)
    {
        isyslog("video decoding disabled by user");
    }
    if (bIgnoreAudioInfo)
    {
        isyslog("audio info usage disabled by user");
    }
    if (bIgnoreVideoInfo)
    {
        isyslog("video info usage disabled by user");
    }

    if (LogoExtraction!=-1)
    {
        // just to be sure extraction works
        bDecodeVideo=true;
        bIgnoreAudioInfo=true;
        bIgnoreVideoInfo=true;
    }

    if (!noPid)
    {
        CreatePidfile(Directory);
        if (abort)
        {
            video_demux=NULL;
            ac3_demux=NULL;
            mp2_demux=NULL;
            decoder=NULL;
            video=NULL;
            audio=NULL;
            osd=NULL;
            return;
        }
    }

    if (Before) sleep(10);

    if (!CheckTS(Directory))
    {
        video_demux=NULL;
        ac3_demux=NULL;
        mp2_demux=NULL;
        decoder=NULL;
        video=NULL;
        audio=NULL;
        osd=NULL;
        return;
    }

    if (isTS)
    {
        if (!CheckPATPMT(Directory))
        {
            esyslog("no PAT/PMT found -> nothing to process");
            abort=true;
        }
        if (!macontext.Audio.Options.AudioSilenceDetection)
        {
            macontext.Info.APid.Num=0;
        }
        if (asprintf(&indexFile,"%s/index",Directory)==-1) indexFile=NULL;
    }
    else
    {
        if (macontext.Audio.Options.AudioSilenceDetection)
        {
            macontext.Info.APid.Num=-1;
        }
        macontext.Info.DPid.Num=-1;
        macontext.Info.VPid.Num=-1;

        if (CheckVDRHD(Directory))
        {
            macontext.Info.VPid.Type=MARKAD_PIDTYPE_VIDEO_H264;
        }
        else
        {
            macontext.Info.VPid.Type=MARKAD_PIDTYPE_VIDEO_H262;
        }
        if (asprintf(&indexFile,"%s/index.vdr",Directory)==-1) indexFile=NULL;
    }

    if (!LoadInfo(Directory))
    {
        if (bDecodeVideo)
        {
            esyslog("failed loading info - logo %s and pre-/post-timer disabled",
                    (LogoExtraction!=-1) ? "extraction" : "detection");
        }
        tStart=0;
        tStop=0;
    }

    if (tStart) isyslog("pre-timer  %is",tStart);
    if (tStop) isyslog("post-timer %is",tStop);

    if (title[0])
    {
        ptitle=title;
    }
    else
    {
        ptitle=(char *) Directory;
    }

    if (OSD)
    {
        osd= new cOSDMessage(SVDRPHost,SVDRPPort);
        if (osd) osd->Send("%s %s",trNOOP("starting markad for"),ptitle);
    }
    else
    {
        osd=NULL;
    }

    if (MarkFileName[0]) marks.SetFileName(MarkFileName);

    if (macontext.Info.VPid.Num)
    {
        if (isTS)
        {
            dsyslog("using %s-video (0x%04x)",
                    macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264 ? "H264": "H262",
                    macontext.Info.VPid.Num);
        }
        else
        {
            dsyslog("using %s-video",
                    macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264 ? "H264": "H262");
        }
        video_demux = new cMarkAdDemux();
    }
    else
    {
        video_demux=NULL;
    }

    if (macontext.Info.APid.Num)
    {
        if (macontext.Info.APid.Num!=-1)
            dsyslog("using MP2 (0x%04x)",macontext.Info.APid.Num);
        mp2_demux = new cMarkAdDemux();
    }
    else
    {
        mp2_demux=NULL;
    }

    if (macontext.Info.DPid.Num)
    {
        if (macontext.Info.DPid.Num!=-1)
            dsyslog("using AC3 (0x%04x)",macontext.Info.DPid.Num);
        ac3_demux = new cMarkAdDemux();
    }
    else
    {
        ac3_demux=NULL;
    }

    if (!abort)
    {
        decoder = new cMarkAdDecoder(macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264,
                                     macontext.Info.APid.Num!=0,macontext.Info.DPid.Num!=0);
        video = new cMarkAdVideo(&macontext);
        audio = new cMarkAdAudio(&macontext);
        streaminfo = new cMarkAdStreamInfo;
        if (macontext.Info.ChannelID)
            dsyslog("channel %s",macontext.Info.ChannelID);
    }
    else
    {
        decoder=NULL;
        video=NULL;
        audio=NULL;
        streaminfo=NULL;
    }

    framecnt=0;
    lastiframe=0;
    iframe=0;
}

cMarkAdStandalone::~cMarkAdStandalone()
{
    if (osd)
    {
        if (abort)
        {
            osd->Send("%s %s",trNOOP("markad aborted for"),ptitle);
        }
        else
        {
            osd->Send("%s %s",trNOOP("markad finished for"),ptitle);
        }
    }

    if (macontext.Info.ChannelID) free(macontext.Info.ChannelID);
    if (indexFile) free(indexFile);

    if (video_demux) delete video_demux;
    if (ac3_demux) delete ac3_demux;
    if (mp2_demux) delete mp2_demux;
    if (decoder) delete decoder;
    if (video) delete video;
    if (audio) delete audio;
    if (streaminfo) delete streaminfo;
    if (osd) delete osd;

    if ((directory) && (!duplicate)) RemovePidfile(directory);
}

bool isnumber(const char *s)
{
    while (*s)
    {
        if (!isdigit(*s))
            return false;
        s++;
    }
    return true;
}

int usage()
{
    // nothing done, give the user some help
    printf("Usage: markad [options] cmd <record>\n"
           "options:\n"
           "-b              --background\n"
           "                  markad runs as a background-process\n"
           "                  this will be automatically set if called with \"after\"\n"
           "-d              --disable=<option>\n"
           "                  <option>   1 = disable video  2 = disable audio\n"
           "                             3 = disable video and audio\n"
           "-i              --ignoreinfo=<info>\n"
           "                  ignores hints from info(.vdr) file\n"
           "                  <info>     1 = ignore audio info 2 = ignore video info\n"
           "                             3 = ignore video and audio info\n"
           "-l              --logocachedir\n"
           "                  directory where logos stored, default /var/lib/markad\n"
           "-p,             --priority level=<priority>\n"
           "                  priority-level of markad when running in background\n"
           "                  <-20...19> default 19\n"
           "-v,             --verbose\n"
           "                  increments loglevel by one, can be given multiple times\n"
           "-B              --backupmarks\n"
           "                  make a backup of existing marks\n"
           "-G              --genindex\n"
           "                  run genindex on broken index file\n"
           "-L              --extractlogo=<direction>[,width[,height]]\n"
           "                  extracts logo to /tmp as pgm files (must be renamed)\n"
           "                  <direction>  0 = top left,    1 = top right\n"
           "                               2 = bottom left, 3 = bottom right\n"
           "                  [width]  range from 50 to %3i, default %3i (SD)\n"
           "                                                 default %3i (HD)\n"
           "                  [height] range from 20 to %3i, default %3i\n"
           "-O,             --OSD\n"
           "                  markad sends an OSD-Message for start and end\n"
           "-V              --version\n"
           "                  print version-info and exit\n"
           "                --asd\n"
           "                  enable audio silence detecion\n"
           "                --bstart=<seconds>\n"
           "                  margin at start\n"
           "                --bstop=<seconds>\n"
           "                  margin at stop\n"
           "                --markfile=<markfilename>\n"
           "                  set a different markfile-name\n"
           "                --online[=1|2] (default is 1)\n"
           "                  start markad immediately when called with \"before\" as cmd\n"
           "                  if online is 1, markad starts online for live-recordings\n"
           "                  only, online=2 starts markad online for every recording\n"
           "                  live-recordings are identified by having a '@' in the\n"
           "                  filename so the entry 'Mark instant recording' in the menu\n"
           "                  'Setup - Recording' of the vdr should be set to 'yes'\n"
           "                --svdrphost=<ip/hostname> (default is 127.0.0.1)\n"
           "                  ip/hostname of a remote VDR for OSD messages\n"
           "                --svdrpport=<port> (default is 2001)\n"
           "                  port of a remote VDR for OSD messages\n"
           "\ncmd: one of\n"
           "-                            dummy-parameter if called directly\n"
           "after                        markad starts to analyze the recording\n"
           "before                       markad exits immediately if called with \"before\"\n"
           "edited                       markad exits immediately if called with \"edited\"\n"
           "nice                         runs markad with nice(19)\n"
           "\n<record>                     is the name of the directory where the recording\n"
           "                             is stored\n\n",
           LOGO_MAXWIDTH,LOGO_DEFWIDTH,LOGO_DEFHDWIDTH,
           LOGO_MAXHEIGHT,LOGO_DEFHEIGHT
          );
    return -1;
}

static void signal_handler(int sig)
{
    switch (sig)
    {
    case SIGTSTP:
        isyslog("paused by signal");
        kill(getpid(),SIGSTOP);
        break;
    case SIGCONT:
        isyslog("continued by signal");
        break;
    case SIGABRT:
        esyslog("aborted by signal");
        if (cmasta) cmasta->SetAbort();
        break;
    case SIGSEGV:
        esyslog("segmentation fault");
        break;
    case SIGTERM:
    case SIGINT:
        esyslog("aborted by user");
        if (cmasta) cmasta->SetAbort();
        break;
    default:
        break;
    }
}

int main(int argc, char *argv[])
{
    int c;
    bool bAfter=false,bBefore=false,bEdited=false;
    bool bFork=false,bNice=false,bImmediateCall=false;
    bool bASD=false;
    int niceLevel = 19;
    char *recDir=NULL;
    char *tok,*str;
    int ntok;
    int logoExtraction=-1;
    int logoWidth=-1;
    int logoHeight=-1;
    bool bBackupMarks=false,bNoPid=false,bOSD=false;
    char markFileName[1024]="";
    char logoDirectory[1024]="";
    char svdrphost[1024]="127.0.0.1";
    int svdrpport=2001;
    bool bDecodeVideo=true;
    bool bDecodeAudio=true;
    bool bIgnoreAudioInfo=false;
    bool bIgnoreVideoInfo=false;
    bool bGenIndex=false;
    int tstart=0;
    int tstop=0;
    int online=0;

    strcpy(logoDirectory,"/var/lib/markad");

    while (1)
    {
        int option_index = 0;
        static struct option long_options[] =
        {
            {"ac3",0,0,'a'
            },
            {"background", 0, 0, 'b'},
            {"comments", 0, 0, 'c'},
            {"disable", 1, 0, 'd'},
            {"ignoreinfo", 1, 0, 'i' },
            {"jumplogo",0,0,'j'},
            {"logocachedir", 1, 0, 'l'},
            {"nelonen",0,0,'n'},
            {"overlap",0,0,'o' },
            {"priority",1,0,'p'},
            {"statisticfile",1,0,'s'},
            {"verbose", 0, 0, 'v'},

            {"asd",0,0,6},
            {"tstart",1,0,10},
            {"tstop",1,0,11},
            {"loglevel",1,0,2},
            {"markfile",1,0,1},
            {"nopid",0,0,5},
            {"online",2,0,4},
            {"pass3only",0,0,7},
            {"svdrphost",1,0,8},
            {"svdrpport",1,0,9},
            {"testmode",0,0,3},

            {"backupmarks", 0, 0, 'B'},
            {"scenechangedetection", 0, 0, 'C'},
            {"genindex",0, 0, 'G'},
            {"extractlogo", 1, 0, 'L'},
            {"OSD",0,0,'O' },
            {"savelogo", 0, 0, 'S'},
            {"version", 0, 0, 'V'},

            {0, 0, 0, 0}
        };

        c = getopt_long  (argc, argv, "abcd:i:jl:nop:s:vBCGL:OSV",
                          long_options, &option_index);
        if (c == -1)
            break;

        switch (c)
        {

        case 'a':
            // --ac3
            break;

        case 'b':
            // --background
            bFork = SYSLOG = true;
            break;

        case 'c':
            // --comments
            break;

        case 'd':
            // --disable
            switch (atoi(optarg))
            {
            case 1:
                bDecodeVideo=false;
                break;
            case 2:
                bDecodeAudio=false;
                break;
            case 3:
                bDecodeVideo=false;
                bDecodeAudio=false;
                break;
            default:
                fprintf(stderr, "markad: invalid disable option: %s\n", optarg);
                return 2;
                break;
            }
            break;

        case 'i':
            // --ignoreinfo
            switch (atoi(optarg))
            {
            case 1:
                bIgnoreAudioInfo=true;
                break;
            case 2:
                bIgnoreVideoInfo=true;
                break;
            case 3:
                bIgnoreVideoInfo=true;
                bIgnoreAudioInfo=true;
                break;
            default:
                fprintf(stderr, "markad: invalid ignoreinfo option: %s\n", optarg);
                return 2;
                break;
            }
            break;

        case 'j':
            // --jumplogo
            break;

        case 'l':
            strncpy(logoDirectory,optarg,sizeof(logoDirectory));
            logoDirectory[sizeof(logoDirectory)-1]=0;
            break;

        case 'n':
            // --nelonen
            break;

        case 'o':
            // --overlap
            break;

        case 'p':
            // --priority
            if (isnumber(optarg) || *optarg=='-')
                niceLevel = atoi(optarg);
            else
            {
                fprintf(stderr, "markad: invalid priority level: %s\n", optarg);
                return 2;
            }
            bNice = true;
            break;

        case 's':
            // --statisticfile
            break;

        case 'v':
            // --verbose
            SysLogLevel++;
            if (SysLogLevel>10) SysLogLevel=10;
            break;

        case 'B':
            // --backupmarks
            bBackupMarks=true;
            break;

        case 'C':
            // --scenechangedetection
            break;

        case 'G':
            bGenIndex=true;
            break;

        case 'L':
            // --extractlogo
            str=optarg;
            ntok=0;
            while (tok=strtok(str,","))
            {
                switch (ntok)
                {
                case 0:
                    logoExtraction=atoi(tok);
                    if ((logoExtraction<0) || (logoExtraction>3))
                    {
                        fprintf(stderr, "markad: invalid extractlogo value: %s\n", tok);
                        return 2;
                    }
                    break;

                case 1:
                    logoWidth=atoi(tok);
                    if ((logoWidth<50) || (logoWidth>LOGO_MAXWIDTH))
                    {
                        fprintf(stderr, "markad: invalid width value: %s\n", tok);
                        return 2;
                    }
                    break;

                case 2:
                    logoHeight=atoi(tok);
                    if ((logoHeight<20) || (logoHeight>LOGO_MAXHEIGHT))
                    {
                        fprintf(stderr, "markad: invalid height value: %s\n", tok);
                        return 2;
                    }
                    break;

                default:
                    break;
                }
                str=NULL;
                ntok++;
            }
            break;

        case 'O':
            // --OSD
            bOSD=true;
            break;

        case 'S':
            // --savelogo
            break;

        case 'V':
            printf("markad %s - marks advertisements in VDR recordings\n",VERSION);
            return 0;

        case '?':
            printf("unknow option ?\n");
            break;

        case 0:
            printf ("option %s", long_options[option_index].name);
            if (optarg)
                printf (" with arg %s", optarg);
            printf ("\n");
            break;

        case 1: // --markfile
            strncpy(markFileName,optarg,sizeof(markFileName));
            markFileName[sizeof(markFileName)-1]=0;
            break;

        case 2: // --loglevel
            SysLogLevel=atoi(optarg);
            if (SysLogLevel>10) SysLogLevel=10;
            if (SysLogLevel<0) SysLogLevel=2;
            break;

        case 3: // --testmode
            break;

        case 4: // --online
            online=atoi(optarg);
            if ((online!=1) && (online!=2))
            {
                fprintf(stderr, "markad: invalid online value: %s\n", optarg);
                return 2;
            }
            break;

        case 5: // --nopid
            bNoPid=true;
            break;

        case 6: // --asd
            bASD=true;
            break;

        case 7: // --pass3only
            break;

        case 8: // --svdrphost
            strncpy(svdrphost,optarg,sizeof(svdrphost));
            svdrphost[sizeof(svdrphost)-1]=0;
            break;

        case 9: // --svdrpport
            if (isnumber(optarg) && atoi(optarg) > 0 && atoi(optarg) < 65536)
            {
                svdrpport=atoi(optarg);
            }
            else
            {
                fprintf(stderr, "markad: invalid svdrpport value: %s\n", optarg);
                return 2;
            }
            break;

        case 10: // --bstart
            if (isnumber(optarg) && atoi(optarg) > 0)
            {
                tstart=atoi(optarg);
            }
            else
            {
                fprintf(stderr, "markad: invalid bstart value: %s\n", optarg);
                return 2;
            }
            break;

        case 11: // --bstop
            if (isnumber(optarg) && atoi(optarg) > 0)
            {
                tstop=atoi(optarg);
            }
            else
            {
                fprintf(stderr, "markad: invalid bstop value: %s\n", optarg);
                return 2;
            }
            break;

        default:
            printf ("? getopt returned character code 0%o ? (option_index %d)\n", c,option_index);
        }
    }

    if (optind < argc)
    {
        while (optind < argc)
        {
            if (strcmp(argv[optind], "after" ) == 0 )
            {
                bAfter = bFork = bNice = SYSLOG = true;
            }
            else if (strcmp(argv[optind], "before" ) == 0 )
            {
                if (!online) online=1;
                bBefore = bFork = bNice = SYSLOG = true;
            }
            else if (strcmp(argv[optind], "edited" ) == 0 )
            {
                bEdited = true;
            }
            else if (strcmp(argv[optind], "nice" ) == 0 )
            {
                bNice = true;
            }
            else if (strcmp(argv[optind], "-" ) == 0 )
            {
                bImmediateCall = true;
            }
            else
            {
                if ( strstr(argv[optind],".rec") != NULL )
                    recDir = argv[optind];
            }
            optind++;
        }
    }

    // do nothing if called from vdr before/after the video is cutted
    if ((bAfter) && (online)) return 0;
    if (bBefore)
    {
        if (!online) return 0;
        if ((online==1) && (!strchr(recDir,'@'))) return 0;
    }
    if (bEdited) return 0;

    // we can run, if one of bImmediateCall, bAfter, bBefore or bNice is true
    // and recDir is given
    if ( (bImmediateCall || bBefore || bAfter || bNice) && recDir )
    {
        // if bFork is given go in background
        if ( bFork )
        {
            (void)umask((mode_t)0011);
            //close_files();
            pid_t pid = fork();
            if (pid < 0)
            {
                char *err=strerror(errno);
                fprintf(stderr, "%s\n",err);
                esyslog("fork ERROR: %s",err);
                return 2;
            }
            if (pid != 0)
            {
                tsyslog("forked to pid %d",pid);
                return 0; // initial program immediately returns
            }
        }
        if ( bFork )
        {
            tsyslog("(forked) pid %d", getpid());
            if (chdir("/")==-1)
            {
                perror("chdir");
                exit(EXIT_FAILURE);
            }
            if (setsid() == (pid_t)(-1))
            {
                perror("setsid");
                exit(EXIT_FAILURE);
            }
            if (signal(SIGHUP, SIG_IGN) == SIG_ERR)
            {
                perror("signal(SIGHUP, SIG_IGN)");
                errno = 0;
            }
            int f;

            f = open("/dev/null", O_RDONLY);
            if (f == -1)
            {
                perror("/dev/null");
                errno = 0;
            }
            else
            {
                if (dup2(f, fileno(stdin)) == -1)
                {
                    perror("dup2");
                    errno = 0;
                }
                (void)close(f);
            }

            f = open("/dev/null", O_WRONLY);
            if (f == -1)
            {
                perror("/dev/null");
                errno = 0;
            }
            else
            {
                if (dup2(f, fileno(stdout)) == -1)
                {
                    perror("dup2");
                    errno = 0;
                }
                if (dup2(f, fileno(stderr)) == -1)
                {
                    perror("dup2");
                    errno = 0;
                }
                (void)close(f);
            }
        }

        int MaxPossibleFileDescriptors = getdtablesize();
        for (int i = STDERR_FILENO + 1; i < MaxPossibleFileDescriptors; i++)
            close(i); //close all dup'ed filedescriptors

        // should we renice ?
        if ( bNice )
        {
            if (setpriority(PRIO_PROCESS,0,niceLevel)==-1)
            {
                esyslog("failed to set nice to %d",niceLevel);
            }
        }

        // now do the work...
        struct stat statbuf;
        if (stat(recDir,&statbuf)==-1)
        {
            fprintf(stderr,"%s not found\n",recDir);
            return -1;
        }

        if (!S_ISDIR(statbuf.st_mode))
        {
            fprintf(stderr,"%s is not a directory\n",recDir);
            return -1;
        }

        // ignore some signals
        signal(SIGHUP, SIG_IGN);

        // catch some signals
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
        signal(SIGSEGV, signal_handler);
        signal(SIGABRT, signal_handler);
        signal(SIGUSR1, signal_handler);
        signal(SIGTSTP, signal_handler);
        signal(SIGCONT, signal_handler);

        cmasta = new cMarkAdStandalone(recDir,bBackupMarks, logoExtraction, logoWidth, logoHeight,
                                       bDecodeVideo,bDecodeAudio,bIgnoreVideoInfo,bIgnoreAudioInfo,
                                       logoDirectory,markFileName,bASD,bNoPid,bOSD,svdrphost,
                                       svdrpport,bBefore,bGenIndex,tstart,tstop);
        if (!cmasta) return -1;

        cmasta->Process(recDir);
        delete cmasta;
        return 0;
    }

    return usage();
}