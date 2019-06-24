#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>

#include <c:\progproj\c\common\include\types.h>
#include <c:\progproj\c\common\include\debug.h>
#include <c:\progproj\c\common\include\bqueue.h>
#include <c:\progproj\c\common\include\jscser.h>
#include <c:\progproj\c\common\include\chrgraph.h>
#include <c:\progproj\c\common\include\jscio.h>
#include <c:\progproj\c\common\include\intrface.h>
#include <c:\progproj\c\common\include\jsctime.h>
#include <c:\progproj\c\common\include\keybrd.h>
#include <c:\progproj\c\common\include\doublell.h>
#include <c:\progproj\c\common\include\mem.h>
#include <c:\progproj\c\filexfer\include\filexfer.h>
#include <c:\progproj\c\filexfer\include\xmodem.h>

#define SOH 0x01
#define STX 0x02
#define ACK 0x06
#define NAK 0x15
#define EOT 0x04
#define CAN 0x18
#define TIMEOUT_VALUE 300L

struct receive_xmodem
{
  BYTE sequence_number;
  BYTE num_consecutive_CANS_recd;
  int xmit_canceled;
  BYTE crc_being_used;
  BYTE error_count;
  long bytes_received;
  char start_char;
  int start_chars_sent;
  BOOLN xmit_finished;
  BOOLN waiting_to_begin;
  int data_length;
  int packet_length;
};

static int OldXonXoff;
static BYTE *buffer;
static WORD *crctable;
static FILE *file;

#ifdef DEBUG
static FILE *log_file;
static char *temp_str = "                ";
#endif

int SendXModem(char *filename, int max_errors, int protocol, s_com_port *port)
{
  BYTE sequence_number = 0;
  size_t bytes_read;
  BYTE *buffer_data_start;
  WORD x;
  BOOLN send_packet;
  BOOLN waiting_to_begin = TRUE;
  int status;
  BYTE num_consecutive_CANS_recd;
  int xmit_canceled = 0;
  BOOLN crc_being_used = FALSE;
  BYTE *crc_position_in_packet;
  long timer;
  WORD packet_length;
  WORD data_length;
  struct find_t file_info;
  long bytes_xmitted = 0;

  crctable = NULL;
  buffer = NULL;
  file = NULL;

  OldXonXoff = GetXonXoff(port);
  SetXonXoff(port, 0);

  if ((buffer = malloc((size_t)1541)) == NULL)
  {
    CleanUpXModem(port);
    return -1;
  }
  buffer_data_start = buffer + 3;
  crctable = (WORD *)(buffer + 1029);
  if (_dos_findfirst(filename, 0, &file_info))
  {
    CleanUpXModem(port);
    return -1;
  }
  if ((file = fopen(filename, "rb")) == NULL)
  {
    CleanUpXModem(port);
    return -1;
  }
  BuildCRC16Table();
  switch (protocol)
  {
  case XMODEM:
    *buffer = (BYTE)SOH;
    packet_length = 132;
    data_length = 128;
    break;

  case XMODEMK:
    *buffer = (BYTE)STX;
    packet_length = 1028;
    data_length = 1024;
    break;
  }
  InitXferBox(filename, max_errors, UPLOAD, file_info.size);

  /* Wait for signal to begin transmission */
  num_consecutive_CANS_recd = 0;
  FlushPortInputBuffer(port);
  while (waiting_to_begin == TRUE && !xmit_canceled)
  {
    SerialWrite(port, 17);
    timer = StartTimer();
    while ((ReadySerial(port) == 0) && (TimerValue(timer) < 6000L) && !KeyInBuffer())
      ;
    if (TimerValue(timer) >= 6000L)
      xmit_canceled = TIMED_OUT_AT_START;
    else if (ReadySerial(port))
    {
      status = SerialRead(port);
      switch (status)
      {
      case NAK: /* Normal XModem transfer */
        waiting_to_begin = FALSE;
        SetXferBoxName("Xmodem Upload");
        break;

      case 'C': /* XModem CRC or XModem 1K transfer */
        crc_being_used = TRUE;
        waiting_to_begin = FALSE;
        packet_length++;
        crc_position_in_packet = buffer + packet_length - 2;
        switch (protocol)
        {
        case XMODEM:
          SetXferBoxName("Xmodem CRC Upload");
          break;

        case XMODEMK:
          SetXferBoxName("Xmodem 1K Upload");
          break;
        }
        break;

      case CAN:
        if (++num_consecutive_CANS_recd == 2)
          xmit_canceled = XMIT_ABORTED_REMOTELY;
        break;

      default:
        xmit_canceled = ShowXferError("Invalid start character received");
      }
    }
    else
    {
      if (GetAKey() == KEY_ESC)
        xmit_canceled = XMIT_ABORTED_LOCALLY;
    }
  }

  StartXferTimer();
  while (!feof(file) && !xmit_canceled)
  {
    if (GetAKey() == KEY_ESC)
      xmit_canceled = XMIT_ABORTED_LOCALLY;

    if (data_length == 1024 && (bytes_xmitted + 1024 > file_info.size))
    {
      *buffer = (BYTE)SOH;
      packet_length = 132;
      data_length = 128;
      if (crc_being_used == TRUE)
      {
        packet_length++;
        crc_position_in_packet = buffer + packet_length - 2;
      }
    }
    *(buffer + 1) = ++sequence_number;
    *(buffer + 2) = (BYTE)(~sequence_number);
    bytes_read = fread(buffer_data_start, (size_t)1, (size_t)data_length, file);

    /* Pad the end of the data block with Ctrl-Zs (EOF) if < 128 bytes read in */
    if (bytes_read < 128)
    {
      while (bytes_read != 128)
        *(buffer_data_start + bytes_read++) = (BYTE)EOF;
    }

    /* Calculate checksum or CRC*/
    if (crc_being_used == TRUE)
    {
      WORD crc;

      crc = ComputeCRC16(buffer_data_start, data_length);
      *crc_position_in_packet = (BYTE)(crc >> 8);
      *(crc_position_in_packet + 1) = (BYTE)(crc & 0xff);
    }
    else
    {
      *(buffer + 131) = 0;
      for (x = 0; x < data_length; x++)
        *(buffer + 131) += *(buffer_data_start + x);
    }

    /* Send the packet */
    send_packet = TRUE;
    num_consecutive_CANS_recd = 0;
    while (send_packet == TRUE && !xmit_canceled)
    {
      if (!num_consecutive_CANS_recd)
      {
        ShowXferStatus("Sending packet");
        for (x = 0; x < packet_length; x++)
          SerialWrite(port, *(buffer + x));
        ShowXferStatus("Waiting for ACK");
      }
      timer = StartTimer();

      while ((ReadySerial(port) == 0) && (TimerValue(timer) < TIMEOUT_VALUE))
        ;

      if (TimerValue(timer) >= TIMEOUT_VALUE)
      {
        xmit_canceled = ShowXferErrorTimeOut();
        timer = StartTimer();
      }
      status = SerialRead(port);

      switch (status)
      {
      case (BYTE)ACK:
        send_packet = FALSE;
        break;

      case (BYTE)CAN:
        num_consecutive_CANS_recd++;
        if (num_consecutive_CANS_recd == 2)
          xmit_canceled = XMIT_ABORTED_REMOTELY;
        break;

      case (BYTE)NAK:
        num_consecutive_CANS_recd = 0;
        xmit_canceled = ShowXferErrorNAK();
        break;

      default:
        xmit_canceled = ShowXferError("Invalid response character received");
      }
    }
    UpdateXferBox(data_length);
    bytes_xmitted += data_length;
  }
  if (!xmit_canceled)
    SerialWrite(port, (BYTE)EOT);
  ShowFinalXferStatus(xmit_canceled, TRUE);
  CleanUpXModem(port);
  return 0;
}

int CheckPacketForErrors(struct receive_xmodem *rec)
{
  if (*buffer != rec->sequence_number)
  {
#ifdef DEBUG
    fprintf(log_file, "Incorrect sequence number of %u\n", *buffer);
#endif
    rec->xmit_canceled = ShowXferError("Incorrect sequence number");
    return REC_BAD_PACKET;
  }
  else if (*(buffer + 1) != (BYTE)~rec->sequence_number)
  {
#ifdef DEBUG
    fprintf(log_file, "Incorrect sequence number compliment of %u\n", *(buffer + 1));
#endif
    rec->xmit_canceled = ShowXferError("Sequence number compliment incorrect");
    return REC_BAD_PACKET;
  }
  if (rec->crc_being_used)
  {
    WORD crc;

    crc = ComputeCRC16(buffer + 2, rec->data_length);
    if (*(buffer + rec->packet_length - 2) != (BYTE)(crc >> 8) ||
        *(buffer + rec->packet_length - 1) != (BYTE)(crc & 0xff))
    {
#ifdef DEBUG
      fprintf(log_file, "Incorrect CRC of %u\n", *(buffer + rec->packet_length - 2) << 8 + *(buffer + rec->packet_length - 1));
#endif
      rec->xmit_canceled = ShowXferError("Bad CRC");
      return REC_BAD_PACKET;
    }
  }
  else
  {
    int x;

    BYTE checksum = 0;
    for (x = 0; x < rec->data_length; x++)
      checksum += *(buffer + 2 + x);
    if (checksum != *(buffer + rec->packet_length - 1))
    {
#ifdef DEBUG
      fprintf(log_file, "Incorrect checksum of %u\n", *(buffer + rec->packet_length - 1));
#endif
      rec->xmit_canceled = ShowXferErrorBadChecksum();
      return REC_BAD_PACKET;
    }
  }
  return 0;
}

int ReceivePacket(struct receive_xmodem *rec, s_com_port *port)
{
  int ch;
  long timer;
  int waiting_for_start_of_packet = 1;
  int x;

#ifdef DEBUG
  fprintf(log_file, "Waiting for start of packet\n");
#endif
  timer = StartTimer();
  while (waiting_for_start_of_packet)
  {
    while (!ReadySerial(port) && TimerValue(timer) <= TIMEOUT_VALUE && !KeyInBuffer())
      ;
    if (TimerValue(timer) > TIMEOUT_VALUE)
    {
#ifdef DEBUG
      fprintf(log_file, "Timed out waiting for start of packet\n");
#endif
      waiting_for_start_of_packet = 0;
      SerialWrite(port, NAK);
      return REC_TIMEOUT_AT_START;
    }
    else if (ReadySerial(port))
    {
      ch = SerialRead(port);
      switch (ch)
      {
      case SOH:
#ifdef DEBUG
        fprintf(log_file, "Rec'd SOH (Small packet SOP Char)\n");
#endif
        waiting_for_start_of_packet = 0;
        rec->packet_length = 131;
        rec->data_length = 128;
        break;

      case STX:
#ifdef DEBUG
        fprintf(log_file, "Rec'd STX (Large packet SOP Char)\n");
#endif
        waiting_for_start_of_packet = 0;
        rec->packet_length = 1027;
        rec->data_length = 1024;
        break;

      case EOT:
#ifdef DEBUG
        fprintf(log_file, "Rec'd EOT\n");
#endif
        waiting_for_start_of_packet = 0;
        rec->xmit_finished = TRUE;
        SerialWrite(port, ACK);
        return REC_XMIT_FINISHED;

      default:
#ifdef DEBUG
        fprintf(log_file, "Rec'd %c\n", ch);
#endif
        break;
      }
    }
    else if (GetAKey() == KEY_ESC)
    {
#ifdef DEBUG
      fprintf(log_file, "User aborted transfer\n");
#endif
      waiting_for_start_of_packet = 0;
      SerialWrite(port, CAN);
      SerialWrite(port, CAN);
      rec->xmit_canceled = XMIT_ABORTED_LOCALLY;
      return REC_XMIT_ABORT_LOCAL;
    }
  }
  if (rec->waiting_to_begin == TRUE)
    StartXferTimer();
  ShowXferStatus("Receiving packet");
  if (rec->crc_being_used)
    rec->packet_length++;
#ifdef DEBUG
  fprintf(log_file, "Receiving Packet. Expecting %u bytes\n", rec->packet_length);
#endif
  for (x = 0; x < rec->packet_length; x++)
  {
    timer = StartTimer();
    while (!ReadySerial(port) && TimerValue(timer) <= TIMEOUT_VALUE && !KeyInBuffer())
      ;
    if (TimerValue(timer) > TIMEOUT_VALUE)
    {
#ifdef DEBUG
      fprintf(log_file, "Timed out waiting to receive packet\n");
#endif
      rec->xmit_canceled = ShowXferErrorTimeOut();
      SerialWrite(port, NAK);
      return REC_TIMEOUT;
    }
    else if (ReadySerial(port))
    {
      *(buffer + x) = (char)SerialRead(port);
#ifdef DEBUG
      fprintf(log_file, "Char: %u Val: %u\n", x, *(buffer + x));
#endif
    }
    else if (GetAKey() == KEY_ESC)
    {
#ifdef DEBUG
      fprintf(log_file, "User aborted transfer\n");
#endif
      SerialWrite(port, CAN);
      SerialWrite(port, CAN);
      rec->xmit_canceled = XMIT_ABORTED_LOCALLY;
      /*return XMIT_ABORTED_LOCALLY;*/
    }
  }
  if (rec->xmit_canceled)
    return rec->xmit_canceled;

  if (CheckPacketForErrors(rec) == 0)
  {
#ifdef DEBUG
    fprintf(log_file, "Acking packet\n");
#endif
    SerialWrite(port, ACK);
    rec->sequence_number++;
    fwrite(buffer + 2, (size_t)1, (size_t)rec->data_length, file);
    UpdateXferBox(rec->data_length);
    ShowXferStatus("Sending ACK");
  }
  else
  {
#ifdef DEBUG
    fprintf(log_file, "Naking packet\n");
#endif
    SerialWrite(port, NAK);
  }
  return 0;
}

int ReceiveXModem(char *filename, int max_errors, int protocol, s_com_port *port)
{
  struct receive_xmodem receive;
  WORD x;

  receive.sequence_number = 1;
  receive.num_consecutive_CANS_recd = 0;
  receive.xmit_canceled = 0;
  receive.crc_being_used = 1;
  receive.error_count = 0;
  receive.bytes_received = 0;
  receive.start_char = 'C';
  receive.start_chars_sent = 0;
  receive.xmit_finished = FALSE;
  receive.waiting_to_begin = TRUE;

  crctable = NULL;
  buffer = NULL;
  file = NULL;

  OldXonXoff = GetXonXoff(port);
  SetXonXoff(port, 0);

  if ((buffer = malloc((size_t)1541)) == NULL)
  {
    CleanUpXModem(port);
    return -1;
  }
  crctable = (WORD *)(buffer + 1029);

  if ((file = fopen(filename, "wb")) == NULL)
  {
    CleanUpXModem(port);
    return -1;
  }
  BuildCRC16Table();

  InitXferBox(filename, max_errors, DOWNLOAD, 0);
  switch (protocol)
  {
  case XMODEM:
    SetXferBoxName("Xmodem Download");
    break;

  case XMODEMK:
    SetXferBoxName("Xmodem 1K Download");
    break;
  }
#ifdef DEBUG
  log_file = fopen("xrecv.log", "wb");
#endif

  /* Send signal to begin transmission */
  while (!receive.xmit_canceled && receive.waiting_to_begin == TRUE)
  {
    SerialWrite(port, receive.start_char);
    receive.start_chars_sent++;
    x = ReceivePacket(&receive, port);
    if (x == REC_TIMEOUT_AT_START)
    {
      if (receive.start_chars_sent == 4)
      {
        receive.start_char = NAK;
        receive.crc_being_used = 0;
      }
      if (receive.start_chars_sent == 12)
        receive.xmit_canceled = TIMED_OUT_AT_START;
    }
    else if (!receive.xmit_canceled)
    {
      receive.waiting_to_begin = FALSE;
      if (protocol == XMODEM && receive.start_char == 'C')
        SetXferBoxName("Xmodem CRC Download");
    }
  }

  while (!receive.xmit_canceled && receive.xmit_finished == FALSE)
  {
    x = ReceivePacket(&receive, port);
    if (x == REC_TIMEOUT_AT_START)
      ShowXferError("Timed out waiting to receive packet.");
  }
  ShowFinalXferStatus(receive.xmit_canceled, TRUE);
  CleanUpXModem(port);
  return 0;
}

static void CleanUpXModem(s_com_port *port)
{
  SetXonXoff(port, OldXonXoff);
  if (buffer != NULL)
    free(buffer);
  if (file != NULL)
    fclose(file);
#ifdef DEBUG
  if (log_file != NULL)
    fclose(log_file);
#endif
  RestoreUnderXferBox();
}

void BuildCRC16Table(void)
{
  int x, y;
  WORD crc;

  for (x = 0; x < 256; x++)
  {
    crc = x << 8;
    for (y = 0; y < 8; y++)
      crc = (crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0);
    *(crctable + x) = crc;
  }
}

WORD ComputeCRC16(BYTE *buffer, int length)
{
  WORD crc = 0;

  while (length--)
    crc = *(crctable + (((crc >> 8) ^ *buffer++) & 0xff)) ^ (crc << 8);

  return crc;
}
