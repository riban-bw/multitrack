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

//!@todo Feature: Soft start and stop to play / record, e.g. fade in / out
//!@todo Feature: Show track length
//!@todo Required: Allow recording to extend file size
//!@todo Required: Rewrite header on save
//!@todo Required: Remove unused header chuncks
//!@todo Feature: Add / remove tracks / channels
//!@todo Bug: Hangs when opening audio device if already in use, e.g. jackd is running
//!@todo Feature: Show input (and output?) monitoring, particularly overload
//!@todo Feature: Punch in/out points
//!@todo Feature: Loop play / record
//!@todo Bug: Does not handle missing WAVE files gracefully
//!@todo Bug: Record past end of file. Stop. Press 'END'. Moves to previous end of track not new end of track
//!@todo Bug: Record past end of file does not record correct audio.

#include <string>
#include <alsa/asoundlib.h>
#include <ncurses.h> //provides user interface
#include <stdio.h>
#include <iostream>
#include <string>
#include <stdlib.h> //provides system
#include <unistd.h> //provides control of terminal - set raw mode
#include <termios.h> //provides control of terminal - set raw mode
#include <sys/types.h> //provides lseek
#include <unistd.h> //provides lseek

using namespace std;

//Constants
static const int SAMPLERATE     = 44100; //Samples per second
static const int SAMPLESIZE     = 2; //Quantity of bytes in each sample
static const int PERIOD_SIZE    = 128; //Number of frames in each period (128 samples at 441000 takes approx 3ms)
static const int MAX_TRACKS     = 16; //Quantity of mono tracks
static const int RECORD_LATENCY = 3000; //microseconds of record latency
static const int REPLAY_LATENCY = 30000; //microseconds of record latency

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

static string MIX_LEVEL[17] = {"  0dB", " -6dB", "-12dB", "-18dB", "-24dB", "-30dB", "-36dB", "-42dB", "-48dB", "-54dB", "-60dB", "-66dB", "-72dB", "-78dB", "-84dB", "-90dB", " -Inf"};

/** Class representing single channel audio track **/
class Track
{
    public:
        int nMonMixA; //A-leg monitor mix antenuation level (x 6Db) 0 - 16
        int nMonMixB; //B-leg monitor mix antenuation level (x 6Db) 0 - 16
        bool bMute; //True if track is muted
        bool bRecording; //True if recording - mute output

        /** Get the channel A mix down value of sample for this channel
        *   @param  nValue Sample value
        *   @return <i>int16_t</i> Antenuated value
        */
        int16_t MixA(int16_t nValue)
        {
            if(bMute || bRecording || 16 == nMonMixA)
                return 0;
            else
                return nValue >> nMonMixA;
        }

        /** Get the channel B mix down value of sample for this channel
        *   @param  nValue Sample value
        *   @return <i>int16_t</i> Antenuated adjusted value
        */
        int16_t MixB(int16_t nValue)
        {
            if(bMute || bRecording || 16 == nMonMixB)
                return 0;
            else
                return nValue >> nMonMixB;
        }
};

/** Write a 16-bit, little-endian word to a char buffer */
void SetLE16(char* pBuffer, uint16_t nWord)
{
    *pBuffer = char(nWord & 0xFF);
    *(pBuffer + 1) = char((nWord >> 8) & 0xFF);
}

/** Write a 32-bit, little-endian word to a char buffer */
void SetLE32(char* pBuffer, uint32_t nWord)
{
    *pBuffer = char(nWord & 0xFF);
    *(pBuffer + 1) = char((nWord >> 8) & 0xFF);
    *(pBuffer + 2) = char((nWord >> 16) & 0xFF);
    *(pBuffer + 3) = char((nWord >> 24) & 0xFF);
}

//Functions
static bool OpenFile(); //Opens WAVE file
static void CloseFile(); //Closes WAVE file
static bool OpenReplay(); //Opens audio replay (output) device
static void CloseReplay(); //Closes audio replay device
static bool OpenRecord(); //Opens audio record (input) device
static void CloseRecord(); //Closes audio record device
static void SetPlayHead(int nPosition); //Positions the playhead at the specified number of frames from the start
static void ShowHeadPosition(); //Update the head position indication
static void ShowMenu(); //Update display
static void HandleControl(); //Handle user input
static bool Play(); //Replay one frame of audio
static bool Record(); //Record one frame of audio
static bool LoadProject(string sName); //Loads a project called sName
static bool SaveProject(string sName = ""); //Loads a project called sName

//Global variables
static int g_nChannels; //Number of channels in replay file
//!@todo Make quantity of tracks dynamic
static Track g_track[MAX_TRACKS]; //Array of track classes
static int g_nSamplerate = SAMPLERATE; //Samples per second
static int g_nFrameSize; //Frame size - size of a single sample of all channels (sample size x quantity of channels)
static int g_nPeriodSize; //Period size - size of all samples in each period (sample size x quantity of channels x PERIOD_SIZE)
static char* g_pSilence; //Pointer to one period of silent samples
//Transport control
static int g_nRecA; //Which track is recording A-leg input (-1 = none)
static int g_nRecB; //Which track is recording B-leg input (-1 = none)
static int g_nTransport; //Transport control status
static bool g_bRecordEnabled; //True if recording enabled
//Tape position (in blocks - one block is one sample of all tracks)
static int g_nHeadPos; //Position of 'play head' in frames
static int g_nLastFrame; //Last frame
static int g_nRecordOffset; //Quantity of frames delay between replay and record
static unsigned int g_nUnderruns; //Quantity of replay buffer underruns
static unsigned int g_nOverruns; //Quantity of record buffer overruns
//file system
static string g_sPath; //Path to project
static string g_sProject; //Project name
//Audio inteface
static snd_pcm_t* g_pPcmRecord; //Pointer to record stream
static snd_pcm_t* g_pPcmPlay; //Pointer to playback stream
static string g_sPcmPlayName = "default";
static string g_sPcmRecName = "default";
WINDOW* g_pWindowRouting; //Pointer to ncurses window
//File offsets (in bytes)
static off_t g_offStartOfData; //Offset of data in wave file
static off_t g_offEndOfData; //Offset of end of data in wave file (end of file)
//General application
static bool g_bLoop; //True whilst main loop is running
static int g_fdWave; //File descriptor of replay file
static int g_nSelectedTrack; //Index of selected track
int g_nDebug; //General purpose debug integer
static char* g_pReadBuffer; //Buffer to hold data read from file
static int16_t g_pWriteBuffer[PERIOD_SIZE * 2]; //Buffer to hold data to be written to audio output device

/** Structure representing RIFF WAVE format chunk header (without id or size, i.e. 8 bytes smaller) **/
struct WaveHeader
{
    uint16_t nAudioFormat; //1=PCM
    uint16_t nNumChannels; //Number of channels in project
    uint32_t nSampleRate; //Samples per second - expect SAMPLERATE
    uint32_t nByteRate; //nSamplrate * nNumChannels * nBitsPerSample / 8
    uint16_t nBlockAlign; //nNumChannels * nBitsPerSample / 8 (bytes for one sample of all chanels)
    uint16_t nBitsPerSample; //Expect 16
};

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
            wprintw(g_pWindowRouting, "     MUTE    ", g_track[i].nMonMixA, g_track[i].nMonMixB);
            wattroff(g_pWindowRouting, COLOR_PAIR(RED_BLACK));
        }
        else
            wprintw(g_pWindowRouting, " %s  %s", MIX_LEVEL[g_track[i].nMonMixA].c_str(), MIX_LEVEL[g_track[i].nMonMixB].c_str());
    }
    wrefresh(g_pWindowRouting);
    switch(g_nTransport)
    {
        case TC_STOP:
            if(g_bRecordEnabled)
                attron(COLOR_PAIR(WHITE_RED));
            else
                attron(COLOR_PAIR(BLACK_GREEN));
            mvprintw(0, 20, " STOP ");
            if(g_bRecordEnabled)
                attroff(COLOR_PAIR(WHITE_RED));
            else
                attroff(COLOR_PAIR(BLACK_GREEN));
            break;
        case TC_PLAY:
            if(g_bRecordEnabled)
                attron(COLOR_PAIR(WHITE_RED));
            else
                attron(COLOR_PAIR(BLACK_GREEN));
            mvprintw(0, 20, " PLAY ");
            if(g_bRecordEnabled)
                attroff(COLOR_PAIR(WHITE_RED));
            else
                attroff(COLOR_PAIR(BLACK_GREEN));
            break;
    }
    refresh();
}

void ShowHeadPosition()
{
    attron(COLOR_PAIR(WHITE_MAGENTA));
    unsigned int nMinutes = g_nHeadPos / g_nSamplerate / 60;
    unsigned int nSeconds = (g_nHeadPos - nMinutes * g_nSamplerate * 60) / g_nSamplerate;
    unsigned int nMillis = (g_nHeadPos - (nMinutes * 60 + nSeconds) * g_nSamplerate) * 1000 / 44100;
    mvprintw(0, 0, "Position: %02d:%02d.%03d ", nMinutes, nSeconds, nMillis);
    attroff(COLOR_PAIR(WHITE_MAGENTA));
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
        case 'o':
            //Open project
            //!@todo Implement open project
            break;
        case KEY_DOWN:
            //Select next track
            if(++g_nSelectedTrack >= g_nChannels)
                g_nSelectedTrack = g_nChannels - 1;
            break;
        case KEY_UP:
            //Select previou track
            if(--g_nSelectedTrack < 0)
                g_nSelectedTrack = 0;
            break;
        case KEY_RIGHT:
            //Increase monitor level
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
        case KEY_LEFT:
            //Decrease monitor level
            if(g_track[g_nSelectedTrack].bMute)
                g_track[g_nSelectedTrack].bMute = false;
            else
            {
                if(g_track[g_nSelectedTrack].nMonMixA < 16)
                    ++g_track[g_nSelectedTrack].nMonMixA;
                if(g_track[g_nSelectedTrack].nMonMixB < 16)
                    ++g_track[g_nSelectedTrack].nMonMixB;
            }
            break;
        case KEY_SRIGHT:
            //Pan monitor right
            if(g_track[g_nSelectedTrack].bMute)
                g_track[g_nSelectedTrack].bMute = false;
            else
            {
                if(g_track[g_nSelectedTrack].nMonMixA < 16)
                    ++g_track[g_nSelectedTrack].nMonMixA;
                if(g_track[g_nSelectedTrack].nMonMixB > 0)
                    --g_track[g_nSelectedTrack].nMonMixB;
            }
            break;
        case KEY_SLEFT:
            //Pan monitor left
            if(g_track[g_nSelectedTrack].bMute)
                g_track[g_nSelectedTrack].bMute = false;
            else
            {
                if(g_track[g_nSelectedTrack].nMonMixA > 0)
                    --g_track[g_nSelectedTrack].nMonMixA;
                if(g_track[g_nSelectedTrack].nMonMixB < 16)
                    ++g_track[g_nSelectedTrack].nMonMixB;
            }
            break;
        case 'L':
            //Pan fully left
            if(g_track[g_nSelectedTrack].bMute)
                g_track[g_nSelectedTrack].bMute = false;
            g_track[g_nSelectedTrack].nMonMixA = 0;
            g_track[g_nSelectedTrack].nMonMixB = 16;
            break;
        case 'R':
            //Pan fully right
            if(g_track[g_nSelectedTrack].bMute)
                g_track[g_nSelectedTrack].bMute = false;
            g_track[g_nSelectedTrack].nMonMixA = 16;
            g_track[g_nSelectedTrack].nMonMixB = 0;
            break;
        case 'l':
            //Pan fully left and pad to fit track count
            if(g_track[g_nSelectedTrack].bMute)
                g_track[g_nSelectedTrack].bMute = false;
            g_track[g_nSelectedTrack].nMonMixA = 4;
            g_track[g_nSelectedTrack].nMonMixB = 16;
            break;
        case 'r':
            //Pan fully right and pad to fit track count
            if(g_track[g_nSelectedTrack].bMute)
                g_track[g_nSelectedTrack].bMute = false;
            g_track[g_nSelectedTrack].nMonMixA = 16;
            g_track[g_nSelectedTrack].nMonMixB = 4;
            break;
        case 'C':
            //Pan centre
            if(g_track[g_nSelectedTrack].bMute)
                g_track[g_nSelectedTrack].bMute = false;
            g_track[g_nSelectedTrack].nMonMixA = 1;
            g_track[g_nSelectedTrack].nMonMixB = 1;
            break;
        case 'c':
            //Pan centre and pad to fit track count
            if(g_track[g_nSelectedTrack].bMute)
                g_track[g_nSelectedTrack].bMute = false;
            g_track[g_nSelectedTrack].nMonMixA = 4; //!@todo should work out from g_nChannels
            g_track[g_nSelectedTrack].nMonMixB = 4;
            break;
        case 'a':
            //Toggle record from A
            if(g_nRecA == g_nSelectedTrack)
            {
                g_track[g_nRecA].bRecording = false;
                g_nRecA = -1;
            }
            else
            {
                if(g_nRecA > -1)
                    g_track[g_nRecA].bRecording = false;
                g_nRecA = g_nSelectedTrack;
                if(g_pPcmRecord)
                    g_track[g_nRecA].bRecording = true;
            }
            if((-1 == g_nRecA) && (-1 == g_nRecB))
                CloseRecord();
            break;
        case 'b':
            //Toggle record from B
            if(g_nRecB == g_nSelectedTrack)
            {
                g_track[g_nRecB].bRecording = false;
                g_nRecB = -1;
            }
            else
            {
                if(g_nRecB > -1)
                    g_track[g_nRecB].bRecording = false;
                g_nRecB = g_nSelectedTrack;
                if(g_pPcmRecord)
                    g_track[g_nRecB].bRecording = true;
            }
            if((-1 == g_nRecA) && (-1 == g_nRecB))
                CloseRecord();
            break;
        case 'm':
            //Toggle monitor mute
            g_track[g_nSelectedTrack].bMute = !g_track[g_nSelectedTrack].bMute;
            break;
        case 'M':
            //Toggle all monitor mute
            {
                bool bMute = !g_track[g_nSelectedTrack].bMute;
                for(int i = 0; i < g_nChannels; ++i)
                    g_track[i].bMute = bMute;
            }
            break;
        case ' ':
            //Start / Stop
            switch(g_nTransport)
            {
                case TC_STOP:
                    //Currently stopped so need to open files and interfaces and start
                    if(OpenReplay())
                        g_nTransport = TC_PLAY;
                    //!@todo Configure whether auto return to zero when playing from end of track
                    if(!g_bRecordEnabled && g_nHeadPos >= g_nLastFrame)
                        g_nHeadPos = 0;
                    SetPlayHead(g_nHeadPos);
                    break;
                case TC_PLAY:
                    //Currently playing so need to stop
                    CloseReplay();
                    g_nTransport = TC_STOP;
                    g_bRecordEnabled = false;
                    CloseRecord();
                    g_nLastFrame = (g_offEndOfData - g_offStartOfData) / g_nFrameSize; //Update file size
                    break;
            }
            break;
        case 'G':
            //Toggle record mode
            if(g_bRecordEnabled)
                CloseRecord();
            g_bRecordEnabled = !g_bRecordEnabled;
            break;
        case KEY_HOME:
            //Go to home position
            SetPlayHead(0);
            break;
        case KEY_END:
            //Go to end of track
            SetPlayHead(g_nLastFrame);
            break;
        case ',':
            //Back 1 seconds
            SetPlayHead(g_nHeadPos - 1 * g_nSamplerate);
            break;
        case '.':
            //Forward 1 seconds
            SetPlayHead(g_nHeadPos + 1 * g_nSamplerate);
            break;
        case '<':
            //Back 10 seconds
            SetPlayHead(g_nHeadPos - 10 * g_nSamplerate);
            break;
        case '>':
            //Forward 10 seconds
            SetPlayHead(g_nHeadPos + 10 * g_nSamplerate);
            break;
        case 'e':
            //Clear errors
            g_nUnderruns = 0;
            g_nOverruns = 0;
            move(18, 0);
            clrtoeol();
            move(19, 0);
            clrtoeol();
            break;
        case '+':
            //Increase record offset
            //!@todo Remove record offset adjustment from user interface
            g_nRecordOffset += 100;
            mvprintw(20,0,"Record offset: %d           ", g_nRecordOffset);
            break;
        case '-':
            //Decrease record offset
            g_nRecordOffset -= 100;
            mvprintw(20,0,"Record offset: %d           ", g_nRecordOffset);
            break;
        case 'z':
            //Debug
            break;
        default:
            return; //Avoid updating menu if invalid keypress
    }
    ShowMenu();
}

//Opens WAVE file and reads header
bool OpenFile()
{
    // Expect header to be 12 + 24 + 8 = 44
	//**Open file**
	if(g_fdWave < 0)
    {
        //File not open
        string sFilename = g_sPath;
        sFilename.append(g_sProject);
        sFilename.append(".wav");
        g_fdWave = open(sFilename.c_str(), O_RDWR | O_CREAT, 0644);
        if(g_fdWave <= 0)
        {
            cerr << "Unable to open file" << sFilename << " - error " << errno << endl;
            CloseReplay();
            return false;
        }

        //**Read RIFF headers**
        char pBuffer[12];
        if(read(g_fdWave, pBuffer, 12) < 12)
        {
            //!@todo Write header if not existing, e.g. create new file
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

        char pWaveBuffer[sizeof(WaveHeader)];
        WaveHeader* pWaveHeader = (WaveHeader*)pWaveBuffer;

        //Look for chuncks
        while(read(g_fdWave, pBuffer, 8) == 8) //read ckID and cksize
        {
            char sId[5] = {0,0,0,0,0}; //buffer for debug output only
            strncpy(sId, pBuffer, 4); //chunk ID is first 32-bit word
            uint32_t nSize = (uint32_t)*(pBuffer + 4); //chunk size is second 32-bit word

    //        cerr << endl << "Found chunk " << sId << " of size " << nSize << endl;
            if(0 == strncmp(pBuffer, "fmt ", 4)) //chunk ID is first 32-bit word
            {
                //Found format chunk
                if(read(g_fdWave, pWaveBuffer, sizeof(pWaveBuffer)) < (int)sizeof(pWaveBuffer))
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
                g_nFrameSize = g_nChannels * SAMPLESIZE;
                attron(COLOR_PAIR(WHITE_MAGENTA));
                mvprintw(0, 27, " % 2d-bit % 6dHz ", pWaveHeader->nBitsPerSample, pWaveHeader->nSampleRate);
                attroff(COLOR_PAIR(WHITE_MAGENTA));
                lseek(g_fdWave, nSize - sizeof(pWaveBuffer), SEEK_CUR); //ignore other parameters
            }
            else if(0 == strncmp(pBuffer, "data ", 4))
            {
                //Aligned with start of data so must have read all header
                g_offStartOfData = lseek(g_fdWave, 0, SEEK_CUR);
                g_offEndOfData = lseek(g_fdWave, 0, SEEK_END);

                if(g_offStartOfData != 44)
                {
                    mvprintw(18, 0, "Importing file - please wait...");
                    attron(COLOR_PAIR(COLOR_RED));
                    mvprintw(19, 0, "                                    ");
                    attroff(COLOR_PAIR(COLOR_RED));
                    refresh();
                    //Use minimal RIFF header - write new header, move wave data then truncate file
                    off_t nWaveSize = g_offEndOfData - g_offStartOfData;
                    char pHeader[20];
                    strncpy(pHeader, "RIFF", 4);
                    SetLE32(pHeader + 4, nWaveSize + 36); //size of RIFF chunck
                    strncpy(pHeader + 8, "WAVE", 4);
                    strncpy(pHeader + 12, "fmt ", 4); //start of format chunk
                    SetLE32(pHeader + 16, 16); //size of format chunck
                    pwrite(g_fdWave, pHeader, 20, 0);
                    pWaveHeader->nAudioFormat = 1; //PCM
                    pwrite(g_fdWave, pWaveBuffer, 16, 20);
                    strncpy(pHeader, "data", 4);
                    SetLE32(pHeader + 4, nWaveSize);
                    pwrite(g_fdWave, pHeader, 8, 36);
                    char pData[512];
                    off_t offRead = g_offStartOfData;
                    off_t offWrite = 44;
                    int nRead;
                    int nProgress = 0;
                    while((nRead = pread(g_fdWave, pData, sizeof(pData), offRead)) > 0)
                    {
                        pwrite(g_fdWave, pData, sizeof(pData), offWrite);
                        offWrite += nRead;
                        offRead += nRead;
                        int nProgressTemp = 100 * offRead / nWaveSize;
                        if(nProgressTemp != nProgress)
                        {
                            nProgress = nProgressTemp;
                            mvprintw(18, 32, "% 2d%%", nProgress);
                            attron(COLOR_PAIR(COLOR_GREEN));
                            mvprintw(19, nProgress / 2.77, " ");
                            attroff(COLOR_PAIR(COLOR_GREEN));
                            refresh();
                        }
                    }
                    ftruncate(g_fdWave, 44 + nWaveSize);
                    g_offStartOfData = 44;
                    //!@todo Show indication of file changing and progress bar
                    move(18, 0);
                    clrtoeol();
                    move(19, 0);
                    clrtoeol();
                    refresh();
                }

                g_offEndOfData = lseek(g_fdWave, 0, SEEK_END);
                g_nLastFrame = (g_offEndOfData - g_offStartOfData) / (g_nFrameSize);
                return true;
            }
            else
                lseek(g_fdWave, nSize, SEEK_CUR); //Not found desired chunk so seek to next chunk
        }
        cerr << "Failed to get WAVE header";
    }
    return false;
}

//Closes WAVE file
void CloseFile()
{
    if(g_fdWave > 0)
    {
        //Write RIFF chunck length
        char pBuffer[4];
        SetLE32(pBuffer, g_offEndOfData - 8);
        pwrite(g_fdWave, pBuffer, 4, 4);
        close(g_fdWave);
    }
    g_fdWave = -1;
    g_nTransport = TC_STOP;
    g_nChannels = 0;
}

void SetPlayHead(int nPosition)
{
    g_nHeadPos = nPosition;
    if(g_nHeadPos < 0)
        g_nHeadPos = 0;
    if(g_nHeadPos > g_nLastFrame)
        g_nHeadPos = g_nLastFrame;
    if(g_fdWave > 0)
        lseek(g_fdWave, g_offStartOfData + g_nHeadPos * g_nFrameSize, SEEK_SET);
    ShowHeadPosition();
}

//Open replay device
bool OpenReplay()
{
    int nError;
    if(g_pPcmPlay)
        return true;

    //**Open sound device**
    if((nError = snd_pcm_open(&g_pPcmPlay, g_sPcmPlayName.c_str(), SND_PCM_STREAM_PLAYBACK, 0)) != 0)
    {
        cerr << "Unable to open replay device: " << snd_strerror(nError) << endl;
        CloseReplay();
        return false;
    }
    if((nError = snd_pcm_set_params(g_pPcmPlay,
                                  SND_PCM_FORMAT_S16_LE, //!@todo does this depend on WAVE file format?
                                  SND_PCM_ACCESS_RW_INTERLEAVED,
                                  2, //2 channels (left & right)
                                  g_nSamplerate,
                                  0, //Don't resample
                                  REPLAY_LATENCY)) != 0)
    {
        cerr << "Unable to configure replay device: " << snd_strerror(nError) << endl;
        CloseReplay();
        return false;
    }

	return true;
}

//Close replay device
void CloseReplay()
{
    if(g_pPcmPlay)
        snd_pcm_close(g_pPcmPlay);
    g_pPcmPlay = NULL;
    if(!g_bRecordEnabled)
        g_nTransport = TC_STOP;
    ShowMenu();
}

//Open record device
bool OpenRecord()
{
    if(g_pPcmRecord)
        return true; //Already open

    //**Open sound device**
    int nError;
    if((nError = snd_pcm_open(&g_pPcmRecord, g_sPcmRecName.c_str(), SND_PCM_STREAM_CAPTURE, 0)) != 0)
    {
        cerr << "Unable to open record device: " << snd_strerror(nError) << endl;
        CloseRecord();
        return false;
    }
    if((nError = snd_pcm_set_params(g_pPcmRecord,
                                  SND_PCM_FORMAT_S16_LE, //!@todo does this depend on WAVE file format?
                                  SND_PCM_ACCESS_RW_INTERLEAVED,
                                  2, //2 channels (left & right)
                                  g_nSamplerate,
                                  0, //Don't resample
                                  RECORD_LATENCY)) != 0)
    {
        cerr << "Unable to configure record device: " << snd_strerror(nError) << endl;
        CloseRecord();
        return false;
    }

    if(g_nRecA > -1)
        g_track[g_nRecA].bRecording = true;
    if(g_nRecB > -1)
        g_track[g_nRecB].bRecording = true;

	return true;
}

//Close record device
void CloseRecord()
{
    if(g_pPcmRecord)
        snd_pcm_close(g_pPcmRecord);
    g_pPcmRecord = NULL;
    //!@todo Probably need to disable recording in tracks elsewhere
    if(g_nRecA > -1)
        g_track[g_nRecA].bRecording = false;
    if(g_nRecB > -1)
        g_track[g_nRecB].bRecording = false;
    ShowMenu();
}

bool Play()
{
    if(!g_pPcmPlay || (g_fdWave < 0) || ((TC_PLAY != g_nTransport)))
        return false;

    //Read frame from file
    //!@todo handle different bits/sample size
    memset(g_pWriteBuffer, 0, sizeof(g_pWriteBuffer)); //silence output buffer
    int nRead = read(g_fdWave, g_pReadBuffer, g_nPeriodSize);
    bool bPlaying = (nRead > 0); //If we fail to read then we should stop
    //Mix each frame to output buffer
    //iterate through input buffer one frame at a time, adding gain-adjusted value to output buffer
    for(int nPos = 0; nPos < nRead ; nPos += g_nFrameSize)
    {
        for(int nChan = 0; nChan < g_nChannels; ++nChan)
        {
            uint16_t nSample = g_pReadBuffer[nPos + (SAMPLESIZE * nChan)] + (g_pReadBuffer[nPos + (SAMPLESIZE * nChan) + 1] << 8); //get little endian sample into 16-bit word
            g_pWriteBuffer[nPos / g_nChannels] += g_track[nChan].MixA(nSample);
            g_pWriteBuffer[nPos / g_nChannels + 1] += g_track[nChan].MixB(nSample);
        }
    }
    snd_pcm_sframes_t nBlocks;
    //Send output buffer to soundcard replay output
    if(bPlaying)
    {
        g_nHeadPos += PERIOD_SIZE;
        nBlocks = snd_pcm_writei(g_pPcmPlay, g_pWriteBuffer, PERIOD_SIZE);
        switch(nBlocks)
        {
            case -EBADFD:
                attron(COLOR_PAIR(WHITE_RED));
                mvprintw(18, 31, "File descriptor in bad state");
                attroff(COLOR_PAIR(WHITE_RED));
                snd_pcm_recover(g_pPcmPlay, nBlocks, 1); //Attempt to recover from error
                break;
            case -EPIPE:
                //Broken Pipe == Underrun
                attron(COLOR_PAIR(WHITE_RED));
                mvprintw(18, 0, "Underruns:% 4d", ++g_nUnderruns);
                attroff(COLOR_PAIR(WHITE_RED));
                snd_pcm_recover(g_pPcmPlay, nBlocks, 1); //Attempt to recover from error
                break;
            case -ESTRPIPE:
                attron(COLOR_PAIR(WHITE_RED));
                mvprintw(18, 12, "Streams pipe error");
                attroff(COLOR_PAIR(WHITE_RED));
                snd_pcm_recover(g_pPcmPlay, nBlocks, 1); //Attempt to recover from error
                break;
        }
    }
    ShowHeadPosition();
    //Return true if more to play else false if at end of file. Don't fail if we are in record mode
    return bPlaying;
}

bool Record()
{
    if(TC_PLAY != g_nTransport)
        return false; //Can't record if we are not rolling
    if(!g_bRecordEnabled)
        return false; //Don't record if we are not in record mode
    if(g_fdWave <= 0)
        return false; //WAVE file not open so nothing to record to
    if((-1 == g_nRecA) && (-1 == g_nRecB))
        return false; //No record channels primed
    if(g_nHeadPos < g_nRecordOffset)
        return true; //Record head not past start of file
    if(!g_pPcmRecord && !OpenRecord())
        return false; //Record device not open and failed to open when we tried - oops!
    //!@todo Optimse - do not create record buffer for each frame - use global buffer
    if(g_nHeadPos >= g_nLastFrame)
    {
        //extend file if recording
        ssize_t nWritten = pwrite(g_fdWave, g_pSilence, g_nPeriodSize, g_offEndOfData);
        if(nWritten > 0)
        {
            g_offEndOfData += nWritten;
            g_nLastFrame += PERIOD_SIZE;;
        }
        else
            cerr << "Failed to extend file" << endl;
    }


    unsigned char pRecBuffer[2 * SAMPLESIZE * PERIOD_SIZE]; // buffer to hold record frame
    memset(pRecBuffer, 0, sizeof(pRecBuffer)); //silence record buffer
    snd_pcm_sframes_t nBlocks = snd_pcm_readi(g_pPcmRecord, pRecBuffer, PERIOD_SIZE);
    switch(nBlocks)
    {
        case -EBADFD:
            attron(COLOR_PAIR(WHITE_RED));
            mvprintw(19, 31, "File descriptor in bad state");
            attroff(COLOR_PAIR(WHITE_RED));
            snd_pcm_recover(g_pPcmRecord, nBlocks, 1); //Attempt to recover from error
            break;
        case -EPIPE:
            //Broken Pipe == Underrun
            attron(COLOR_PAIR(WHITE_RED));
            mvprintw(19, 0, "Overruns:% 4d ", ++g_nOverruns);
            attroff(COLOR_PAIR(WHITE_RED));
            snd_pcm_recover(g_pPcmRecord, nBlocks, 1); //Attempt to recover from error
            break;
        case -ESTRPIPE:
            attron(COLOR_PAIR(WHITE_RED));
            mvprintw(19, 12, "Streams pipe error");
            attroff(COLOR_PAIR(WHITE_RED));
            snd_pcm_recover(g_pPcmRecord, nBlocks, 1); //Attempt to recover from error
            break;
    }
    //Write samples to file
    off_t offRewrite = g_offStartOfData + (g_nHeadPos - g_nRecordOffset) * g_nFrameSize;
    int nRead = pread(g_fdWave, g_pReadBuffer, g_nPeriodSize, offRewrite);
    for(unsigned int nSample = 0; nSample < PERIOD_SIZE; ++nSample)
    {
        if(-1 != g_nRecA)
        {
            memcpy(g_pReadBuffer + nSample * g_nFrameSize + (g_nRecA * SAMPLESIZE), (pRecBuffer + nSample * 4), SAMPLESIZE);
//            pwrite(g_fdWave, pRecBuffer + nSample * 4, 2,
//                g_offStartOfData + (g_nHeadPos + nSample - g_nRecordOffset) * g_nFrameSize  + g_nRecA * SAMPLESIZE);
        }
        if(-1 != g_nRecB)
        {
            memcpy(g_pReadBuffer + 2 + nSample * g_nFrameSize + (g_nRecA * SAMPLESIZE), (pRecBuffer + 2 + nSample * 4), SAMPLESIZE);
//            pwrite(g_fdWave, 2 + pRecBuffer + nSample * 4, 2,
//                2 + g_offStartOfData + (g_nHeadPos + nSample - g_nRecordOffset) * g_nFrameSize  + g_nRecA * SAMPLESIZE);
        }
    }
    pwrite(g_fdWave, g_pReadBuffer, nRead, offRewrite);
    return true;
}

bool LoadProject(string sName)
{
    //Project consists of sName.wav and sName.cfg
    //Close existing WAVE file and open new one
    CloseFile();
    CloseReplay();
    CloseRecord();
    attron(COLOR_PAIR(WHITE_MAGENTA));
    mvprintw(0, 45, "                                                    ");
    attroff(COLOR_PAIR(WHITE_MAGENTA));
    g_sProject = sName;
    if(!OpenFile())
        return false;
    attron(COLOR_PAIR(WHITE_MAGENTA));
    mvprintw(0, 45, "Project: %s", sName.c_str());
    attroff(COLOR_PAIR(WHITE_MAGENTA));

    //Get configuration
    string sConfig = g_sPath;
    sConfig.append(sName);
    sConfig.append(".cfg");
    FILE *pFile = fopen(sConfig.c_str(), "r");
    if(pFile)
    {
        char pLine[256];
        while(fgets(pLine, sizeof(pLine), pFile))
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
            if(0 == strncmp(pLine, "Pos=", 4))
                g_nHeadPos = atoi(pLine + 4); //Set transport position
            if(0 == strncmp(pLine, "Rof=", 4))
                g_nRecordOffset = atoi(pLine + 4); //Set record offset
        }
        fclose(pFile);
    }
    SetPlayHead(g_nHeadPos);
    g_nPeriodSize = g_nFrameSize * PERIOD_SIZE;
    g_nRecordOffset = g_nSamplerate * (RECORD_LATENCY + REPLAY_LATENCY) / 1000000;
    //Create new silent period
    delete[] g_pSilence;
    g_pSilence = new char[g_nPeriodSize];
    memset(g_pSilence, 0, g_nPeriodSize);
    //Create new read buffer
    delete[] g_pReadBuffer;
    g_pReadBuffer = new char[g_nPeriodSize];
    return true;
}

bool SaveProject(string sName)
{
    string sConfig = g_sPath;
    if(sName == "")
    {
        sConfig.append(g_sProject);
    }
    else
    {
        sConfig.append(sName);
        string sCpCmd = "cp ";
        sCpCmd.append(g_sPath);
        sCpCmd.append(g_sProject);
        sCpCmd.append(".wav");
        sCpCmd.append(" ");
        sCpCmd.append(g_sPath);
        sCpCmd.append(sName);
        sCpCmd.append(".wav");
        system(sCpCmd.c_str());
        g_sProject = sName;
    }
    sConfig.append(".cfg");
    FILE *pFile = fopen(sConfig.c_str(), "w+");
    if(pFile)
    {
        char pBuffer[32];
        for(int i = 0; i < g_nChannels; ++i)
        {
            memset(pBuffer, 0, sizeof(pBuffer));
            sprintf(pBuffer, "%02dL=%d\n", i, g_track[i].nMonMixA);
            fputs(pBuffer , pFile);
            memset(pBuffer, 0, sizeof(pBuffer));
            sprintf(pBuffer, "%02dR=%d\n", i, g_track[i].nMonMixB);
            fputs(pBuffer , pFile);
            memset(pBuffer, 0, sizeof(pBuffer));
            sprintf(pBuffer, "%02dM=%s",i, g_track[i].bMute?"1\n":"0\n");
            fputs(pBuffer , pFile);
        }
        memset(pBuffer, 0, sizeof(pBuffer));
        sprintf(pBuffer, "Pos=%d\n", g_nHeadPos);
        fputs(pBuffer , pFile);
        memset(pBuffer, 0, sizeof(pBuffer));
        sprintf(pBuffer, "Rof=%d\n", g_nRecordOffset);
//        fputs(pBuffer , pFile);

        fclose(pFile);
        return true;
    }
    return false;
}

int main()
{
    g_nDebug = 0;
    g_nTransport = TC_STOP;
    g_bRecordEnabled = false;
    g_nSelectedTrack = 0;
    g_nRecA = -1; //Deselect A-channel recording
    g_nRecB = -1; //Deselect B-channel recording
    g_fdWave = -1;
    g_pPcmPlay = NULL;
    g_pPcmRecord = NULL;
    g_pSilence = NULL;
    g_pReadBuffer = NULL;
    g_nChannels = MAX_TRACKS;
    g_nRecordOffset = SAMPLERATE * (RECORD_LATENCY + REPLAY_LATENCY) / 1000000;
    g_sPath = "/media/multitrack/"; //!@todo replace this absolute path
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
    mvprintw(0, 0, "                                             ");
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

    while(g_bLoop)
    {
        HandleControl();
        if(!Play() && TC_PLAY == g_nTransport)
            CloseReplay();
        if(!Record())
            CloseRecord();
        if(TC_STOP == g_nTransport)
            usleep(1000);
    }
    CloseReplay();
    CloseRecord();
    SaveProject();
    CloseFile();
    delete[] g_pSilence;
    delete[] g_pReadBuffer;
    endwin();
    return 0;
}
