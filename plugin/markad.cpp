/*
 * markad.c: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <vdr/plugin.h>

#include "markad.h"

cPluginMarkAd::cPluginMarkAd(void)
{
    // Initialize any member variables here.
    // DON'T DO ANYTHING ELSE THAT MAY HAVE SIDE EFFECTS, REQUIRE GLOBAL
    // VDR OBJECTS TO EXIST OR PRODUCE ANY OUTPUT!
    statusMonitor=NULL;
    bindir=strdup(DEF_BINDIR);
    logodir=strdup(DEF_LOGODIR);

    setup.ProcessDuring=true;
    setup.whileRecording=true;
    setup.whileReplaying=true;
    setup.GenIndex=true;
    setup.OSDMessage=false;
    setup.BackupMarks=false;
    setup.Verbose=false;
}

cPluginMarkAd::~cPluginMarkAd()
{
    // Clean up after yourself!
    if (statusMonitor) delete statusMonitor;
    if (bindir) free(bindir);
    if (logodir) free(logodir);
}

const char *cPluginMarkAd::CommandLineHelp(void)
{
    // Return a string that describes all known command line options.
    return "  -b DIR,   --bindir=DIR        use DIR as location for markad executable\n"
           "                                (default: /usr/bin)\n"
           "  -l DIR    --logocachedir=DIR  use DIR as location for markad logos\n"
           "                                (default: /var/lib/markad)\n";
}

bool cPluginMarkAd::ProcessArgs(int argc, char *argv[])
{
    // Command line argument processing
    static struct option long_options[] =
    {
        { "bindir",      required_argument, NULL, 'b'
        },
        { "logocachedir",      required_argument, NULL, 'l'},
        { NULL, 0, NULL, 0 }
    };

    int c;
    while ((c = getopt_long(argc, argv, "b:l:", long_options, NULL)) != -1)
    {
        switch (c)
        {
        case 'b':
            if ((access(optarg,R_OK | X_OK))!=-1)
            {
                if (bindir) free(bindir);
                bindir=strdup(optarg);
            }
            else
            {
                fprintf(stderr,"markad: can't access bin directory: %s\n",
                        optarg);
                return false;
            }
            break;

        case 'l':
            if ((access(optarg,R_OK))!=-1)
            {
                if (logodir) free(logodir);
                logodir=strdup(optarg);
            }
            else
            {
                fprintf(stderr,"markad: can't access logo directory: %s\n",
                        optarg);
                return false;
            }
            break;
        default:
            return false;
        }
    }
    return true;
}

bool cPluginMarkAd::Initialize(void)
{
    // Initialize any background activities the plugin shall perform.
    char *path;
    if (asprintf(&path,"%s/markad",bindir)==-1) return false;
    struct stat statbuf;
    if (stat(path,&statbuf)==-1)
    {
        esyslog("markad: cannot find %s, please install",path);
        free(path);
        return false;
    }
    free(path);

    return true;
}

bool cPluginMarkAd::Start(void)
{
    // Start any background activities the plugin shall perform.
    statusMonitor = new cStatusMarkAd(bindir,logodir,&setup);
    return true;
}

void cPluginMarkAd::Stop(void)
{
    // Stop any background activities the plugin is performing.
}

void cPluginMarkAd::Housekeeping(void)
{
    // Perform any cleanup or other regular tasks.
}

void cPluginMarkAd::MainThreadHook(void)
{
    // Perform actions in the context of the main program thread.
    // WARNING: Use with great care - see PLUGINS.html!
}

cString cPluginMarkAd::Active(void)
{
    // Return a message string if shutdown should be postponed
    if (statusMonitor->MarkAdRunning())
        return tr("markad still running");
    return NULL;
}

time_t cPluginMarkAd::WakeupTime(void)
{
    // Return custom wakeup time for shutdown script
    return 0;
}

cOsdObject *cPluginMarkAd::MainMenuAction(void)
{
    // Perform the action when selected from the main VDR menu.
    return new cMenuMarkAd(statusMonitor);
}

cMenuSetupPage *cPluginMarkAd::SetupMenu(void)
{
    // Return the setup menu
    return new cSetupMarkAd(&setup);
}

bool cPluginMarkAd::SetupParse(const char *Name, const char *Value)
{
    // Parse setup parameters and store their values.
    if (!strcasecmp(Name,"Execution")) setup.ProcessDuring=atoi(Value);
    else if (!strcasecmp(Name,"whileRecording")) setup.whileRecording=atoi(Value);
    else if (!strcasecmp(Name,"whileReplaying")) setup.whileReplaying=atoi(Value);
    else if (!strcasecmp(Name,"OSDMessage")) setup.OSDMessage=atoi(Value);
    else if (!strcasecmp(Name,"BackupMarks")) setup.BackupMarks=atoi(Value);
    else if (!strcasecmp(Name,"Verbose")) setup.Verbose=atoi(Value);
    else return false;
    return true;
}

bool cPluginMarkAd::Service(const char *UNUSED(Id), void *UNUSED(Data))
{
    // Handle custom service requests from other plugins
    return false;
}

const char **cPluginMarkAd::SVDRPHelpPages(void)
{
    // Return help text for SVDRP
    static const char *HelpPage[] =
    {
        "MARK <filename>\n"
        "     Start markad for the recording with the given filename.",
        NULL
    };
    return HelpPage;
}

bool cPluginMarkAd::ReadTitle(const char *Directory)
{
    memset(&title,0,sizeof(title));
    char *buf;
#if VDRVERSNUM > 10700
    if (asprintf(&buf,"%s/info",Directory)==-1) return false;
#else
    if (asprintf(&buf,"%s/info.vdr",Directory)==-1) return false;
#endif

    FILE *f;
    f=fopen(buf,"r");
    free(buf);
    if (!f)
    {
#if VDRVERSNUM > 10700
        if (asprintf(&buf,"%s/info.vdr",Directory)==-1) return false;
#else
        if (asprintf(&buf,"%s/info",Directory)==-1) return false;
#endif
        f=fopen(buf,"r");
        free(buf);
        if (!f) return false;
    }

    char *line=NULL;
    size_t length;
    while (getline(&line,&length,f)!=-1)
    {
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
    }
    if (line) free(line);

    fclose(f);
    return (title[0]!=0);
}

cString cPluginMarkAd::SVDRPCommand(const char *Command, const char *Option, int &ReplyCode)
{
    // Process SVDRP command
    if (!strcasecmp(Command,"MARK"))
    {
        if (Option)
        {
            char *Title=NULL;
            if (ReadTitle(Option)) Title=(char *) &title;
            if (statusMonitor->Start(Option,Title,true))
            {
                return cString::sprintf("Started markad for %s",Option);
            }
            else
            {
                ReplyCode=451;
                return cString::sprintf("Failed to start markad for %s",Option);
            }
        }
        else
        {
            ReplyCode=501;
            return cString::sprintf("Missing filename");
        }
    }
    return NULL;
}

VDRPLUGINCREATOR(cPluginMarkAd) // Don't touch this!