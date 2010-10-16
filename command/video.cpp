/*
 * video.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <time.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern "C"
{
#include "debug.h"
}

#include "video.h"

cMarkAdLogo::cMarkAdLogo(MarkAdContext *maContext)
{
    macontext=maContext;

    // 3x3 GX Sobel mask

    GX[0][0] = -1;
    GX[0][1] =  0;
    GX[0][2] =  1;
    GX[1][0] = -2;
    GX[1][1] =  0;
    GX[1][2] =  2;
    GX[2][0] = -1;
    GX[2][1] =  0;
    GX[2][2] =  1;

    // 3x3 GY Sobel mask
    GY[0][0] =  1;
    GY[0][1] =  2;
    GY[0][2] =  1;
    GY[1][0] =  0;
    GY[1][1] =  0;
    GY[1][2] =  0;
    GY[2][0] = -1;
    GY[2][1] = -2;
    GY[2][2] = -1;

    LOGOHEIGHT=LOGO_DEFHEIGHT;
    if (maContext->Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264)
    {
        LOGOWIDTH=LOGO_DEFHDWIDTH;
    }
    else
    {
        LOGOWIDTH=LOGO_DEFWIDTH;
    }

    pixfmt_info=false;
    Clear();
}

void cMarkAdLogo::Clear()
{
    memset(&area,0,sizeof(area));
    area.status=UNINITIALIZED;
}

int cMarkAdLogo::Load(const char *directory, char *file, int plane)
{
    if ((plane<0) || (plane>3)) return -3;

    char *path;
    if (asprintf(&path,"%s/%s-P%i.pgm",directory,file,plane)==-1) return -3;

    // Load mask
    FILE *pFile;
    area.valid[plane]=false;
    pFile=fopen(path, "rb");
    free(path);
    if (!pFile)
    {
        if (plane>0) return 0; // only report for plane0
        return -1;
    }

    int width,height;
    if (fscanf(pFile, "P5\n#C%i %i\n%d %d\n255\n#", &area.corner,&area.mpixel[plane],&width,&height)!=4)
    {
        fclose(pFile);
        return -2;
    }

    if (height==255)
    {
        height=width;
        width=area.mpixel[plane];
        area.mpixel[plane]=0;
    }

    if ((width<=0) || (height<=0) || (width>LOGO_MAXWIDTH) || (height>LOGO_MAXHEIGHT) ||
            (area.corner<TOP_LEFT) || (area.corner>BOTTOM_RIGHT))
    {
        fclose(pFile);
        return -2;
    }

    if (fread(&area.mask[plane],1,width*height,pFile)!=(size_t) (width*height)) return -2;

    if (!area.mpixel[plane])
    {
        for (int i=0; i<width*height; i++)
        {
            if (!area.mask[plane][i]) area.mpixel[plane]++;
        }
    }

    if (!plane)
    {
        // plane 0 is the largest -> use this values
        LOGOWIDTH=width;
        LOGOHEIGHT=height;
    }

    fclose(pFile);
    area.valid[plane]=true;
    return 0;
}



void cMarkAdLogo::Save(int framenumber, uchar picture[4][MAXPIXEL], int plane)
{
    if (!macontext) return;
    if ((plane<0) || (plane>3)) return;
    if (!macontext->Info.ChannelName) return;
    if (!macontext->Video.Info.Width) return;
    if (!macontext->Video.Info.Height) return;
    if (!macontext->Video.Data.Valid) return;
    if (!macontext->Video.Data.PlaneLinesize[plane]) return;

    char *buf=NULL;
    if (asprintf(&buf,"%s/%06d-%s-A%i_%i-P%i.pgm","/tmp/",framenumber,
                 macontext->Info.ChannelName,
                 area.aspectratio.Num,area.aspectratio.Den,plane)==-1) return;

    // Open file
    FILE *pFile=fopen(buf, "wb");
    if (pFile==NULL)
    {
        free(buf);
        return;
    }

    int width=LOGOWIDTH;
    int height=LOGOHEIGHT;

    if (plane>0)
    {
        width/=2;
        height/=2;
    }

    // Write header
    fprintf(pFile, "P5\n#C%i\n%d %d\n255\n", area.corner,width,height);

    // Write pixel data
    if (fwrite(picture[plane],1,width*height,pFile)) {};
    // Close file
    fclose(pFile);
    free(buf);
}

int cMarkAdLogo::SobelPlane(int plane)
{
    if ((plane<0) || (plane>3)) return 0;
    if (!macontext->Video.Data.PlaneLinesize[plane]) return 0;

    int xstart,xend,ystart,yend;

    switch (area.corner)
    {
    case TOP_LEFT:
        xstart=0;
        xend=LOGOWIDTH;
        ystart=0;
        yend=LOGOHEIGHT;
        break;
    case TOP_RIGHT:
        xstart=macontext->Video.Info.Width-LOGOWIDTH;
        xend=macontext->Video.Info.Width;
        ystart=0;
        yend=LOGOHEIGHT;
        break;
    case BOTTOM_LEFT:
        xstart=0;
        xend=LOGOWIDTH;
        ystart=macontext->Video.Info.Height-LOGOHEIGHT;
        yend=macontext->Video.Info.Height;
        break;
    case BOTTOM_RIGHT:
        xstart=macontext->Video.Info.Width-LOGOWIDTH;
        xend=macontext->Video.Info.Width;
        ystart=macontext->Video.Info.Height-LOGOHEIGHT;
        yend=macontext->Video.Info.Height;
        break;
    default:
        return 0;
    }

    if (macontext->Video.Info.Pix_Fmt!=0)
    {
        if (!pixfmt_info)
        {
            esyslog("unknown pix_fmt %i, please report!",macontext->Video.Info.Pix_Fmt);
            pixfmt_info=true;
        }
        return 0;
    }

    int boundary=15;
    int cutval=127;
    int width=LOGOWIDTH;

    if (plane>0)
    {
        xstart/=2;
        xend/=2;
        ystart/=2;
        yend/=2;
        boundary/=2;
        cutval/=2;
        width/=2;
    }

    int SUM;
    int sumX,sumY;
    area.rpixel[plane]=0;
    if (!plane) area.intensity=0;
    for (int Y=ystart; Y<=yend-1; Y++)
    {
        for (int X=xstart; X<=xend-1; X++)
        {
            if (!plane)
            {
                area.intensity+=macontext->Video.Data.Plane[plane][X+(Y*macontext->Video.Data.PlaneLinesize[plane])];
            }
            sumX=0;
            sumY=0;

            // image boundaries
            if (Y<(ystart+boundary) || Y>(yend-boundary))
                SUM=0;
            else if (X<(xstart+boundary) || X>(xend-boundary))
                SUM=0;
            // convolution starts here
            else
            {
                // X Gradient approximation
                for (int I=-1; I<=1; I++)
                {
                    for (int J=-1; J<=1; J++)
                    {
                        sumX=sumX+ (int) ((*(macontext->Video.Data.Plane[plane]+X+I+
                                             (Y+J)*macontext->Video.Data.PlaneLinesize[plane]))
                                          *GX[I+1][J+1]);
                    }
                }

                // Y Gradient approximation
                for (int I=-1; I<=1; I++)
                {
                    for (int J=-1; J<=1; J++)
                    {
                        sumY=sumY+ (int) ((*(macontext->Video.Data.Plane[plane]+X+I+
                                             (Y+J)*macontext->Video.Data.PlaneLinesize[plane]))*
                                          GY[I+1][J+1]);
                    }
                }

                // Gradient Magnitude approximation
                SUM = abs(sumX) + abs(sumY);
            }

            if (SUM>=cutval) SUM=255;
            if (SUM<cutval) SUM=0;

            int val = 255-(uchar) SUM;

            area.sobel[plane][(X-xstart)+(Y-ystart)*width]=val;

            area.result[plane][(X-xstart)+(Y-ystart)*width]=
                (area.mask[plane][(X-xstart)+(Y-ystart)*width] + val) & 255;

            if (!area.result[plane][(X-xstart)+(Y-ystart)*width]) area.rpixel[plane]++;
#ifdef VDRDEBUG
            val=macontext->Video.Data.Plane[plane][X+(Y*macontext->Video.Data.PlaneLinesize[plane])];
            area.source[plane][(X-xstart)+(Y-ystart)*width]=val;
#endif

        }
    }
    if (!plane) area.intensity/=(LOGOHEIGHT*width);

    return 1;
}

int cMarkAdLogo::Detect(int framenumber, int *logoframenumber)
{
    bool extract=(macontext->Config->logoExtraction!=-1);

    int rpixel=0,mpixel=0;
    int processed=0;
    for (int plane=0; plane<4; plane++)
    {
        if ((area.valid[plane]) || (extract))
        {
            if (SobelPlane(plane)) processed++;
        }
        if (extract)
        {
            Save(framenumber,area.sobel,plane);
        }
        else
        {
            rpixel+=area.rpixel[plane];
            mpixel+=area.mpixel[plane];
        }
    }
    if (extract) return NOCHANGE;
    if (!processed) return ERROR;

    if (processed==1)
    {
        if ((area.intensity>100) || (area.status!=LOGO) &&
                (area.intensity>180))  return NOCHANGE;
    }

    int ret=NOCHANGE;
    if (area.status==UNINITIALIZED)
    {
        // Initialize
        if (rpixel>(mpixel*LOGO_VMARK))
        {
            area.status=LOGO;
        }
        else
        {
            area.status=NOLOGO;
        }
    }

    if (rpixel>=(mpixel*LOGO_VMARK))
    {
        if (area.status==NOLOGO)
        {
            if (area.counter>=LOGO_VMAXCOUNT)
            {
                area.status=ret=LOGO;
                *logoframenumber=area.framenumber;
                area.counter=0;
            }
            else
            {
                if (!area.counter) area.framenumber=framenumber;
                area.counter++;
            }
        }
        else
        {
            area.framenumber=framenumber;
            area.counter=0;
        }
    }

    if (rpixel<(mpixel*LOGO_IMARK))
    {
        if (area.status==LOGO)
        {
            if (area.counter>=LOGO_IMAXCOUNT)
            {
                area.status=ret=NOLOGO;
                *logoframenumber=area.framenumber;
                area.counter=0;
            }
            else
            {
                area.counter++;
            }
        }
        else
        {
            area.counter=0;
        }
    }

    if ((rpixel<(mpixel*LOGO_VMARK)) && (rpixel>(mpixel*LOGO_IMARK)))
    {
        area.counter=0;
    }
    return ret;
}

int cMarkAdLogo::Process(int FrameNumber, int *LogoFrameNumber)
{
    if (!macontext) return ERROR;
    if (!macontext->Video.Data.Valid) return ERROR;
    if (!macontext->Video.Info.Width) return ERROR;
    if (!macontext->Video.Info.Height) return ERROR;
    if (!macontext->Config->logoDirectory[0]) return ERROR;
    if (!macontext->Info.ChannelName) return ERROR;

    if (macontext->Config->logoExtraction==-1)
    {
        if ((area.aspectratio.Num!=macontext->Video.Info.AspectRatio.Num) ||
                (area.aspectratio.Den!=macontext->Video.Info.AspectRatio.Den))
        {
            char *buf=NULL;
            if (asprintf(&buf,"%s-A%i_%i",macontext->Info.ChannelName,
                         macontext->Video.Info.AspectRatio.Num,macontext->Video.Info.AspectRatio.Den)!=-1)
            {
                area.corner=-1;
                for (int plane=0; plane<4; plane++)
                {
                    int ret=Load(macontext->Config->logoDirectory,buf,plane);
                    switch (ret)
                    {
                    case -1:
                        isyslog("no logo for %s",buf);
                        break;
                    case -2:
                        esyslog("format error in %s",buf);
                        break;
                    case -3:
                        esyslog("cannot load %s",buf);
                        break;
                    }
                }
                free(buf);
            }
            area.aspectratio.Num=macontext->Video.Info.AspectRatio.Num;
            area.aspectratio.Den=macontext->Video.Info.AspectRatio.Den;
        }
    }
    else
    {
        area.aspectratio.Num=macontext->Video.Info.AspectRatio.Num;
        area.aspectratio.Den=macontext->Video.Info.AspectRatio.Den;
        area.corner=macontext->Config->logoExtraction;
        if (macontext->Config->logoWidth!=-1)
        {
            LOGOWIDTH=macontext->Config->logoWidth;
        }
        if (macontext->Config->logoHeight!=-1)
        {
            LOGOHEIGHT=macontext->Config->logoHeight;
        }
    }
    return Detect(FrameNumber,LogoFrameNumber);
}

cMarkAdBlackBordersHoriz::cMarkAdBlackBordersHoriz(MarkAdContext *maContext)
{
    macontext=maContext;

    Clear();
}

void cMarkAdBlackBordersHoriz::Clear()
{
    borderstatus=UNINITIALIZED;
    borderframenumber=-1;
}

int cMarkAdBlackBordersHoriz::Process(int FrameNumber, int *BorderIFrame)
{
#define CHECKHEIGHT 20
#define BRIGHTNESS 20
#define OFFSET 5
    if (!macontext) return 0;
    if (!macontext->Video.Data.Valid) return 0;
    if (macontext->Video.Info.FramesPerSecond==0) return 0;
    // Assumption: If we have 4:3, we should have aspectratio-changes!
    if (macontext->Video.Info.AspectRatio.Num==4) return 0;
    *BorderIFrame=0;

    int height=macontext->Video.Info.Height-OFFSET;

    int start=(height-CHECKHEIGHT)*macontext->Video.Data.PlaneLinesize[0];
    int end=height*macontext->Video.Data.PlaneLinesize[0];
    bool ftop=true,fbottom=true;
    int val=0,cnt=0,xz=0;

    for (int x=start; x<end; x++)
    {
        if (xz<macontext->Video.Info.Width)
        {
            val+=macontext->Video.Data.Plane[0][x];
            cnt++;
        }
        xz++;
        if (xz>=macontext->Video.Data.PlaneLinesize[0]) xz=0;
    }
    val/=cnt;
    if (val>BRIGHTNESS) fbottom=false;

    if (fbottom)
    {
        start=OFFSET*macontext->Video.Data.PlaneLinesize[0];
        end=macontext->Video.Data.PlaneLinesize[0]*(CHECKHEIGHT+OFFSET);
        val=0;
        cnt=0;
        xz=0;
        for (int x=start; x<end; x++)
        {
            if (xz<macontext->Video.Info.Width)
            {
                val+=macontext->Video.Data.Plane[0][x];
                cnt++;
            }
            xz++;
            if (xz>=macontext->Video.Data.PlaneLinesize[0]) xz=0;
        }
        val/=cnt;
        if (val>BRIGHTNESS) ftop=false;
    }

    if ((fbottom) && (ftop))
    {
        if (borderframenumber==-1)
        {
            borderframenumber=FrameNumber;
        }
        else
        {
#define MINSECS 240
            switch (borderstatus)
            {
            case UNINITIALIZED:
                if (FrameNumber>(borderframenumber+macontext->Video.Info.FramesPerSecond*MINSECS))
                {
                    borderstatus=BORDER;
                }
                break;

            case NOBORDER:
                if (FrameNumber>(borderframenumber+macontext->Video.Info.FramesPerSecond*MINSECS))
                {
                    *BorderIFrame=borderframenumber;
                    borderstatus=BORDER;
                    return 1; // detected start of black border
                }
                break;

            case BORDER:
                borderframenumber=FrameNumber;
                break;
            }
        }
    }
    else
    {
        if (borderframenumber!=-1)
        {
            if (borderstatus==BORDER)
            {
                *BorderIFrame=borderframenumber;
                borderstatus=NOBORDER;
                borderframenumber=-1;
                return -1; // detected stop of black border
            }
            else
            {
                borderframenumber=-1;
            }
        }
        else
        {
            borderframenumber=-1;
            borderstatus=NOBORDER;
        }
    }
    return 0;
}

cMarkAdOverlap::cMarkAdOverlap(MarkAdContext *maContext)
{
    macontext=maContext;

    histbuf[BEFORE]=NULL;
    histbuf[AFTER]=NULL;
    result.CommentBefore=NULL;
    result.CommentAfter=NULL;
    Clear();
}

cMarkAdOverlap::~cMarkAdOverlap()
{
    Clear();
}

void cMarkAdOverlap::Clear()
{
    histcnt[BEFORE]=0;
    histcnt[AFTER]=0;
    histframes[BEFORE]=0;
    histframes[AFTER]=0;
    if (histbuf[BEFORE])
    {
        delete[] histbuf[BEFORE];
        histbuf[BEFORE]=NULL;
    }
    if (histbuf[AFTER])
    {
        delete[] histbuf[AFTER];
        histbuf[AFTER]=NULL;
    }
    if (result.CommentBefore) free(result.CommentBefore);
    if (result.CommentAfter) free(result.CommentAfter);
    memset(&result,0,sizeof(result));
    similarCutOff=0;
    similarMaxCnt=0;

    lastframenumber=-1;
}

void cMarkAdOverlap::getHistogram(simpleHistogram &dest)
{
    memset(dest,0,sizeof(simpleHistogram));
    for (int Y=0; Y<macontext->Video.Info.Height;Y++)
    {
        for (int X=0; X<macontext->Video.Info.Width;X++)
        {
            uchar val=macontext->Video.Data.Plane[0][X+(Y*macontext->Video.Data.PlaneLinesize[0])];
            dest[val]++;
        }
    }
}

bool cMarkAdOverlap::areSimilar(simpleHistogram &hist1, simpleHistogram &hist2)
{
    int similar=0;
    for (int i=0; i<256; i++)
    {
        similar+=abs(hist1[i]-hist2[i]);
    }
    if (similar<similarCutOff) return true;
    return false;
}

MarkAdPos *cMarkAdOverlap::Detect()
{
    int start=0,simcnt=0;
    int tmpA=0,tmpB=0;
    if (result.FrameNumberBefore==-1) return NULL;
    result.FrameNumberBefore=-1;
    for (int B=0; B<histcnt[BEFORE]; B++)
    {
        for (int A=start; A<histcnt[AFTER]; A++)
        {
            bool simil=areSimilar(histbuf[BEFORE][B].histogram,histbuf[AFTER][A].histogram);
            if (simil)
            {
                tmpA=A;
                tmpB=B;
                start=A+1;
                simcnt++;
                break;
            }
            else
            {
                //if (simcnt) printf("%i %i %i\n",simcnt,histbuf[BEFORE][B].framenumber,histbuf[AFTER][A].framenumber);

                if (simcnt>similarMaxCnt)
                {
                    result.FrameNumberBefore=histbuf[BEFORE][tmpB].framenumber;
                    result.FrameNumberAfter=histbuf[AFTER][tmpA].framenumber;
                }
                else
                {
                    start=0;
                }
                simcnt=0;
            }
        }
    }
    if (result.FrameNumberBefore==-1)
    {
        if (simcnt>similarMaxCnt)
        {
            result.FrameNumberBefore=histbuf[BEFORE][tmpB].framenumber;
            result.FrameNumberAfter=histbuf[AFTER][tmpA].framenumber;
        }
        else
        {
            return NULL;
        }
    }

    if (asprintf(&result.CommentBefore,"detected overlap before (%i)",result.FrameNumberBefore)==-1)
    {
        result.CommentBefore=NULL;
    }
    if (asprintf(&result.CommentAfter,"detected overlap after (%i)",result.FrameNumberAfter)==-1)
    {
        result.CommentAfter=NULL;
    }
    return &result;
}

MarkAdPos *cMarkAdOverlap::Process(int FrameNumber, int Frames, bool BeforeAd)
{
    if ((lastframenumber>0) && (!similarMaxCnt))
    {
        similarCutOff=50000;
        similarMaxCnt=4;
    }

    if (BeforeAd)
    {
        if ((histframes[BEFORE]) && (histcnt[BEFORE]>=histframes[BEFORE]))
        {
            if (result.FrameNumberBefore)
            {
                Clear();
            }
            else
            {
                return NULL;
            }
        }
        if (!histbuf[BEFORE])
        {
            histframes[BEFORE]=Frames;
            histbuf[BEFORE]=new histbuffer[Frames+1];
        }
        getHistogram(histbuf[BEFORE][histcnt[BEFORE]].histogram);
        histbuf[BEFORE][histcnt[BEFORE]].framenumber=FrameNumber;
        histcnt[BEFORE]++;
    }
    else
    {
        if (!histbuf[AFTER])
        {
            histframes[AFTER]=Frames;
            histbuf[AFTER]=new histbuffer[Frames+1];
        }

        if (histcnt[AFTER]>=histframes[AFTER]-1)
        {
            if (result.FrameNumberBefore) return NULL;
            return Detect();
        }
        getHistogram(histbuf[AFTER][histcnt[AFTER]].histogram);
        histbuf[AFTER][histcnt[AFTER]].framenumber=FrameNumber;
        histcnt[AFTER]++;
    }
    lastframenumber=FrameNumber;
    return NULL;
}

cMarkAdVideo::cMarkAdVideo(MarkAdContext *maContext)
{
    macontext=maContext;

    mark.Comment=NULL;
    mark.Position=0;
    mark.Type=0;

    hborder=new cMarkAdBlackBordersHoriz(maContext);
    logo = new cMarkAdLogo(maContext);
    overlap = NULL;
    Clear();
}

cMarkAdVideo::~cMarkAdVideo()
{
    ResetMark();
    if (hborder) delete hborder;
    if (logo) delete logo;
    if (overlap) delete overlap;
}

void cMarkAdVideo::Clear()
{
    aspectratio.Num=0;
    aspectratio.Den=0;
    if (hborder) hborder->Clear();
    if (logo) logo->Clear();
}

void cMarkAdVideo::ResetMark()
{
    if (mark.Comment) free(mark.Comment);
    mark.Comment=NULL;
    mark.Position=0;
    mark.Type=0;
}

bool cMarkAdVideo::AddMark(int Type, int Position, const char *Comment)
{
    if (!Comment) return false;
    if (mark.Comment)
    {
        int oldlen=strlen(mark.Comment);
        mark.Comment=(char *) realloc(mark.Comment,oldlen+10+strlen(Comment));
        if (!mark.Comment)
        {
            mark.Position=0;
            return false;
        }
        strcat(mark.Comment," [");
        strcat(mark.Comment,Comment);
        strcat(mark.Comment,"]");
    }
    else
    {
        mark.Comment=strdup(Comment);
    }
    mark.Position=Position;
    mark.Type=Type;
    return true;
}

bool cMarkAdVideo::AspectRatioChange(MarkAdAspectRatio *a, MarkAdAspectRatio *b)
{
    if ((!a) || (!b)) return false;

    if (a->Num==0 || a->Den==0 || b->Num==0 || b->Den==0) return false;
    if ((a->Num!=b->Num) && (a->Den!=b->Den)) return true;
    return false;

}

MarkAdPos *cMarkAdVideo::Process2ndPass(int FrameNumber, int Frames, bool BeforeAd)
{
    if (!FrameNumber) return NULL;
    if (!overlap) overlap=new cMarkAdOverlap(macontext);
    if (!overlap) return NULL;

    return overlap->Process(FrameNumber, Frames, BeforeAd);
}

MarkAdMark *cMarkAdVideo::Process(int FrameNumber, int FrameNumberNext)
{
    if ((!FrameNumber) && (!FrameNumberNext)) return NULL;

    ResetMark();

    if (!macontext->Video.Options.IgnoreLogoDetection)
    {
        int logoframenumber;
        int lret=logo->Process(FrameNumber,&logoframenumber);
        if ((lret>=-1) && (lret!=0))
        {
            char *buf=NULL;
            if (lret>0)
            {
                if (asprintf(&buf,"detected logo start (%i)",logoframenumber)!=-1)
                {
                    AddMark(MT_LOGOSTART,logoframenumber,buf);
                    free(buf);
                }
            }
            else
            {
                if (asprintf(&buf,"detected logo stop (%i)",logoframenumber)!=-1)
                {
                    AddMark(MT_LOGOSTOP,logoframenumber,buf);
                    free(buf);
                }
            }
        }
    }

    int borderframenumber;
    int hret=hborder->Process(FrameNumber,&borderframenumber);

    if ((hret>0) && (borderframenumber))
    {
        char *buf=NULL;
        if (asprintf(&buf,"detected start of horiz. borders (%i [%i])",
                     borderframenumber,FrameNumber)!=-1)
        {
            AddMark(MT_BORDERSTART,borderframenumber,buf);
            free(buf);
        }
    }

    if ((hret<0) && (borderframenumber))
    {
        char *buf=NULL;
        if (asprintf(&buf,"detected stop of horiz. borders (%i [%i])",
                     borderframenumber,FrameNumber)!=-1)
        {
            AddMark(MT_BORDERSTOP,borderframenumber,buf);
            free(buf);
        }
    }

    if (!macontext->Video.Options.IgnoreAspectRatio)
    {
        if (AspectRatioChange(&macontext->Video.Info.AspectRatio,&aspectratio))
        {
            char *buf=(char *) calloc(1,256);
            snprintf(buf,255,"aspect ratio change from %i:%i to %i:%i (",
                     aspectratio.Num,aspectratio.Den,
                     macontext->Video.Info.AspectRatio.Num,
                     macontext->Video.Info.AspectRatio.Den);

            if ((macontext->Info.AspectRatio.Num) && (macontext->Info.AspectRatio.Den))
            {
                if ((macontext->Video.Info.AspectRatio.Num==macontext->Info.AspectRatio.Num) &&
                        (macontext->Video.Info.AspectRatio.Den==macontext->Info.AspectRatio.Den))
                {
                    char nbuf[20];
                    snprintf(nbuf,sizeof(nbuf),"%i)*",FrameNumberNext);
                    nbuf[19]=0;
                    strcat(buf,nbuf);
                    AddMark(MT_ASPECTSTART,FrameNumberNext,buf);
                }
                else
                {
                    char nbuf[20];
                    snprintf(nbuf,sizeof(nbuf),"%i)",framelast);
                    nbuf[19]=0;
                    strcat(buf,nbuf);
                    AddMark(MT_ASPECTSTOP,framelast,buf);
                }
            }
            else
            {
                char nbuf[20];
                snprintf(nbuf,sizeof(nbuf),"%i)?",FrameNumber);
                nbuf[19]=0;
                strcat(buf,nbuf);

                AddMark(MT_ASPECTCHANGE,FrameNumber,buf);
            }
            free(buf);
        }

        aspectratio.Num=macontext->Video.Info.AspectRatio.Num;
        aspectratio.Den=macontext->Video.Info.AspectRatio.Den;
    }
    framelast=FrameNumberNext;
    return &mark;
}
