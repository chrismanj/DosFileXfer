#define XMODEM 0
#define XMODEMK 1

#define REC_TIMEOUT_AT_START 1
#define REC_TIMEOUT          2
#define REC_BAD_PACKET       3
#define REC_XMIT_ABORT_LOCAL 4
#define REC_XMIT_FINISHED    5

int         SendXModem        (char *, int, int, s_com_port *);
int         ReceiveXModem     (char *, int, int, s_com_port *);
static void CleanUpXModem     (s_com_port *);
void        BuildCRC16Table   (void);
WORD        ComputeCRC16      (BYTE *, int);
