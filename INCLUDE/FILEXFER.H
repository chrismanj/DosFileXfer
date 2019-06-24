#define XMIT_SUCCESS          0
#define XMIT_ABORTED_LOCALLY  1
#define XMIT_ABORTED_REMOTELY 2
#define TOO_MANY_ERRORS       3
#define TIMED_OUT_AT_START    4

#define UPLOAD                0
#define DOWNLOAD              1

void DrawXferBox                 (char *, int);
void RestoreUnderXferBox         (void);
long *InitXferBox                (char *, int, int, long);
void SetXferBoxName              (char *);
void StartXferTimer              (void);
void UpdateXferBox               (WORD);
void ShowXferStatus              (char *);
int  ShowXferError               (char *);
int  ShowXferErrorNAK            (void);
int  ShowXferErrorTimeOut        (void);
int  ShowXferErrorLongPacket     (void);
int  ShowXferErrorShortPacket    (void);
int  ShowXferErrorBadChecksum    (void);
void ShowFinalXferStatus         (int, BOOLN);
void ShowXferStatusTransComplete (void);
