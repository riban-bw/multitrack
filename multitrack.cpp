/** Multitrack recorder
*   Record from one or two inputs
*   Mixdown playback of up any / all tracks to two outputs
*   Will run on low-power devices such as Raspberry Pi
* 
*   Copyright (C) Brian Walton 2014
*   GPL V3.0
*/

//!@todo Delays between playback and record
//!@todo Use configuration instead of hardcoding
//!@todo Add Recording indication
//!@todo Add "Overwrite" warning
 
#include <ncurses.h>
#include <stdlib.h>
#include <string>

using namespace std;

static const int CHANNEL_QUANT = 16;
static const int STATE_MUTE = 0;
static const int STATE_PLAY = 1;
static const int STATE_REC =  2;
static const string FORMAT = " -b16 -c1 -esigned-integer -r44100 -L -traw";
static const string PATH = " /mnt/multitrack/";

int main()
{
  WINDOW* pWindowRouting;
  bool abMute[CHANNEL_QUANT]; //Mute status of each channel
  bool abRecA[CHANNEL_QUANT]; //Record-A status of each channel
  bool abRecB[CHANNEL_QUANT]; //Record-B status of each channel
  int nRecA = -1; //Track recording from input A
  int nRecB = -1; //Track recording from input B
  bool bAutoMute = true; //True to automatically mute when recording
  initscr();
  noecho();
  keypad(stdscr, TRUE);
  start_color();
	init_pair(1, COLOR_BLACK, COLOR_RED);
	init_pair(2, COLOR_BLACK, COLOR_GREEN);
  printw("Multitrack\n");
  pWindowRouting = newwin(CHANNEL_QUANT, 30, 1, 0);
  wrefresh(pWindowRouting);
  refresh();
  int nCursor = 0;
  for(int i = 0; i < CHANNEL_QUANT; ++i)
  {
    abMute[i] = false;
    abRecA[i] = false;
    abRecB[i] = false;
    mvwprintw(pWindowRouting, i, 1, "Track %02d: PLAY", i + 1);
    //Ensure all tracks exist
    string sTouchCommand = "touch";
    char sTrack[10];
    sprintf(sTrack, "track%02d", i);
    sTouchCommand.append(PATH);
    sTouchCommand.append(sTrack);
    system(sTouchCommand.c_str());
  }
  mvwprintw(pWindowRouting, nCursor, 0, ">");
  mvprintw(CHANNEL_QUANT + 2, 0, "STOP");
  wrefresh(pWindowRouting);
  int nInput;
  bool bRun = false;

  while('q' != (nInput = getch()))
  {
    switch(nInput)
    {
      case KEY_UP:
        mvwprintw(pWindowRouting, nCursor, 0, " ");
        if(nCursor <= 0)
          nCursor = CHANNEL_QUANT - 1;
        else
          --nCursor;
        mvwprintw(pWindowRouting, nCursor, 0, ">");
        break;
      case KEY_DOWN:
        mvwprintw(pWindowRouting, nCursor, 0, " ");
        if(nCursor >= CHANNEL_QUANT - 1)
          nCursor = 0;
        else
          ++nCursor;
        mvwprintw(pWindowRouting, nCursor, 0, ">");
        break;
      case 'm':
      {
        abMute[nCursor] = !abMute[nCursor];
        mvwprintw(pWindowRouting, nCursor, 11, abMute[nCursor]?"MUTE":"PLAY");
        break;
      }
      case 'a':
      {
        abRecA[nCursor] = !abRecA[nCursor];
        if(abRecA[nCursor])
          wattron(pWindowRouting, COLOR_PAIR(1));
        mvwprintw(pWindowRouting, nCursor, 16, abRecA[nCursor]?"REC-A":"     ");
        wattroff(pWindowRouting, COLOR_PAIR(1));
        for(int i = 0; i < CHANNEL_QUANT; ++i)
        {
          if(i != nCursor)
          {
            abRecA[i] = false;
            mvwprintw(pWindowRouting, i, 16, "     ");
          }
        }
        if(bAutoMute)
        {
          abMute[nCursor] = abRecA[nCursor];
          mvwprintw(pWindowRouting, nCursor, 11, abMute[nCursor]?"MUTE":"PLAY");
        }
        break;
      }
      case 'b':
      {
        abRecB[nCursor] = !abRecB[nCursor];
        if(abRecB[nCursor])
          wattron(pWindowRouting, COLOR_PAIR(1));
        mvwprintw(pWindowRouting, nCursor, 22, abRecB[nCursor]?"REC-B":"     ");
        wattroff(pWindowRouting, COLOR_PAIR(1));
        for(int i = 0; i < CHANNEL_QUANT; ++i)
        {
          if(i != nCursor)
          {
            abRecB[i] = false;
            mvwprintw(pWindowRouting, i, 22, "     ");
          }
        }
        if(bAutoMute)
        {
          abMute[nCursor] = abRecB[nCursor];
          mvwprintw(pWindowRouting, nCursor, 11, abMute[nCursor]?"MUTE":"PLAY");
        }
        break;
      }
      case ' ': //run with spacebar
      {
        bRun = ! bRun;
        if(bRun)
        {
          string sPlayCommand = "play -q";
          string sRecCommand = "arecord -I -q -Dsysdefault:CODEC -fcd ";
          sRecCommand.append(PATH);
          sRecCommand.append("newrec &");
          int nPlay = 0;
          nRecA = -1;
          nRecB = -1;
          for(int i = 0; i < CHANNEL_QUANT; ++i)
          {
            char sTrack[10];
            sprintf(sTrack, "track%02d", i);
            if(!abMute[i])
            {
              ++nPlay;
              sPlayCommand.append(FORMAT);
              sPlayCommand.append(PATH);
              sPlayCommand.append(sTrack);
            }
            if(abRecA[i])
            {
                nRecA = i;
            }
            if(abRecB[i])
            {
                nRecB = i;
            }
          }
          if(nPlay > 1)
            sPlayCommand.append(" --combine mix-power");
          sPlayCommand.append(" -t alsa &");
          //mvprintw(CHANNEL_QUANT + 2, 0, sPlayCommand.c_str()); //debug
          if(nPlay > 0)
            system(sPlayCommand.c_str());
          if(-1 != nRecA || -1 != nRecB)
          {
            attron(COLOR_PAIR(1));
            mvprintw(CHANNEL_QUANT + 2, 0, "REC ");
            attroff(COLOR_PAIR(1));
            system(sRecCommand.c_str());
          }
          else
          {
            attron(COLOR_PAIR(2));
            mvprintw(CHANNEL_QUANT + 2, 0, "PLAY");
            attroff(COLOR_PAIR(1));
            system(sRecCommand.c_str());
          }
        }
        else
        {
          mvprintw(CHANNEL_QUANT + 2, 0, "STOP");
          system("killall -q arecord");
          system("killall -q play");
          if(-1 != nRecA)
          {
            char sTrack[10];
            sprintf(sTrack, "track%02d", nRecA);
            string sMoveCommand = "mv";
            sMoveCommand.append(PATH);
            sMoveCommand.append("newrec.0 ");
            sMoveCommand.append(PATH);
            sMoveCommand.append(sTrack);
            system(sMoveCommand.c_str());
          }
          if(-1 != nRecB)
          {
            char sTrack[10];
            sprintf(sTrack, "track%02d", nRecB);
            string sMoveCommand = "mv";
            sMoveCommand.append(PATH);
            sMoveCommand.append("newrec.1 ");
            sMoveCommand.append(PATH);
            sMoveCommand.append(sTrack);
            system(sMoveCommand.c_str());
          }
        }
        break;
      }
      case 'M':
        //Global mute
        bool bMute = !abMute[0];
        for(int i = 0; i < CHANNEL_QUANT; ++i)
        {
          abMute[i] = bMute;
          mvwprintw(pWindowRouting, i, 11, abMute[i]?"MUTE":"PLAY");
        }
        break;
    }
    mvprintw(CHANNEL_QUANT + 2, 0, ""); // show output at bottom
    wrefresh(pWindowRouting);
    refresh();
  }
  endwin();
  return 0;
}
