/** Mutitrack audio recorder
*   Uses two channel input and output device
*   May record from either or both inputs to any track
*   May select any tracks to mix-down to stereo output for monitoring
*   Does not perform processing other than stereo mix-down
*   Intended purpose is to record audio for processing in external DAW (e.g. Ardour)
*   May only record to one track from each input (may want to add feature to copy tracks
*
*   Routing:
*       May select which track is recording each input
*       May set level of each track on each output (allows minimal mix and pan)
*       May set level of each input on each output (allows monitoring)
*
*   Tracks are stored in single WAV-RIFF file with (16) channels, 16-bit, (44100) samples per second, little-ended
*/

#include <stdio.h>
#include <iostream>
#include <string>
#include <ncurses.h> //provides user interface
#include <stdlib.h> //provides system
#include <alsa/asoundlib.h>
#include <unistd.h> //provides control of terminal - set raw mode
#include <termios.h> //provides control of terminal - set raw mode
#include <sys/types.h> //provides lseek
#include <unistd.h> //provides lseek

using namespace std;

//Constants
static const int SAMPLERATE     = 44100; //Samples per second
static const int FRAME_SIZE     = 128; //Number of samples in each frame (128 samples at 441000 takes approx 3ms)
static const int MAX_TRACKS     = 16; //Quantity of mono tracks
//Transport control states
static const int TC_STOP        = 0;
static const int TC_PLAY        = 1;
static const int TC_PAUSE       = 2;
static const int TC_RECORD      = 4;
//Colours
static const int WHITE_RED      = 1;
static const int BLACK_GREEN    = 2;
static const int WHITE_BLUE     = 3;
static const int RED_BLACK      = 4;
static const int WHITE_MAGENTA  = 5;

//Functions
static bool OpenReplay();
static void CloseReplay();
static void ShowMenu();
static bool CreateProject(string sName);
static void HandleControl();
static bool Play();
static bool LoadProject(string sName);

/** Class representing single channel audio track **/
class Track
{
    public:
        int nMonMixA = 50; //A-leg monitor mix level (0-100)
        int nMonMixB = 50; //B-leg monitor mix level (0-100)
        bool bMute = true; //True if track is muted

        /** Get the channel A mix down value of sample for this channel
        *   @param  nValue Sample value
        *   @return <i>int16_t</i> Gain (pad) adjusted value
        */
        int16_t MixA(int16_t nValue)
        {
            if(bMute || 0 == nMonMixA)
                return 0;
            else
                return (nValue / 100) * nMonMixA;
        }

        /** Get the channel B mix down value of sample for this channel
        *   @param  nValue Sample value
        *   @return <i>int16_t</i> Gain (pad) adjusted value
        */
        int16_t MixB(int16_t nValue)
        {
            if(bMute || 0 == nMonMixB)
                return 0;
            else
                return (nValue / 100) * nMonMixB;
        }
};

/** Structure representing RIFF WAVE format chunk header (without id or size) **/
struct WaveHeader
{
    uint16_t nAudioFormat; //1=PCM
    uint16_t nNumChannels; //Number of channels in project
    uint32_t nSampleRate; //Samples per second - expect SAMPLERATE
    uint32_t nByteRate; //nSamplrate * nNumChannels * nBitsPerSample / 8
    uint16_t nBlockAlign; //nNumChannels * nBitsPerSample / 8 (bytes for one sampel of all chanels)
    uint16_t nBitsPerSample; //Expect 16
};


static int g_nChannels; //Number of channels in replay file
//!@todo Make quantity of tracks dynamic
static Track g_track[MAX_TRACKS]; //Array of track classes
static int g_nSamplerate = SAMPLERATE;
static int g_nRecA; //Which track is recording A-leg input (-1 = none)
static int g_nRecB; //Which track is recording B-leg input (-1 = none)
static int g_nSelectedTrack; //Index of selected track
static int g_nTransport; //Transport control status
static int g_nReplayFd; //File descriptor of replay file
//static int g_nRecordFd; //File descriptor of record file
static unsigned int g_nHeadPos; //Position of 'play head' in frames
static unsigned int g_nUnderruns; //Quantity of replay buffer underruns
static string g_sPath; //Path to project
static string g_sFilename; //Filename of multitrack recording
static bool g_bLoop; //True whilst main loop is running
//static snd_pcm_t* g_pPcmRecord = NULL; //Pointer to record stream
static snd_pcm_t* g_pPcmPlay; //Pointer to playback stream
static string g_sPcmPlayName = "default";
static string g_sPcmRecName = "default";
WINDOW* g_pWindowRouting; //ncurses window for routing panel
static off_t g_offStartOfData; //Offset of data in wave file

int g_nDebug;

void ShowMenu()
{
    for(int i = 0; i < g_nChannels; ++i)
    {
        if((int)i == g_nSelectedTrack)
            wattron(g_pWindowRouting, COLOR_PAIR(WHITE_BLUE));
        mvwprintw(g_pWindowRouting, i, 0, "Track %02d: ", i + 1);
        wattroff(g_pWindowRouting, COLOR_PAIR(WHITE_BLUE));
        if((int)i == g_nRecA)
        {
            wattron(g_pWindowRouting, COLOR_PAIR(WHITE_RED));
            wprintw(g_pWindowRouting, "REC-A ");
            wattroff(g_pWindowRouting, COLOR_PAIR(WHITE_RED));
        }
        else
            wprintw(g_pWindowRouting, "      ");
        if((int)i == g_nRecB)
        {
            wattron(g_pWindowRouting, COLOR_PAIR(WHITE_RED));
            wprintw(g_pWindowRouting, "REC-B ");
            wattroff(g_pWindowRouting, COLOR_PAIR(WHITE_RED));
        }
        else
            wprintw(g_pWindowRouting, "      ");
        if(g_track[i].bMute)
        {
            wattron(g_pWindowRouting, COLOR_PAIR(RED_BLACK));
            wprintw(g_pWindowRouting, "    MUTE   ", g_track[i].nMonMixA, g_track[i].nMonMixB);
            wattroff(g_pWindowRouting, COLOR_PAIR(RED_BLACK));
        }
        else
            wprintw(g_pWindowRouting, " L%3d  R%3d", g_track[i].nMonMixA, g_track[i].nMonMixB);
    }
    wrefresh(g_pWindowRouting);
    switch(g_nTransport)
    {
        case TC_STOP:
            attron(COLOR_PAIR(WHITE_MAGENTA));
            mvprintw(0, 18, "  STOP  ");
            attroff(COLOR_PAIR(WHITE_MAGENTA));
            break;
        case TC_PLAY:
            attron(COLOR_PAIR(BLACK_GREEN));
            mvprintw(0, 18, "  PLAY  ");
            attroff(COLOR_PAIR(BLACK_GREEN));
            break;
        case TC_RECORD:
            attron(COLOR_PAIR(WHITE_RED));
            mvprintw(0, 18, " RECORD ");
            attroff(COLOR_PAIR(WHITE_RED));
            break;
    }
    refresh();
}

bool CreateProject(string sName)
{
    //!@todo implement CreateProject
    //Create directory
    //Create WAVE file with header
    //Set path
    return true;
}

void HandleControl()
{
    int nInput = getch();
    switch(nInput)
    {
        case 'q':
            //Quit
            //!@todo Confirm quit
            g_bLoop = false;
            break;
        case KEY_DOWN:
            //Select next track
            if(++g_nSelectedTrack >= g_nChannels)
                g_nSelectedTrack = 0;
            break;
        case KEY_UP:
            //Select previou track
            if(0 == g_nSelectedTrack)
                g_nSelectedTrack = g_nChannels;
            --g_nSelectedTrack;
            break;
        case KEY_LEFT:
            //Decrease monitor level
            if(g_track[g_nSelectedTrack].bMute)
                g_track[g_nSelectedTrack].bMute = false;
            else
            {
                if(g_track[g_nSelectedTrack].nMonMixA > 0)
                    --g_track[g_nSelectedTrack].nMonMixA;
                if(g_track[g_nSelectedTrack].nMonMixB > 0)
                    --g_track[g_nSelectedTrack].nMonMixB;
            }
            break;
        case KEY_RIGHT:
            //Increase monitor level
            if(g_track[g_nSelectedTrack].bMute)
                g_track[g_nSelectedTrack].bMute = false;
            else
            {
                if(g_track[g_nSelectedTrack].nMonMixA < 100)
                    ++g_track[g_nSelectedTrack].nMonMixA;
                if(g_track[g_nSelectedTrack].nMonMixB < 100)
                    ++g_track[g_nSelectedTrack].nMonMixB;
            }
            break;
        case KEY_SLEFT:
            //Pan monitor left
            if(g_track[g_nSelectedTrack].bMute)
                g_track[g_nSelectedTrack].bMute = false;
            else
            {
                if(g_track[g_nSelectedTrack].nMonMixA < 100)
                    ++g_track[g_nSelectedTrack].nMonMixA;
                if(g_track[g_nSelectedTrack].nMonMixB > 0)
                    --g_track[g_nSelectedTrack].nMonMixB;
            }
            break;
        case KEY_SRIGHT:
            //Pan monitor right
            if(g_track[g_nSelectedTrack].bMute)
                g_track[g_nSelectedTrack].bMute = false;
            else
            {
                if(g_track[g_nSelectedTrack].nMonMixA > 0)
                    --g_track[g_nSelectedTrack].nMonMixA;
                if(g_track[g_nSelectedTrack].nMonMixB < 100)
                    ++g_track[g_nSelectedTrack].nMonMixB;
            }
            break;
        case 'L':
            //Pan fully left
            if(g_track[g_nSelectedTrack].bMute)
                g_track[g_nSelectedTrack].bMute = false;
            g_track[g_nSelectedTrack].nMonMixA = 100;
            g_track[g_nSelectedTrack].nMonMixB = 0;
            break;
        case 'R':
            //Pan fully right
            if(g_track[g_nSelectedTrack].bMute)
                g_track[g_nSelectedTrack].bMute = false;
            g_track[g_nSelectedTrack].nMonMixA = 0;
            g_track[g_nSelectedTrack].nMonMixB = 100;
            break;
        case 'C':
            //Pan centre
            if(g_track[g_nSelectedTrack].bMute)
                g_track[g_nSelectedTrack].bMute = false;
            g_track[g_nSelectedTrack].nMonMixA = 50;
            g_track[g_nSelectedTrack].nMonMixB = 50;
            break;
        case 'a':
            //Toggle record from A
            if(g_nRecA == g_nSelectedTrack)
                g_nRecA = -1;
            else
                g_nRecA = g_nSelectedTrack;
            break;
        case 'b':
            //Toggle record from B
            if(g_nRecB == g_nSelectedTrack)
                g_nRecB = -1;
            else
                g_nRecB = g_nSelectedTrack;
            break;
        case 'm':
            //Toggle monitor mute
            g_track[g_nSelectedTrack].bMute = !g_track[g_nSelectedTrack].bMute;
            break;
        case ' ':
            //Start / Stop
            switch(g_nTransport)
            {
                case TC_STOP:
                    //Currently stopped so need to open files and interfaces and start
                    if(OpenReplay())
                        g_nTransport = TC_PLAY;
                    break;
                case TC_PAUSE:
                    //Currently paused so need to resume playing / recording
                    g_nTransport = TC_PLAY;
                    break;
                case TC_PLAY:
                    //Currently playing so need to stop
                    CloseReplay();
                    g_nTransport = TC_STOP;
                    break;
                case TC_RECORD:
                    //Currently recording so need to stop
                    break;
            }
            break;
        case KEY_HOME:
            {
                //Go to home position
                int nState = g_nTransport;
                //!@todo Leave open and position playhead
                CloseReplay();
                switch(nState)
                {
                    OpenReplay();
                    case TC_PLAY:
                    case TC_PAUSE:
                    case TC_RECORD:
                        OpenReplay();
                        g_nTransport = nState;
                        break;
                }
                break;
            }
        case 'z':
            //Debug
            break;
        default:
            return;
    }
    ShowMenu();
}

//Open replay device
bool OpenReplay()
{
    int nError;
    g_nHeadPos = 0;
    g_nUnderruns = 0;
    if(g_nReplayFd > 0 && g_pPcmPlay)
        return false; //!@todo should we return true if device already open?

	//**Open file**
	if(g_nReplayFd < 0)
    {
        //File not open
        string sFilename = g_sPath;
        sFilename.append("audio.wav");
        g_nReplayFd = open(sFilename.c_str(), O_RDONLY | O_CREAT, 0644);
        if(g_nReplayFd <= 0)
        {
            cerr << "Unable to open replay file " << sFilename << " - error " << errno << endl;
            CloseReplay();
            return false;
        }

        //**Read RIFF headers**
        char pBuffer[12];
        if(read(g_nReplayFd, pBuffer, 12) < 12)
        {
            cerr << "Replay file not RIFF - too small" << endl;
            CloseReplay();
            return false;
        }
        if(0 != strncmp(pBuffer, "RIFF", 4))
        {
            cerr << "Not RIFF" << endl;
            CloseReplay();
            return false;
        }
        if(0 != strncmp(pBuffer + 8, "WAVE", 4))
        {
            cerr << "Not WAVE" << endl;
            CloseReplay();
            return false;
        }

        //Look for chuncks
        while(read(g_nReplayFd, pBuffer, 8) == 8) //read ckID and cksize
        {
            char sId[5] = {0,0,0,0,0}; //buffer for debug output only
            strncpy(sId, pBuffer, 4); //chunk ID is first 32-bit word
            uint32_t nSize = (uint32_t)*(pBuffer + 4); //chunk size is second 32-bit word

    //        cerr << endl << "Found chunk " << sId << " of size " << nSize << endl;
            if(0 == strncmp(pBuffer, "fmt ", 4)) //chunk ID is first 32-bit word
            {
                //Found format chunk
                char pWaveBuffer[sizeof(WaveHeader)];
                WaveHeader* pWaveHeader = (WaveHeader*)pWaveBuffer;
                if(read(g_nReplayFd, pWaveBuffer, sizeof(pWaveBuffer)) < (int)sizeof(pWaveBuffer))
                {
                    cerr << "Too small for WAVE header" << endl;
                    CloseReplay();
                    return false;
                }
                g_nChannels = pWaveHeader->nNumChannels;
                if(g_nChannels > MAX_TRACKS)
                {
                    //!@todo handle too many tracks, e.g. ask whether to delete extra tracks
                }
                g_nSamplerate = pWaveHeader->nSampleRate;
                attron(COLOR_PAIR(WHITE_MAGENTA));
                mvprintw(0, 27, " % 2d bit % 6dHz ", pWaveHeader->nBitsPerSample, pWaveHeader->nSampleRate);
                attroff(COLOR_PAIR(WHITE_MAGENTA));
                lseek(g_nReplayFd, nSize - sizeof(pWaveBuffer), SEEK_CUR); //ignore other parameters
            }
            else if(0 == strncmp(pBuffer, "data ", 4))
            {
                //Aligned with start of data
                g_offStartOfData = lseek(g_nReplayFd, 0, SEEK_CUR);
                return true;
            }
            else
                lseek(g_nReplayFd, nSize, SEEK_CUR); //Not found desired chunk so seek to next chunk
        }
        cerr << "Failed to get WAVE header";
    }

    //**Open sound device**
    if(NULL == g_pPcmPlay)
    {
        //Replay stream not open
        if((nError = snd_pcm_open(&g_pPcmPlay, g_sPcmPlayName.c_str(), SND_PCM_STREAM_PLAYBACK, 0)) < 0)
        {
            cerr << "Unable to open replay device: " << snd_strerror(nError) << endl;
            CloseReplay();
            return false;
        }
        if((nError = snd_pcm_set_params(g_pPcmPlay,
                                      SND_PCM_FORMAT_S16_LE,
                                      SND_PCM_ACCESS_RW_INTERLEAVED,
                                      2,
                                      g_nSamplerate,
                                      1,
                                      3000)) < 0)
        {
            cerr << "Unable to configure replay device: " << snd_strerror(nError) << endl;
            CloseReplay();
            return false;
        }
    }

	return false; //If we get here something must be wrong!
}

//Close replay device
void CloseReplay()
{
    if(g_pPcmPlay)
        snd_pcm_close(g_pPcmPlay);
    g_pPcmPlay = NULL;
    if(g_nReplayFd > 0)
        close(g_nReplayFd);
    g_nReplayFd = -1;
    g_nTransport = TC_STOP;
    g_nChannels = 0;
    ShowMenu();
}

//Play audio
bool Play()
{
    if(!g_pPcmPlay || (g_nReplayFd < 0) || ((TC_PLAY != g_nTransport) && (TC_RECORD != g_nTransport)))
        return false;

    //Read frame from file
    //!@todo handle different bits/sample size
    unsigned char pInBuffer[2 * g_nChannels * FRAME_SIZE]; // buffer to hold input frame: 2 bytes per sample, (16) channels
    int16_t pOutBuffer[2 * FRAME_SIZE]; // buffer to hold output frame: 2 channels (stereo output) 16-bit word
    //!@todo Should we use pcm silence function
    memset(pOutBuffer, 0, sizeof(pOutBuffer)); //silence output buffer
    int nRead = read(g_nReplayFd, pInBuffer, sizeof(pInBuffer));
    //cerr << endl << endl << "Read " << nRead << " bytes from file. Channels " << g_nChannels << " buffer size=" << sizeof(pInBuffer) << endl;
    bool bPlaying = (nRead > 0);

    //Mix each frame to output buffer
    //iterate through input buffer one frame at a time, adding gain-adjusted value to output buffer
    for(int nPos = 0; nPos < nRead ; nPos += (2 * g_nChannels))
    {
        for(int nChan = 0; nChan < g_nChannels; ++nChan)
        {
            uint16_t nSample = pInBuffer[nPos + (2 * nChan)] + (pInBuffer[nPos + (2 * nChan) + 1] << 8); //get little endian sample into 16-bit word
            pOutBuffer[nPos / g_nChannels] += g_track[nChan].MixA(nSample);
            pOutBuffer[nPos / g_nChannels + 1] += g_track[nChan].MixB(nSample);
        }
        ++g_nHeadPos; //Move one sample forward
    }
    snd_pcm_sframes_t nFrames;
    //Send output buffer to soundcard replay output
    if(bPlaying)
    {
        nFrames = snd_pcm_writei(g_pPcmPlay, pOutBuffer, FRAME_SIZE);
        switch(nFrames)
        {
            case -EBADFD:
                attron(COLOR_PAIR(WHITE_RED));
                mvprintw(18, 31, "File descriptor in bad state");
                attroff(COLOR_PAIR(WHITE_RED));
                snd_pcm_recover(g_pPcmPlay, nFrames, 1); //Attempt to recover from error
                break;
            case -EPIPE:
                //Broken Pipe == Underrun
                attron(COLOR_PAIR(WHITE_RED));
                mvprintw(18, 0, "Underruns:% 4d", ++g_nUnderruns);
                attroff(COLOR_PAIR(WHITE_RED));
                snd_pcm_recover(g_pPcmPlay, nFrames, 1); //Attempt to recover from error
                break;
            case -ESTRPIPE:
                attron(COLOR_PAIR(WHITE_RED));
                mvprintw(18, 12, "Streams pipe error");
                attroff(COLOR_PAIR(WHITE_RED));
                snd_pcm_recover(g_pPcmPlay, nFrames, 1); //Attempt to recover from error
                break;
            default:
                mvprintw(17, 0, "Played frames: %d", nFrames);
        }
    }
    attron(COLOR_PAIR(WHITE_MAGENTA));
    unsigned int nMinutes = g_nHeadPos / g_nSamplerate / 60;
    unsigned int nSeconds = (g_nHeadPos - nMinutes * g_nSamplerate * 60) / g_nSamplerate;
    mvprintw(0, 0, " Position: %02d:%02d ", nMinutes, nSeconds);
    attroff(COLOR_PAIR(WHITE_MAGENTA));
    mvprintw(17, 0, "Played frames: %10d", nFrames);
    //Return true if more to play else false if at end of file
    return bPlaying;
}

bool LoadProject(string sName)
{
    string sConfig = g_sPath;
    sConfig.append("config");
    FILE *pFile = fopen(sConfig.c_str(),"r");
    if(pFile)
    {
        char pLine[256];
        while(fgets(pLine, sizeof(pLine) ,pFile))
        {
            if(strnlen(pLine, sizeof(pLine)) < 5)
                continue;
            int nChannel = (pLine[0] - '0') * 10 + (pLine[1] - '0');
            if(nChannel >= 0 && nChannel < g_nChannels)
            {
                switch(pLine[2])
                {
                    case 'L':
                        g_track[nChannel].nMonMixA = atoi(pLine + 4);
                        break;
                    case 'R':
                        g_track[nChannel].nMonMixB = atoi(pLine + 4);
                        break;
                    case 'M':
                        //Mute
                        g_track[nChannel].bMute = (pLine[4] == '1');
                        break;
                }
            }
        }
        fclose(pFile);
    }
}

int main()
{
    g_nDebug = 0;
    g_nTransport = TC_STOP;
    g_nHeadPos = 0;
    g_nSelectedTrack = 0;
    g_nRecA = -1; //Deselect A-channel recording
    g_nRecB = -1; //Deselect B-channel recording
    g_nReplayFd = -1;
    g_pPcmPlay = NULL;
    g_nChannels = 0;
    g_sPath = "/home/brian/multitrack/";
    initscr();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    start_color();
    init_pair(WHITE_RED, COLOR_WHITE, COLOR_RED);
    init_pair(BLACK_GREEN, COLOR_BLACK, COLOR_GREEN);
    init_pair(WHITE_BLUE, COLOR_WHITE, COLOR_BLUE);
    init_pair(RED_BLACK, COLOR_RED, COLOR_BLACK);
    init_pair(WHITE_MAGENTA, COLOR_WHITE, COLOR_MAGENTA);
    attron(COLOR_PAIR(WHITE_MAGENTA));
    mvprintw(0,0, "                                             ");
    attroff(COLOR_PAIR(WHITE_MAGENTA));

    LoadProject("default");

    g_pWindowRouting = newwin(MAX_TRACKS, 40, 1, 0);
    refresh();
    ShowMenu();

    //Set stdin to non-blocking
    termios flags;
    if(tcgetattr(fileno(stdin), &flags) < 0)
    {
        /* handle error */
        cerr << "Failed to get terminal attributes" << endl;
    }
    flags.c_lflag &= ~ICANON; // set raw (unset canonical modes)
    flags.c_cc[VMIN] = 0; // i.e. min 1 char for blocking, 0 chars for non-blocking
    flags.c_cc[VTIME] = 0; // block if waiting for char
    if(tcsetattr(fileno(stdin), TCSANOW, &flags) < 0)
    {
        /* handle error */
        cerr << "Failed to set terminal attributes" << endl;
    }


    g_bLoop = true;

//    if(!CreateProject("default"))
//    {
//        cerr << "Failed to create default project - check path" << endl;
//        return -1;
//    }

    while(g_bLoop)
    {
        HandleControl();
        if(!Play() && TC_PLAY == g_nTransport)
            CloseReplay();
//        Play();
        //Read frame from inputs
        //Read frame from each track
        //Write frame to record-primed tracks
        //Mix each sample to output frame buffers
        //Write output frame buffers to outputs
    }
    CloseReplay();
    endwin();
    return 0;
}
