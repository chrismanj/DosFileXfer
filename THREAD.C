#include <stdio.h>
#include <dos.h>
#include <string.h>
#include <stdlib.h>

#include "..\common\include\types.h"
#include "..\common\include\debug.h"
#include "..\common\include\bqueue.h"
#include "..\common\include\jscser.h"
#include "..\common\include\doublell.h"
#include "..\common\include\intrface.h"
#include "..\filexfer\include\filexfer.h"

extern long thread_timeout;

/******************************************************************************\

  Routine: ThreadUpload

 Function:

     Pass:

   Return:

\******************************************************************************/

void ThreadUpload(s_com_port *port)
{
  WORD block_size;
  DWORD thread_checksum;
  BYTE *buffer;
  BOOLN xmit_finished = FALSE;
  FILE *file;
  WORD buffer_loc;
  WORD x = 0;
  char thread_filename[161] = "";
  char temp_str[81];
  struct find_t file_info;
  BYTE in_ch;
  BYTE ch = 0;
  int xmit_canceled = 0;
  BYTE cur_ch;
  long oldTimeout;

  oldTimeout = GetReadTimeout(port);
  SetReadTimeout(port, thread_timeout);

  /* Get filename */
  while (ch != 13)
  {
    ch = (BYTE)(SerialReadWTimeout(port) & 0x7f);
    if (ch != 13)
    {
      if (x < 159)
        thread_filename[x++] = ch;
      else
      {
        ErrorBox("Bad filename");
        SerialStringWrite(port, "N\r");
        return;
      }
    }
    else
      thread_filename[x] = 0;
  }
  if (_dos_findfirst(thread_filename, 0, &file_info))
  {
    ErrorBox("Could not find file");
    SerialStringWrite(port, "N\r");
    return;
  }
  if ((file = fopen(thread_filename, "rb")) == NULL)
  {
    ErrorBox("Could not open file for reading");
    SerialStringWrite(port, "N\r");
    return;
  }
  InitXferBox(thread_filename, 5, UPLOAD, file_info.size);
  SetXferBoxName("Thread Upload");

  /* Get block size */
  ch = 0;
  x = 0;
  ShowXferStatus("Getting block size");
  GetCommError(port);
  while (ch != 13)
  {
    temp_str[x] = 0;
    ch = (BYTE)(SerialReadWTimeout(port) & 0x7f);
    if (ch != 13)
    {
      if (x < 8 && !GetCommError(port))
        temp_str[x++] = ch;
      else
      {
        ErrorBox("Error getting block size");
        SerialStringWrite(port, "N\r");
        RestoreUnderXferBox();
        return;
      }
    }
  }
  block_size = atoi(temp_str);
  if ((buffer = malloc((size_t)512)) == NULL)
  {
    SerialStringWrite(port, "N\r");
    RestoreUnderXferBox();
    ErrorBox("Couldn't allocate buffer");
    return;
  }
  SerialStringWrite(port, "Y\r"); /* If we've made it this far all is fine */
  StartXferTimer();
  ch = (BYTE)fgetc(file);
  while (xmit_finished == FALSE && !xmit_canceled)
  {
    /* Build packet */
    buffer_loc = 0;
    while (buffer_loc < block_size - 5 && !feof(file))
    {
      if (ch < (BYTE)32 || ch == (BYTE)'|')
      {
        sprintf(temp_str, "%03u", ch);
        *(buffer + buffer_loc++) = '|';
        *(buffer + buffer_loc++) = temp_str[0];
        *(buffer + buffer_loc++) = temp_str[1];
        *(buffer + buffer_loc++) = temp_str[2];
      }
      else
        *(buffer + buffer_loc++) = ch;
      ch = (BYTE)fgetc(file);
    }
    in_ch = 0;
    /* Transmit packet */
    while (in_ch != (BYTE)0x8d && !xmit_canceled)
    {
      ShowXferStatus("Sending block length");
      SerialStringWrite(port, itoa(buffer_loc, temp_str, 10)); /* Send block length */
      SerialWrite(port, '\r');
      ShowXferStatus("Waiting for block length ACK");
      in_ch = WaitForCharSerial(port, (BYTE)0x8d); /* Wait for CR */
      if (in_ch != (BYTE)0x8d)
        xmit_canceled = ShowXferError("Did not receive ACK for block length");
      else
      {
        ShowXferStatus("Sending packet");
        thread_checksum = 0;
        GetCommError(port);
        for (x = 0; x < buffer_loc && !GetCommError(port); x++) /* Xmit packet */
        {
          cur_ch = *(buffer + x);
          SerialWrite(port, cur_ch);
          sprintf(temp_str, "Sending BYTE %03u of %03u - %03u", x, buffer_loc, (WORD)cur_ch);
          ShowXferStatus(temp_str);
          thread_checksum += cur_ch;
          sprintf(temp_str, "Waiting for echo for BYTE %03u of %03u - %03u", x, buffer_loc, (WORD)cur_ch);
          ShowXferStatus(temp_str);
          if (WaitForCharSerial(port, (BYTE)(cur_ch | 0x80)) != (BYTE)(cur_ch | 0x80)) /* Wait for echo */
            xmit_canceled = ShowXferError("Did not receive echo.");
          if (x == 126 && buffer_loc > 127)
            SerialWrite(port, '\r');
        }
        SerialWrite(port, '\r');
        ShowXferStatus("Waiting for OK to send checksum");
        if (WaitForCharSerial(port, 0x8d) != 0x8d) /* Wait for CR */
          xmit_canceled = ShowXferError("Did not receive OK to send checksum");
        else
        {
          ShowXferStatus("Sending checksum");
          ultoa(thread_checksum, temp_str, 10);
          SerialStringWrite(port, temp_str); /* Send checksum */
          SerialWrite(port, '\r');
          ShowXferStatus("Waiting for packet ACK");
          in_ch = WaitForCharsSerial(port, "\x8d\x8a");
          if (in_ch == (BYTE)0x8d)
            UpdateXferBox(buffer_loc);
          else if (in_ch == (BYTE)0x8a)
          {
            sprintf(temp_str, "Bad checksum - %08lu", thread_checksum);
            xmit_canceled = ShowXferError(temp_str);
          }
          else
            xmit_canceled = ShowXferError("Packet not acknowledged");
        }
      }
    }
    if (feof(file))
      xmit_finished = TRUE;
  }
  SerialStringWrite(port, "0\r");
  WaitForCharSerial(port, (BYTE)'\x8d');
  ShowFinalXferStatus(xmit_canceled, FALSE);
  RestoreUnderXferBox();
  free(buffer);
  fclose(file);
  SetReadTimeout(port, oldTimeout);
}

/******************************************************************************\

  Routine: ThreadDownload

 Function:

     Pass:

   Return:

\******************************************************************************/

void ThreadDownload(s_com_port *port)
{
  BYTE thread_block = 1;
  BYTE block_number;
  BYTE remote_block_num;
  WORD block_size;
  DWORD thread_checksum;
  DWORD remote_checksum;
  BYTE *buffer;
  BOOLN xmit_finished = FALSE;
  FILE *file;
  BYTE *buffer_loc;
  WORD x = 0;
  char thread_filename[161] = "";
  char temp_str[7];
  BYTE ch = 0;
  int xmit_canceled = 0;
  BOOLN packet_received;
  long oldTimeout;

  oldTimeout = GetReadTimeout(port);
  SetReadTimeout(port, thread_timeout);

  /* Get rid of the asterisk before the filename */
  ch = (BYTE)(SerialReadWTimeout(port) & 0x7f);
  if (GetCommError(port) != 0)
  {
    SerialStringWrite(port, "\x9b");
    return;
  }

  /* Get filename */
  while (ch != 13)
  {
    ch = (BYTE)(SerialReadWTimeout(port) & 0x7f);
    if (GetCommError(port) != 0)
    {
      SerialWrite(port, (BYTE)'\x9b');
      return;
    }
    if (ch != 13)
    {
      if (x < 159)
        thread_filename[x++] = ch;
      else
      {
        SerialWrite(port, (BYTE)'\x9b');
        return;
      }
    }
    else
      thread_filename[x] = 0;
  }
  if ((buffer = malloc((size_t)512)) == NULL)
  {
    SerialWrite(port, (BYTE)'\x9b');
    return;
  }
  if ((file = fopen(thread_filename, "wb")) == NULL)
  {
    SerialStringWrite(port, "N\r");
    free(buffer);
    return;
  }
  SerialStringWrite(port, "Y\r"); /* If we've made it this far all is fine */
  InitXferBox(thread_filename, 30, DOWNLOAD, 0);
  SetXferBoxName("Thread Download");
  StartXferTimer();
  while (xmit_finished == FALSE && !xmit_canceled)
  {
    packet_received = FALSE;
    while (packet_received == FALSE && !xmit_canceled)
    {
      thread_checksum = 0;
      buffer_loc = buffer;

      ShowXferStatus("Waiting for packet");
      ch = WaitForCharSerial(port, (BYTE)'\x9b');
      if (ch != (BYTE)0x9b)
      {
        xmit_canceled = ShowXferError("Did not receive starting escape");
        break;
      }
      ch = (BYTE)(SerialReadWTimeout(port) & 0x7f);
      if (ch != 'T')
      {
        xmit_canceled = ShowXferError("Did not receive starting T");
        break;
      }
      ch = (BYTE)(SerialReadWTimeout(port) & 0x7f);
      if (ch == 0)
      {
        ShowXferStatus("Receiving block size");
        for (x = 0; x < 3; x++)
        {
          temp_str[x] = (BYTE)(SerialReadWTimeout(port) & 0x7f);
          if (GetCommError(port) != 0)
          {
            xmit_canceled = ShowXferError("Timed out waiting to receive block size");
            break;
          }
        }
        temp_str[3] = 0;
        block_size = atoi(temp_str);

        ShowXferStatus("Receiving block number");
        for (x = 0; x < 3; x++)
        {
          temp_str[x] = (BYTE)(SerialReadWTimeout(port) & 0x7f);
          if (GetCommError(port) != 0)
          {
            xmit_canceled = ShowXferError("Timed out waiting to receive block number");
            break;
          }
        }
        temp_str[3] = 0;
        remote_block_num = (BYTE)atoi(temp_str);

        ShowXferStatus("Receiving packet");
        for (x = 0; x < block_size; x++)
        {
          ch = (BYTE)(SerialReadWTimeout(port) & 0x7f);
          if (GetCommError(port) == 0)
          {
            *buffer_loc = ch;
            buffer_loc++;
            thread_checksum += ch;
          }
          else
          {
            xmit_canceled = ShowXferError("Timed out receiving packet");
            break;
          }
        }

        ShowXferStatus("Receiving checksum");
        for (x = 0; x < 6; x++)
        {
          temp_str[x] = (BYTE)(SerialReadWTimeout(port) & 0x7f);
          if (GetCommError(port) != 0)
          {
            xmit_canceled = ShowXferError("Timed out waiting to receive checksum");
            break;
          }
        }
        temp_str[6] = 0;
        remote_checksum = atol(temp_str);

        if (remote_checksum != thread_checksum)
        {
          xmit_canceled = ShowXferError("Bad checksum");
          break;
        }
        if (remote_block_num != thread_block)
        {
          xmit_canceled = ShowXferError("Bad block number");
          break;
        }
        UpdateXferBox(block_size);
        thread_block += 1;
        if (thread_block == 128)
          thread_block = 1;
        fwrite(buffer, (size_t)1, (size_t)block_size, file);
        packet_received = TRUE;
      }
      else
      {
        if (ch == 0x1b)
        {
          if ((SerialReadWTimeout(port) & 0x7f) == 'Q')
          {
            xmit_finished = TRUE;
            SerialStringWrite(port, "Y\r");
            break;
          }
        }
      }
    }
    if (packet_received == TRUE)
      SerialStringWrite(port, "Y\r");
    else
      SerialStringWrite(port, "N\r");
  }
  ShowFinalXferStatus(xmit_canceled, FALSE);
  RestoreUnderXferBox();
  free(buffer);
  fclose(file);
  SetReadTimeout(port, oldTimeout);
}
