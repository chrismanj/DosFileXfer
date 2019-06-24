/******************************************************************************\
Kermit File Xfer v1.0
by John Chrisman
2-20-98
\******************************************************************************/

#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>

#include "..\common\include\types.h"
#include "..\common\include\debug.h"
#include "..\common\include\bqueue.h"
#include "..\common\include\jscser.h"
#include "..\common\include\chrgraph.h"
#include "..\common\include\jscio.h"
#include "..\common\include\intrface.h"
#include "..\common\include\jsctime.h"
#include "..\common\include\doublell.h"
#include "..\common\include\mem.h"
#include "..\filexfer\include\filexfer.h"
#include "..\filexfer\include\kermit.h"

#define TIMEOUT_VALUE 100L

static BYTE *send_buffer;
static BYTE *send_buffer_loc;
static BYTE *receive_buffer;
static FILE *xfer_file;
static BYTE sequence_number = 31;

#ifdef DEBUG
FILE *xfer_details_file;
#endif

/******************************************************************************\

  Routine: Calc6BitChecksum

 Function:

     Pass: Nothing

   Return:

\******************************************************************************/

BYTE Calc6BitChecksum(BYTE *buffer)
{
  int x;
  BYTE s = 0;

  for (x = 0; x < (*buffer - 32); x++)
    s += *(buffer + x);
  return (BYTE)(((s + ((s >> 6) & 3)) & 63) + 32);
}

/******************************************************************************\

  Routine: SendKPacket

 Function:

     Pass: Nothing

   Return:

\******************************************************************************/

void SendKPacket(void)
{
  int x;
#ifdef DEBUG
  fprintf(xfer_details_file, "\n Sent: ");
#endif
  *(send_buffer + 1) = sequence_number;
  *(send_buffer + (*send_buffer - 32)) = Calc6BitChecksum(send_buffer);
  SerialWrite(0, (BYTE)0x01); /* Send the start of packet character */
#ifdef DEBUG
  fputc(0x01, xfer_details_file);
#endif
  for (x = 0; x < *send_buffer - 31; x++)
  {
    SerialWrite(0, *(send_buffer + x));
#ifdef DEBUG
    fputc(*(send_buffer + x), xfer_details_file);
#endif
  }
  SerialWrite(0, (BYTE)13); /* Send the end of packet character */
#ifdef DEBUG
  fputc(13, xfer_details_file);
#endif
}

/******************************************************************************\

  Routine: ReceiveKPacket

 Function: Receive a Kermit packet

     Pass: Nothing

   Return: 0 = Unsuccessful
     1 = Successful
     3 = Error count exceeded

\******************************************************************************/

int ReceiveKPacket(void)
{
  BYTE *receive_buffer_loc = receive_buffer;
  int ch = 0;
  long timer;
  int bytes_received = 0;

#ifdef DEBUG
  fprintf(xfer_details_file, "\nRec'd: ");
#endif

  /**************************************************************\
  Read in characters until a start of packet character is received
  or TIMEOUT_VALUE seconds have elapsed
  \**************************************************************/

  timer = StartTimer();
  do
    ch = SerialRead(0);
  while (ch != 0x01 && TimerValue(timer) < TIMEOUT_VALUE);

  if (TimerValue(timer) < TIMEOUT_VALUE)
  {
#ifdef DEBUG
    fputc(0x01, xfer_details_file);
#endif

    /**************************************************************\
    Read in the packet, allow no more than TIMEOUT_VALUE seconds
    between received characters
    \**************************************************************/

    do
    {
      timer = StartTimer();
      while (!ReadySerial(0) && TimerValue(timer) < TIMEOUT_VALUE)
        ;
      if (TimerValue(timer) < TIMEOUT_VALUE)
      {
        ch = SerialRead(0);
        if (bytes_received == 95 && ch != 13)
          return ShowXferErrorLongPacket();
        *receive_buffer_loc++ = (char)ch;
        bytes_received++;
#ifdef DEBUG
        fputc(ch, xfer_details_file);
#endif
      }
      else
        return ShowXferErrorTimeOut();
    } while (ch != 13);
    if (*receive_buffer - 32 < bytes_received - 2)
      return ShowXferErrorShortPacket();
    if (*receive_buffer - 32 > bytes_received - 2)
      return ShowXferErrorLongPacket();
    if (*(receive_buffer + bytes_received - 2) != Calc6BitChecksum(receive_buffer))
      return ShowXferErrorBadChecksum();
  }
  else
    return ShowXferErrorTimeOut();

  return 1;
}

/******************************************************************************\

  Routine: ReceiveAndVerifyKPacket

 Function: Receive a Kermit packet, repsond to any errors

     Pass: Nothing

   Return: 0 = Unsuccessful
     1 = Successful
     3 = Error count exceeded

\******************************************************************************/

int ReceiveAndVerifyKPacket(void)
{
  int done = 0;
  int err;

  while (!done)
  {
    err = ReceiveKPacket();
    if (err == 3)
    {
      done = 1;
      return 3;
    }
    if (err == 1)
    {
      switch (*(receive_buffer + 2))
      {
      case 'Y':
        done = 1;
        break;

      case 'N':
        if (ShowXferErrorNAK() == 3)
        {
          done = 1;
          return 3;
        }
        SendKPacket();
        break;

      case 'E':
        done = 1;
        return 2;
      }
    }
  }
  return 1;
}

/******************************************************************************\

  Routine: CleanUpKermit

 Function:

     Pass: Nothing

   Return:

\******************************************************************************/

static void CleanUpKermit(void)
{
#ifdef DEBUG
  fclose(xfer_details_file);
#endif
  if (xfer_file != NULL)
    fclose(xfer_file);
  if (send_buffer != NULL)
    free(send_buffer);
  RestoreUnderXferBox();
}

/******************************************************************************\

  Routine: InitSendBuffer

 Function:

     Pass: Byte to place in buffer

   Return:

\******************************************************************************/

void InitSendBuffer(void)
{
  send_buffer_loc = send_buffer + 2;
  *send_buffer = 34; /* Initialize packet length to 2 */
  if (++sequence_number > 95)
    sequence_number = 32;
}

/******************************************************************************\

  Routine: PlaceValueInSendBuffer

 Function:

     Pass: Byte to place in buffer

   Return:

\******************************************************************************/

void PlaceValueInSendBuffer(int value)
{
  *send_buffer_loc++ = (BYTE)value;
  (*send_buffer)++; /* Increment packet length */
}

/******************************************************************************\

  Routine: PlaceStringInSendBuffer

 Function:

     Pass: Byte to place in buffer

   Return:

\******************************************************************************/

void PlaceStringInSendBuffer(char *string)
{
  while (*string)
    PlaceValueInSendBuffer(*string++);
}

/******************************************************************************\

  Routine: InterpretInitPacket

 Function:

     Pass: Nothing

   Return:

\******************************************************************************/

void InterpretInitPacket(void)
{
}

/******************************************************************************\

  Routine: SendKermit

 Function:

     Pass: Nothing

   Return:

\******************************************************************************/

int SendKermit(char *filespec, int max_errors)
{
  struct find_t file_info;
  int done;
  int err;
  int xmit_canceled;
  char *filename;

#ifdef DEBUG
  xfer_details_file = fopen("kxfer", "wb");
#endif

  if ((filename = strrchr(filespec, '\\')) == NULL)
    filename = filespec;
  else
    filename++;
  if ((send_buffer = malloc((size_t)192)) == NULL)
  {
    CleanUpKermit();
    return -1;
  }
  receive_buffer = send_buffer + 96;
  if (_dos_findfirst(filespec, 0, &file_info))
  {
    CleanUpKermit();
    return -1;
  }
  if ((xfer_file = fopen(filespec, "rb")) == NULL)
  {
    CleanUpKermit();
    return -1;
  }
  InitXferBox(filename, 4 /*max_errors*/, UPLOAD, file_info.size);
  SetXferBoxName("Kermit Upload");

  /* ********************************************************************** *\
     Build send-init packet
  \* ********************************************************************** */

  InitSendBuffer();
  PlaceValueInSendBuffer('S'); /* Packet type */
  PlaceValueInSendBuffer(126); /* Largest packet length */
  PlaceValueInSendBuffer(37);  /* Timeout */
  PlaceValueInSendBuffer(32);  /* Number of padding characters */
  PlaceValueInSendBuffer('@'); /* Padding character */
  PlaceValueInSendBuffer(45);  /* End of packet character */
  PlaceValueInSendBuffer('#'); /* Control prefix character */
  PlaceValueInSendBuffer('Y'); /* Eighth bit prefix character */
  PlaceValueInSendBuffer('1'); /* Packet check */
  PlaceValueInSendBuffer('~'); /* Repeat prefix */
  SendKPacket();
  xmit_canceled = ReceiveAndVerifyKPacket();
  InterpretInitPacket();

  /* ********************************************************************** *\
     Build filename packet
  \* ********************************************************************** */

  InitSendBuffer();
  PlaceValueInSendBuffer('F');
  PlaceStringInSendBuffer(filename);
  SendKPacket();
  xmit_canceled = ReceiveAndVerifyKPacket();

  ShowFinalXferStatus(xmit_canceled, TRUE);
  CleanUpKermit();
  return 0;
}

/******************************************************************************\

  Routine: ReceiveKermit

 Function:

     Pass: Nothing

   Return:

\******************************************************************************/

int ReceiveKermit(int max_errors)
{
  struct find_t file_info;
  int xmit_canceled = 0;

#ifdef DEBUG
  xfer_details_file = fopen("kxfer", "wb");
#endif

  if ((send_buffer = malloc((size_t)192)) == NULL)
  {
    CleanUpKermit();
    return -1;
  }
  receive_buffer = send_buffer + 96;

  *send_buffer = (BYTE)0x01;
  InitXferBox("", max_errors, DOWNLOAD, file_info.size);
  SetXferBoxName("Kermit Download");

  /* ************************************************************ *\
     Receive send-init packet
  \* ************************************************************ */

  ReceiveKPacket();

  /* ************************************************************ *\
     Build answer to send-init packet
  \* ************************************************************ */

  InitSendBuffer();
  PlaceValueInSendBuffer('Y'); /* Packet type */
  PlaceValueInSendBuffer(126); /* Largest packet length */
  PlaceValueInSendBuffer(37);  /* Timeout */
  PlaceValueInSendBuffer(32);  /* Number of padding characters */
  PlaceValueInSendBuffer('@'); /* Padding character */
  PlaceValueInSendBuffer(45);  /* End of packet character */
  PlaceValueInSendBuffer('#'); /* Control prefix character */
  PlaceValueInSendBuffer('Y'); /* Eighth bit prefix character */
  PlaceValueInSendBuffer('1'); /* Packet check */
  PlaceValueInSendBuffer('~'); /* Repeat prefix */
  SendKPacket();

  ReceiveKPacket();
  InitSendBuffer();
  PlaceValueInSendBuffer('Y'); /* Packet type */
  SendKPacket();

  StartXferTimer();
  do
  {
    if (ReceiveKPacket() == 3)
      xmit_canceled = 1;
    UpdateXferBox(*receive_buffer - 35);
    InitSendBuffer();
    PlaceValueInSendBuffer('Y'); /* Packet type */
    SendKPacket();
  } while (*(receive_buffer + 2) == 'D' && !xmit_canceled);

  ReceiveKPacket();
  InitSendBuffer();
  PlaceValueInSendBuffer('Y'); /* Packet type */
  SendKPacket();

  ReceiveKPacket();
  InitSendBuffer();
  PlaceValueInSendBuffer('Y'); /* Packet type */
  SendKPacket();

  CleanUpKermit();
  return 0;
}
