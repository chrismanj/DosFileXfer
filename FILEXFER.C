#include <stdlib.h>
#include <stdio.h>
#include <dos.h>
#include <time.h>

#include "..\common\include\types.h"
#include "..\common\include\debug.h"
#include "..\common\include\chrgraph.h"
#include "..\common\include\jsctime.h"
#include "..\common\include\speaker.h"
#include "..\filexfer\include\filexfer.h"

WORD *xmit_db_rect;
long total_bytes_sent;
WORD chars_per_second;
long avg_chars_per_second;
long num_times_CPS_calculated;
time_t start_time, current_time;
long elapsed_time;
long time_left;
int error_count;
int max_errors;
long file_size;
WORD old_cursor_shape;

extern int sound_on;

void PlayMusic(void)
{
  if (sound_on)
  {
    SoundOn();
    DoNote(F4, 20);
    DoNote(A4, 20);
    DoNote(D4, 20);
    DoNote(F4, 20);
    DoNote(A4, 20);
    DoNote(D4, 20);
    DoNote(F4, 20);
    DoNote(A4, 20);
    DoNote(D4, 20);
    SoundOff();
    /*
    Pause (50L);
    SoundOn();
    DoNote(D4, 20);
    DoNote(A4, 20);
    DoNote(F4, 20);
    DoNote(D4, 20);
    DoNote(A4, 20);
    DoNote(F4, 20);
    DoNote(D4, 20);
    DoNote(A4, 20);
    DoNote(F4, 20);
    SoundOff();
*/
  }
  else
    Pause(300L);
}

void DrawXferBox(char *filename, int xfer_type)
{
  xmit_db_rect = SaveAndDrawBox(6, 10, 12, 60, ' ');
  OutTextAt(8, 16, "Filename:");
  OutTextAt(8, 26, filename);
  OutTextAt(10, 12, "Elapsed time:");
  OutTextAt(10, 36, "Time left:");
  OutTextAt(10, 57, "CPS:");
  if (xfer_type == 0)
    OutTextAt(12, 14, "Bytes sent:");
  else
    OutTextAt(12, 14, "Bytes recd:");
  OutTextAt(12, 37, "of");
  OutTextAt(13, 18, "Status:");
  OutTextAt(14, 14, "Last error:");
  OutTextAt(15, 13, "Error count:");
}

long *InitXferBox(char *filename, int max_errs, int xfer_type, long size)
{
  char temp_string_space[11];

  old_cursor_shape = SetCursorShape(0x2000);
  avg_chars_per_second = 0;
  num_times_CPS_calculated = 0;
  xmit_db_rect = NULL;
  total_bytes_sent = 0;
  error_count = 0;

  DrawXferBox(filename, xfer_type);

  file_size = size;
  if (file_size > 0)
    OutTextAt(12, 40, ltoa(file_size, temp_string_space, 10));
  else
    OutTextAt(12, 40, "???");

  ShowXferStatus("Waiting to begin");
  max_errors = max_errs;

  return &file_size;
}

void SetXferBoxName(char *string)
{
  OutCharAt(6, 12, 'µ');
  OutText(string);
  OutText(" - Press ESC to abort");
  OutChar('Æ');
}

void RestoreUnderXferBox(void)
{
  if (xmit_db_rect != NULL)
  {
    RestoreRect(xmit_db_rect);
    xmit_db_rect = NULL;
  }
  SetCursorShape(old_cursor_shape);
}

int ShowXferError(char *string)
{
  char temp_string_space[4];

  HChar(14, 26, 42, ' ');
  OutTextAt(14, 26, string);
  OutTextAt(15, 26, itoa(++error_count, temp_string_space, 10));
  if (error_count < max_errors)
    return 0;
  else
    return TOO_MANY_ERRORS;
}

void ShowXferStatus(char *string)
{
  HChar(13, 26, 42, ' ');
  OutTextAt(13, 26, string);
}

void StartXferTimer(void)
{
  time(&start_time);
}

void UpdateXferBox(WORD num_bytes_sent)
{
  char temp_string_space[20];

  total_bytes_sent += num_bytes_sent;
  OutTextAt(12, 26, ltoa(total_bytes_sent, temp_string_space, 10));

  time(&current_time);
  elapsed_time = current_time - start_time;

  if (elapsed_time)
  {
    ConvertTimeToString(elapsed_time, temp_string_space);
    OutTextAt(10, 26, temp_string_space);
    avg_chars_per_second += total_bytes_sent / elapsed_time;
    chars_per_second = (WORD)(avg_chars_per_second / ++num_times_CPS_calculated);
    OutTextAt(10, 62, itoa(chars_per_second, temp_string_space, 10));
    OutChar(' ');
    if (file_size > 0 && chars_per_second)
    {
      ConvertTimeToString((file_size - total_bytes_sent) / chars_per_second, temp_string_space);
      OutTextAt(10, 47, temp_string_space);
    }
    else
      OutTextAt(10, 47, "???");
  }
}

int ShowXferErrorNAK(void)
{
  char temp_string_space[23];

  sprintf(temp_string_space, "NAK at byte %ld", total_bytes_sent);
  return ShowXferError(temp_string_space);
}

int ShowXferErrorTimeOut(void)
{
  char temp_string_space[33];

  sprintf(temp_string_space, "Timeout error at byte %ld", total_bytes_sent);
  return ShowXferError(temp_string_space);
}

int ShowXferErrorLongPacket(void)
{
  char temp_string_space[33];

  sprintf(temp_string_space, "Long packet at byte %ld", total_bytes_sent);
  return ShowXferError(temp_string_space);
}

int ShowXferErrorShortPacket(void)
{
  char temp_string_space[33];

  sprintf(temp_string_space, "Short packet at byte %ld", total_bytes_sent);
  return ShowXferError(temp_string_space);
}

int ShowXferErrorBadChecksum(void)
{
  char temp_string_space[33];

  sprintf(temp_string_space, "Bad checksum at byte %ld", total_bytes_sent);
  return ShowXferError(temp_string_space);
}

void ShowFinalXferStatus(int status, BOOLN play_music)
{
  switch (status)
  {
  case 0:
    ShowXferStatus("Transmission successful");
    break;

  case 1:
    ShowXferStatus("Transmission aborted by local user");
    break;

  case 2:
    ShowXferStatus("Transmission aborted by remote");
    break;

  case 3:
    ShowXferStatus("Transmission aborted - too many errors");
    break;

  case 4:
    ShowXferStatus("Timed out waiting to begin");
    break;
  }
  if (play_music == TRUE)
    PlayMusic();
}
